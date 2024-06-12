#include "gd_scene_synchronizer.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/object.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "gd_data_buffer.h"
#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/core/data_buffer.h"
#include "modules/network_synchronizer/core/net_utilities.h"
#include "modules/network_synchronizer/core/object_data.h"
#include "modules/network_synchronizer/core/processor.h"
#include "modules/network_synchronizer/core/scene_synchronizer_debugger.h"
#include "modules/network_synchronizer/godot4/gd_network_interface.h"
#include "modules/network_synchronizer/godot4/gd_scene_synchronizer.h"
#include "modules/network_synchronizer/scene_synchronizer.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/node.h"
#include "scene/main/window.h"
#include <cstring>
#include <string>
#include <vector>

std::string GdFileSystem::get_base_dir() const {
	return OS::get_singleton()->get_executable_path().get_base_dir().utf8().ptr();
}

std::string GdFileSystem::get_date() const {
	OS::DateTime date = OS::get_singleton()->get_datetime();
	return std::to_string(date.day) + "/" + std::to_string(date.month) + "/" + std::to_string(date.year);
}

std::string GdFileSystem::get_time() const {
	OS::DateTime date = OS::get_singleton()->get_datetime();
	return std::to_string(date.hour) + "::" + std::to_string(date.minute);
}

bool GdFileSystem::make_dir_recursive(const std::string &p_dir_path, bool p_erase_content) const {
	Ref<DirAccess> dir = DirAccess::create_for_path(p_dir_path.c_str());

	Error e;
	e = dir->make_dir_recursive(p_dir_path.c_str());

	ERR_FAIL_COND_V(e != OK, false);

	e = dir->change_dir(p_dir_path.c_str());

	ERR_FAIL_COND_V(e != OK, false);

	if (p_erase_content) {
		dir->erase_contents_recursive();
	}

	return true;
}

bool GdFileSystem::store_file_string(const std::string &p_path, const std::string &p_string_file) const {
	Error e;
	Ref<FileAccess> file = FileAccess::open(String(p_path.c_str()), FileAccess::WRITE, &e);

	ERR_FAIL_COND_V(e != OK, false);

	file->flush();
	file->store_string(p_string_file.c_str());

	return true;
}

bool GdFileSystem::store_file_buffer(const std::string &p_path, const std::uint8_t *p_src, uint64_t p_length) const {
	Ref<FileAccess> f = FileAccess::open(p_path.c_str(), FileAccess::WRITE);
	ENSURE_V_MSG(!f.is_null(), false, "Can't create the `" + p_path + "` file.");
	f->store_buffer(p_src, p_length);
	return true;
}

bool GdFileSystem::file_exists(const std::string &p_path) const {
	return FileAccess::exists(p_path.c_str());
}

const uint32_t GdSceneSynchronizer::GLOBAL_SYNC_GROUP_ID = NS::SyncGroupId::GLOBAL.id;

void GdSceneSynchronizer::_bind_methods() {
	BIND_CONSTANT(GLOBAL_SYNC_GROUP_ID)

	BIND_ENUM_CONSTANT(CHANGE)
	BIND_ENUM_CONSTANT(SYNC_RECOVER)
	BIND_ENUM_CONSTANT(SYNC_RESET)
	BIND_ENUM_CONSTANT(SYNC_REWIND)
	BIND_ENUM_CONSTANT(END_SYNC)
	BIND_ENUM_CONSTANT(DEFAULT)
	BIND_ENUM_CONSTANT(SYNC)
	BIND_ENUM_CONSTANT(ALWAYS)

	BIND_ENUM_CONSTANT(PROCESS_PHASE_EARLY)
	BIND_ENUM_CONSTANT(PROCESS_PHASE_PRE)
	BIND_ENUM_CONSTANT(PROCESS_PHASE_PROCESS)
	BIND_ENUM_CONSTANT(PROCESS_PHASE_POST)
	BIND_ENUM_CONSTANT(PROCESS_PHASE_LATE)

	ClassDB::bind_method(D_METHOD("reset_synchronizer_mode"), &GdSceneSynchronizer::reset_synchronizer_mode);
	ClassDB::bind_method(D_METHOD("clear"), &GdSceneSynchronizer::clear);

	ClassDB::bind_method(D_METHOD("set_netstats_update_interval_sec", "delay_in_ms"), &GdSceneSynchronizer::set_netstats_update_interval_sec);
	ClassDB::bind_method(D_METHOD("get_netstats_update_interval_sec"), &GdSceneSynchronizer::get_netstats_update_interval_sec);

	ClassDB::bind_method(D_METHOD("set_max_fps_acceleration_percentage", "acceleration"), &GdSceneSynchronizer::set_max_fps_acceleration_percentage);
	ClassDB::bind_method(D_METHOD("get_max_fps_acceleration_percentage"), &GdSceneSynchronizer::get_max_fps_acceleration_percentage);

	ClassDB::bind_method(D_METHOD("set_max_trickled_nodes_per_update", "rate"), &GdSceneSynchronizer::set_max_trickled_nodes_per_update);
	ClassDB::bind_method(D_METHOD("get_max_trickled_nodes_per_update"), &GdSceneSynchronizer::get_max_trickled_nodes_per_update);

	ClassDB::bind_method(D_METHOD("set_frame_confirmation_timespan", "interval"), &GdSceneSynchronizer::set_frame_confirmation_timespan);
	ClassDB::bind_method(D_METHOD("get_frame_confirmation_timespan"), &GdSceneSynchronizer::get_frame_confirmation_timespan);

	ClassDB::bind_method(D_METHOD("set_nodes_relevancy_update_time", "time"), &GdSceneSynchronizer::set_nodes_relevancy_update_time);
	ClassDB::bind_method(D_METHOD("get_nodes_relevancy_update_time"), &GdSceneSynchronizer::get_nodes_relevancy_update_time);

	ClassDB::bind_method(D_METHOD("set_frames_per_seconds", "fps"), &GdSceneSynchronizer::set_frames_per_seconds);
	ClassDB::bind_method(D_METHOD("get_frames_per_seconds"), &GdSceneSynchronizer::get_frames_per_seconds);

	ClassDB::bind_method(D_METHOD("register_node", "node"), &GdSceneSynchronizer::register_node_gdscript);
	ClassDB::bind_method(D_METHOD("unregister_node", "node"), &GdSceneSynchronizer::unregister_node);
	ClassDB::bind_method(D_METHOD("setup_controller", "node", "peer", "collect_input_func", "count_input_size_func", "are_inputs_different_func", "proces_func"), &GdSceneSynchronizer::setup_controller);
	ClassDB::bind_method(D_METHOD("get_node_id", "node"), &GdSceneSynchronizer::get_node_id);
	ClassDB::bind_method(D_METHOD("get_node_from_id", "id", "expected"), &GdSceneSynchronizer::get_node_from_id, DEFVAL(true));

	ClassDB::bind_method(D_METHOD("register_variable", "node", "variable"), &GdSceneSynchronizer::register_variable);
	ClassDB::bind_method(D_METHOD("unregister_variable", "node", "variable"), &GdSceneSynchronizer::unregister_variable);
	ClassDB::bind_method(D_METHOD("get_variable_id", "node", "variable"), &GdSceneSynchronizer::get_variable_id);

	ClassDB::bind_method(D_METHOD("set_skip_rewinding", "node", "variable", "skip_rewinding"), &GdSceneSynchronizer::set_skip_rewinding);

	ClassDB::bind_method(D_METHOD("track_variable_changes", "nodes", "variables", "callable", "flags"), &GdSceneSynchronizer::track_variable_changes, DEFVAL(NetEventFlag::DEFAULT));
	ClassDB::bind_method(D_METHOD("untrack_variable_changes", "handle"), &GdSceneSynchronizer::untrack_variable_changes);

	ClassDB::bind_method(D_METHOD("register_process", "node", "phase", "function"), &GdSceneSynchronizer::register_process);
	ClassDB::bind_method(D_METHOD("unregister_process", "node", "phase", "function"), &GdSceneSynchronizer::unregister_process);

	ClassDB::bind_method(D_METHOD("local_controller_get_controlled_nodes"), &GdSceneSynchronizer::local_controller_get_controlled_nodes);

	ClassDB::bind_method(D_METHOD("setup_trickled_sync", "node", "collect_epoch_func", "apply_epoch_func"), &GdSceneSynchronizer::setup_trickled_sync);

	ClassDB::bind_method(D_METHOD("get_peer_latency", "peer"), &GdSceneSynchronizer::get_peer_latency_ms); // TODO deprecated, remove.
	ClassDB::bind_method(D_METHOD("get_peer_latency_ms", "peer"), &GdSceneSynchronizer::get_peer_latency_ms);
	ClassDB::bind_method(D_METHOD("get_peer_latency_jitter_ms", "peer"), &GdSceneSynchronizer::get_peer_latency_jitter_ms);
	ClassDB::bind_method(D_METHOD("get_peer_packet_loss_percentage", "peer"), &GdSceneSynchronizer::get_peer_packet_loss_percentage);

	ClassDB::bind_method(D_METHOD("sync_group_create"), &GdSceneSynchronizer::sync_group_create);
	ClassDB::bind_method(D_METHOD("sync_group_add_node", "node_id", "group_id", "realtime"), &GdSceneSynchronizer::sync_group_add_node_by_id);
	ClassDB::bind_method(D_METHOD("sync_group_remove_node", "node_id", "group_id"), &GdSceneSynchronizer::sync_group_remove_node_by_id);
	ClassDB::bind_method(D_METHOD("sync_group_move_peer_to", "peer_id", "group_id"), &GdSceneSynchronizer::sync_group_move_peer_to);
	ClassDB::bind_method(D_METHOD("sync_group_set_trickled_update_rate", "node_id", "group_id", "update_rate"), &GdSceneSynchronizer::sync_group_set_trickled_update_rate_by_id);
	ClassDB::bind_method(D_METHOD("sync_group_get_trickled_update_rate", "node_id", "group_id"), &GdSceneSynchronizer::sync_group_get_trickled_update_rate_by_id);

	ClassDB::bind_method(D_METHOD("is_recovered"), &GdSceneSynchronizer::is_recovered);
	ClassDB::bind_method(D_METHOD("is_resetted"), &GdSceneSynchronizer::is_resetted);
	ClassDB::bind_method(D_METHOD("is_rewinding"), &GdSceneSynchronizer::is_rewinding);
	ClassDB::bind_method(D_METHOD("is_end_sync"), &GdSceneSynchronizer::is_end_sync);

	ClassDB::bind_method(D_METHOD("force_state_notify", "group_id"), &GdSceneSynchronizer::force_state_notify);
	ClassDB::bind_method(D_METHOD("force_state_notify_all"), &GdSceneSynchronizer::force_state_notify_all);

	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &GdSceneSynchronizer::set_enabled);
	ClassDB::bind_method(D_METHOD("set_peer_networking_enable", "peer", "enabled"), &GdSceneSynchronizer::set_peer_networking_enable);
	ClassDB::bind_method(D_METHOD("get_peer_networking_enabled", "peer"), &GdSceneSynchronizer::is_peer_networking_enabled);

	ClassDB::bind_method(D_METHOD("is_server"), &GdSceneSynchronizer::is_server);
	ClassDB::bind_method(D_METHOD("is_client"), &GdSceneSynchronizer::is_client);
	ClassDB::bind_method(D_METHOD("is_networked"), &GdSceneSynchronizer::is_networked);

	ClassDB::bind_method(D_METHOD("_rpc_net_sync_reliable"), &GdSceneSynchronizer::_rpc_net_sync_reliable);
	ClassDB::bind_method(D_METHOD("_rpc_net_sync_unreliable"), &GdSceneSynchronizer::_rpc_net_sync_unreliable);

	GDVIRTUAL_BIND(_update_nodes_relevancy);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "netstats_update_interval_sec", PROPERTY_HINT_RANGE, "0,10,0.001"), "set_netstats_update_interval_sec", "get_netstats_update_interval_sec");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_fps_acceleration_percentage", PROPERTY_HINT_RANGE, "0.1,20.0,0.01"), "set_max_fps_acceleration_percentage", "get_max_fps_acceleration_percentage");

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frame_confirmation_timespan", PROPERTY_HINT_RANGE, "0.001,10.0,0.0001"), "set_frame_confirmation_timespan", "get_frame_confirmation_timespan");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "nodes_relevancy_update_time", PROPERTY_HINT_RANGE, "0.0,2.0,0.01"), "set_nodes_relevancy_update_time", "get_nodes_relevancy_update_time");

	ADD_SIGNAL(MethodInfo("sync_started"));
	ADD_SIGNAL(MethodInfo("sync_paused"));
	ADD_SIGNAL(MethodInfo("peer_status_updated", PropertyInfo(Variant::INT, "peer"), PropertyInfo(Variant::BOOL, "connected"), PropertyInfo(Variant::BOOL, "enabled")));

	ADD_SIGNAL(MethodInfo("state_validated", PropertyInfo(Variant::INT, "input_id"), PropertyInfo(Variant::BOOL, "desync_detected")));
	ADD_SIGNAL(MethodInfo("rewind_frame_begin", PropertyInfo(Variant::INT, "input_id"), PropertyInfo(Variant::INT, "index"), PropertyInfo(Variant::INT, "count")));

	ADD_SIGNAL(MethodInfo("desync_detected", PropertyInfo(Variant::INT, "input_id"), PropertyInfo(Variant::OBJECT, "node"), PropertyInfo(Variant::ARRAY, "var_names"), PropertyInfo(Variant::ARRAY, "client_values"), PropertyInfo(Variant::ARRAY, "server_values")));
}

GdSceneSynchronizer::GdSceneSynchronizer() :
		Node(),
		scene_synchronizer(false) {
	rpc_id(1, "");

	Dictionary rpc_config_reliable;
	rpc_config_reliable["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
	rpc_config_reliable["call_local"] = false;
	rpc_config_reliable["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;

	Dictionary rpc_config_unreliable;
	rpc_config_unreliable["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
	rpc_config_unreliable["call_local"] = false;
	rpc_config_unreliable["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_UNRELIABLE;

	rpc_config(SNAME("_rpc_net_sync_reliable"), rpc_config_reliable);
	rpc_config(SNAME("_rpc_net_sync_unreliable"), rpc_config_unreliable);

	event_handler_sync_started =
			scene_synchronizer.event_sync_started.bind([this]() -> void {
				emit_signal("sync_started");
			});

	event_handler_sync_paused =
			scene_synchronizer.event_sync_paused.bind([this]() -> void {
				emit_signal("sync_paused");
			});

	event_handler_peer_status_updated =
			scene_synchronizer.event_peer_status_updated.bind([this](int p_peer, bool p_connected, bool p_enabled) -> void {
				emit_signal(
						"peer_status_updated",
						p_peer,
						p_connected,
						p_enabled);
			});

	event_handler_state_validated =
			scene_synchronizer.event_state_validated.bind([this](NS::FrameIndex p_frame, bool p_desync_detected) -> void {
				emit_signal("state_validated", p_frame.id, p_desync_detected);
			});

	event_handler_rewind_frame_begin =
			scene_synchronizer.event_rewind_frame_begin.bind([this](NS::FrameIndex p_frame, int p_index, int p_count) -> void {
				emit_signal("rewind_frame_begin", p_frame.id, p_index, p_count);
			});

	event_handler_desync_detected =
			scene_synchronizer.event_desync_detected_with_info.bind([this](NS::FrameIndex p_frame, NS::ObjectHandle p_app_object, const std::vector<std::string> &p_var_names, const std::vector<NS::VarData> &p_client_values, const std::vector<NS::VarData> &p_server_values) -> void {
				Vector<String> var_names;
				Vector<Variant> client_values;
				Vector<Variant> server_values;
				for (auto n : p_var_names) {
					var_names.push_back(String(n.c_str()));
				}
				for (const auto &vd : p_client_values) {
					Variant v;
					GdSceneSynchronizer::convert(v, vd);
					client_values.push_back(v);
				}
				for (const auto &vd : p_server_values) {
					Variant v;
					GdSceneSynchronizer::convert(v, vd);
					server_values.push_back(v);
				}
				emit_signal("desync_detected", p_frame.id, GdSceneSynchronizer::SyncClass::from_handle(p_app_object), var_names, client_values, server_values);
			});
}

GdSceneSynchronizer::~GdSceneSynchronizer() {
	scene_synchronizer.event_sync_started.unbind(event_handler_sync_started);
	event_handler_sync_started = NS::NullPHandler;

	scene_synchronizer.event_sync_paused.unbind(event_handler_sync_paused);
	event_handler_sync_paused = NS::NullPHandler;

	scene_synchronizer.event_peer_status_updated.unbind(event_handler_peer_status_updated);
	event_handler_peer_status_updated = NS::NullPHandler;

	scene_synchronizer.event_state_validated.unbind(event_handler_state_validated);
	event_handler_state_validated = NS::NullPHandler;

	scene_synchronizer.event_rewind_frame_begin.unbind(event_handler_rewind_frame_begin);
	event_handler_rewind_frame_begin = NS::NullPHandler;

	scene_synchronizer.event_desync_detected_with_info.unbind(event_handler_desync_detected);
	event_handler_desync_detected = NS::NullPHandler;
}

void GdSceneSynchronizer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}

			if (low_level_peer != get_multiplayer()->get_multiplayer_peer().ptr()) {
				// The low level peer changed, so we need to refresh the synchronizer.
				reset_synchronizer_mode();
			}

			const double delta = get_process_delta_time();
			scene_synchronizer.process(delta);
		} break;
		case NOTIFICATION_ENTER_TREE: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}

			scene_synchronizer.get_network_interface().owner = this;
			scene_synchronizer.setup(*this);

			get_tree()->connect(SNAME("node_removed"), Callable(this, SNAME("unregister_node")));

		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}

			scene_synchronizer.conclude();

			get_tree()->disconnect(SNAME("node_removed"), Callable(this, SNAME("unregister_node")));
		}
	}
}

void GdSceneSynchronizer::on_init_synchronizer(bool p_was_generating_ids) {
	// Always runs the SceneSynchronizer last.
	const int lowest_priority_number = INT32_MIN;
	set_process_priority(lowest_priority_number);
	set_process_internal(true);
	low_level_peer = get_multiplayer()->get_multiplayer_peer().ptr();

	std::string debugger_mode;
	if (scene_synchronizer.is_server()) {
		debugger_mode = "server";
	} else if (scene_synchronizer.is_client()) {
		debugger_mode = "client";
	} else if (scene_synchronizer.is_no_network()) {
		debugger_mode = "nonet";
	}
	SceneSynchronizerDebugger::singleton()->setup_debugger(debugger_mode, 0, get_tree());

	// Setup the debugger log level.
	const int log_level = GLOBAL_GET("NetworkSynchronizer/log_level");
	if (log_level == 0) {
		SceneSynchronizerDebugger::singleton()->set_log_level(NS::INFO);
	} else if (log_level == 1) {
		SceneSynchronizerDebugger::singleton()->set_log_level(NS::WARNING);
	} else {
		SceneSynchronizerDebugger::singleton()->set_log_level(NS::ERROR);
	}
}

void GdSceneSynchronizer::on_uninit_synchronizer() {
	set_physics_process_internal(false);
	low_level_peer = nullptr;
}

void GdSceneSynchronizer::on_add_object_data(NS::ObjectData &p_object_data) {
	//SceneSynchronizerDebugger::singleton()->register_class_for_node_to_dump(SyncClass::from_handle(p_object_data.app_object_handle));
}

#ifdef DEBUG_ENABLED
void GdSceneSynchronizer::debug_only_validate_objects() {
	LocalVector<NS::ObjectHandle> null_objects;
	null_objects.reserve(scene_synchronizer.get_all_object_data().size());

	for (uint32_t i = 0; i < scene_synchronizer.get_all_object_data().size(); i += 1) {
		const NS::ObjectData *nd = scene_synchronizer.get_all_object_data()[i];
		if (nd) {
			if (ObjectDB::get_instance(ObjectID(nd->instance_id)) == nullptr) {
				// Mark for removal.
				null_objects.push_back(nd->app_object_handle);
			}
		}
	}

	// Removes the invalidated `NodeData`.
	if (null_objects.size()) {
		SceneSynchronizerDebugger::singleton()->print(NS::ERROR, "At least one node has been removed from the tree without the SceneSynchronizer noticing. This shouldn't happen.", scene_synchronizer.get_network_interface().get_owner_name());
		for (uint32_t i = 0; i < null_objects.size(); i += 1) {
			scene_synchronizer.on_app_object_removed(null_objects[i]);
		}
	}
}
#endif

void GdSceneSynchronizer::update_objects_relevancy() {
	if (GDVIRTUAL_IS_OVERRIDDEN(_update_nodes_relevancy)) {
		const bool executed = GDVIRTUAL_CALL(_update_nodes_relevancy);
		if (executed == false) {
			SceneSynchronizerDebugger::singleton()->print(NS::ERROR, "The function _update_nodes_relevancy failed!");
		}
	}
}

NS::ObjectHandle GdSceneSynchronizer::fetch_app_object(const std::string &p_object_name) {
	if (get_tree() && get_tree()->get_root()) {
		return scene_synchronizer.to_handle(get_tree()->get_root()->get_node_or_null(NodePath(p_object_name.c_str())));
	}
	return NS::ObjectHandle::NONE;
}

uint64_t GdSceneSynchronizer::get_object_id(NS::ObjectHandle p_app_object_handle) const {
	return scene_synchronizer.from_handle(p_app_object_handle)->get_instance_id();
}

std::string GdSceneSynchronizer::get_object_name(NS::ObjectHandle p_app_object_handle) const {
	return std::string(String(scene_synchronizer.from_handle(p_app_object_handle)->get_path()).utf8());
}

void GdSceneSynchronizer::setup_synchronizer_for(NS::ObjectHandle p_app_object_handle, NS::ObjectLocalId p_id) {
	Node *node = scene_synchronizer.from_handle(p_app_object_handle);
	if (node->has_method("_setup_synchronizer")) {
		node->call("_setup_synchronizer", p_id.id);
	} else {
		SceneSynchronizerDebugger::singleton()->print(NS::ERROR, "The registered node `" + FROM_GSTRING(String(node->get_path())) + "` doesn't override the method `_setup_synchronizer`, which is called by the SceneSynchronizer to know the node sync properties. Pleaes implement it.");
	}
}

void GdSceneSynchronizer::set_variable(NS::ObjectHandle p_app_object_handle, const char *p_name, const NS::VarData &p_val) {
	Node *node = scene_synchronizer.from_handle(p_app_object_handle);
	Variant v;
	GdSceneSynchronizer::convert(v, p_val);
	node->set(StringName(p_name), v);
}

bool GdSceneSynchronizer::get_variable(NS::ObjectHandle p_app_object_handle, const char *p_name, NS::VarData &r_val) const {
#ifdef NS_PROFILING_ENABLED
	std::string info = std::string("Var name: ") + p_name;
	NS_PROFILE_WITH_INFO(info);
#endif

	const Node *node = scene_synchronizer.from_handle(p_app_object_handle);
	bool valid = false;
	Variant val = node->get(StringName(p_name), &valid);
	if (valid) {
		GdSceneSynchronizer::convert(r_val, val);
	}
	return valid;
}

void GdSceneSynchronizer::set_netstats_update_interval_sec(float p_delay) {
	scene_synchronizer.set_netstats_update_interval_sec(p_delay);
}

float GdSceneSynchronizer::get_netstats_update_interval_sec() const {
	return scene_synchronizer.get_netstats_update_interval_sec();
}

void GdSceneSynchronizer::set_max_fps_acceleration_percentage(double p_acceleration) {
	scene_synchronizer.set_max_fps_acceleration_percentage(p_acceleration);
}

double GdSceneSynchronizer::get_max_fps_acceleration_percentage() const {
	return scene_synchronizer.get_max_fps_acceleration_percentage();
}

void GdSceneSynchronizer::set_max_trickled_nodes_per_update(int p_rate) {
	scene_synchronizer.set_max_trickled_objects_per_update(p_rate);
}

int GdSceneSynchronizer::get_max_trickled_nodes_per_update() const {
	return scene_synchronizer.get_max_trickled_objects_per_update();
}

void GdSceneSynchronizer::set_frame_confirmation_timespan(real_t p_interval) {
	scene_synchronizer.set_frame_confirmation_timespan(p_interval);
}

real_t GdSceneSynchronizer::get_frame_confirmation_timespan() const {
	return scene_synchronizer.get_frame_confirmation_timespan();
}

void GdSceneSynchronizer::set_nodes_relevancy_update_time(real_t p_time) {
	scene_synchronizer.set_objects_relevancy_update_time(p_time);
}

real_t GdSceneSynchronizer::get_nodes_relevancy_update_time() const {
	return scene_synchronizer.get_objects_relevancy_update_time();
}

void GdSceneSynchronizer::set_frames_per_seconds(int p_fps) {
	scene_synchronizer.set_frames_per_seconds(p_fps);
}

int GdSceneSynchronizer::get_frames_per_seconds() const {
	return scene_synchronizer.get_frames_per_seconds();
}

void GdSceneSynchronizer::_rpc_net_sync_reliable(const Vector<uint8_t> &p_args) {
	static_cast<GdNetworkInterface *>(&scene_synchronizer.get_network_interface())->gd_rpc_receive(p_args);
}

void GdSceneSynchronizer::_rpc_net_sync_unreliable(const Vector<uint8_t> &p_args) {
	static_cast<GdNetworkInterface *>(&scene_synchronizer.get_network_interface())->gd_rpc_receive(p_args);
}

void GdSceneSynchronizer::reset_synchronizer_mode() {
	scene_synchronizer.set_debug_rewindings_enabled(ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/log_debug_rewindings"));
	scene_synchronizer.set_debug_server_speedup(ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/debug_server_speedup"));
	scene_synchronizer.set_debug_log_nodes_relevancy_update(ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/log_debug_nodes_relevancy_update"));
	scene_synchronizer.reset_synchronizer_mode();
}

void GdSceneSynchronizer::clear() {
	scene_synchronizer.clear();
}

NS::ObjectLocalId GdSceneSynchronizer::register_node(Node *p_node) {
	NS::ObjectLocalId id;
	scene_synchronizer.register_app_object(scene_synchronizer.to_handle(p_node), &id);
	return id;
}

uint32_t GdSceneSynchronizer::register_node_gdscript(Node *p_node) {
	const NS::ObjectLocalId id = register_node(p_node);
	return id.id;
}

void GdSceneSynchronizer::setup_controller(
		Node *p_node,
		int p_peer,
		const Callable &p_collect_input_func,
		const Callable &p_count_input_size_func,
		const Callable &p_are_inputs_different_func,
		const Callable &p_process_func) {
	scene_synchronizer.setup_controller(
			scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(p_node)),
			p_peer,
			[p_collect_input_func](double p_delta, NS::DataBuffer &r_collect_input) -> void {
				Array arguments;
				arguments.push_back(p_delta);

				GdDataBuffer *gd_db = memnew(GdDataBuffer);
				gd_db->data_buffer = &r_collect_input;
				arguments.push_back(gd_db);

				p_collect_input_func.callv(arguments);

				memdelete(gd_db);
			},
			[p_count_input_size_func](NS::DataBuffer &p_collect_input) -> int {
				Array arguments;

				GdDataBuffer *gd_db = memnew(GdDataBuffer);
				gd_db->data_buffer = &p_collect_input;
				arguments.push_back(gd_db);

				auto r = p_count_input_size_func.callv(arguments);

				memdelete(gd_db);
				return r;
			},
			[p_are_inputs_different_func](NS::DataBuffer &p_collect_input_A, NS::DataBuffer &p_collect_input_B) -> bool {
				Array arguments;

				GdDataBuffer *gd_db_A = memnew(GdDataBuffer);
				GdDataBuffer *gd_db_B = memnew(GdDataBuffer);
				gd_db_A->data_buffer = &p_collect_input_A;
				gd_db_B->data_buffer = &p_collect_input_B;

				arguments.push_back(gd_db_A);
				arguments.push_back(gd_db_B);

				auto r = p_are_inputs_different_func.callv(arguments);

				memdelete(gd_db_A);
				memdelete(gd_db_B);
				return r;
			},
			[p_process_func](double p_delta, NS::DataBuffer &p_collect_input) -> void {
				Array arguments;
				arguments.push_back(p_delta);

				GdDataBuffer *gd_db = memnew(GdDataBuffer);
				gd_db->data_buffer = &p_collect_input;
				arguments.push_back(gd_db);

				p_process_func.callv(arguments);

				memdelete(gd_db);
			});
}

void GdSceneSynchronizer::unregister_node(Node *p_node) {
	scene_synchronizer.unregister_app_object(scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(p_node)));
}

uint32_t GdSceneSynchronizer::get_node_id(Node *p_node) {
	return scene_synchronizer.get_app_object_net_id(scene_synchronizer.to_handle(p_node)).id;
}

Node *GdSceneSynchronizer::get_node_from_id(uint32_t p_id, bool p_expected) {
	return SyncClass::from_handle(scene_synchronizer.get_app_object_from_id(NS::ObjectNetId{ { p_id } }, p_expected));
}

const Node *GdSceneSynchronizer::get_node_from_id_const(uint32_t p_id, bool p_expected) const {
	return SyncClass::from_handle(scene_synchronizer.get_app_object_from_id_const(NS::ObjectNetId{ { p_id } }, p_expected));
}

void GdSceneSynchronizer::register_variable(Node *p_node, const StringName &p_variable) {
	scene_synchronizer.register_variable(scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(p_node)), std::string(String(p_variable).utf8()));
}

void GdSceneSynchronizer::unregister_variable(Node *p_node, const StringName &p_variable) {
	scene_synchronizer.unregister_variable(scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(p_node)), std::string(String(p_variable).utf8()));
}

uint32_t GdSceneSynchronizer::get_variable_id(Node *p_node, const StringName &p_variable) {
	NS::ObjectLocalId id = scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(p_node));
	if (id != NS::ObjectLocalId::NONE) {
		return scene_synchronizer.get_variable_id(id, std::string(String(p_variable).utf8())).id;
	}
	return NS::VarId::NONE.id;
}

void GdSceneSynchronizer::set_skip_rewinding(Node *p_node, const StringName &p_variable, bool p_skip_rewinding) {
	NS::ObjectLocalId id = scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(p_node));
	if (id != NS::ObjectLocalId::NONE) {
		scene_synchronizer.set_skip_rewinding(id, std::string(String(p_variable).utf8()), p_skip_rewinding);
	}
}

uint64_t GdSceneSynchronizer::track_variable_changes(
		Array p_nodes,
		Array p_vars,
		const Callable &p_callable,
		NetEventFlag p_flags) {
	ERR_FAIL_COND_V(p_nodes.size() != p_vars.size(), 0);
	ERR_FAIL_COND_V(p_nodes.size() == 0, 0);

	std::vector<NS::ObjectLocalId> objects_ids;
	std::vector<std::string> var_names;

	for (int i = 0; i < int(p_nodes.size()); i++) {
		Object *obj = p_nodes[i];
		Node *node = dynamic_cast<Node *>(obj);
		NS::ObjectLocalId lid = scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(node));
		objects_ids.push_back(lid);
		var_names.push_back(std::string(String(p_vars[i]).utf8()));
	}

	NS::ListenerHandle raw_handle =
			scene_synchronizer.track_variables_changes(
					objects_ids,
					var_names,
					[p_callable](const std::vector<NS::VarData> &p_old_variables) {
						Array arguments;
						for (const auto &vd : p_old_variables) {
							Variant v;
							GdSceneSynchronizer::convert(v, vd);
							arguments.push_back(v);
						}
						p_callable.callv(arguments);
					},
					p_flags);

	return static_cast<uint64_t>(raw_handle.id);
}

void GdSceneSynchronizer::untrack_variable_changes(uint64_t p_handle) {
	scene_synchronizer.untrack_variable_changes({ static_cast<std::intptr_t>(p_handle) });
}

uint64_t GdSceneSynchronizer::register_process(Node *p_node, ProcessPhase p_phase, const Callable &p_func) {
	NS::ObjectLocalId id;
	scene_synchronizer.register_app_object(scene_synchronizer.to_handle(p_node), &id);
	const NS::PHandler EFH = scene_synchronizer.register_process(id, p_phase, [p_func](float p_delta) {
		Array a;
		a.push_back(p_delta);
		p_func.callv(a);
	});
	return static_cast<uint64_t>(EFH);
}

void GdSceneSynchronizer::unregister_process(Node *p_node, ProcessPhase p_phase, uint64_t p_handler) {
	NS::ObjectLocalId id = scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(p_node));

	scene_synchronizer.unregister_process(
			id,
			p_phase,
			static_cast<NS::PHandler>(p_handler));
}

void GdSceneSynchronizer::setup_simulated_sync(
		Node *p_node,
		const Callable &p_collect,
		const Callable &p_get_size,
		const Callable &p_are_equals,
		const Callable &p_process) {
}

void GdSceneSynchronizer::setup_trickled_sync(Node *p_node, const Callable &p_collect_epoch_func, const Callable &p_apply_epoch_func) {
	scene_synchronizer.setup_trickled_sync(
			scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(p_node)),
			[p_collect_epoch_func](NS::DataBuffer &db, float p_update_rate) -> void {
				Array a;

				GdDataBuffer *gd_db = memnew(GdDataBuffer);
				gd_db->data_buffer = &db;
				a.push_back(gd_db);

				a.push_back(p_update_rate);
				p_collect_epoch_func.callv(a);

				memdelete(gd_db);
			},
			[p_apply_epoch_func](double delta, float alpha, NS::DataBuffer &db_from, NS::DataBuffer &db_to) -> void {
				Array a;
				a.push_back(delta);
				a.push_back(alpha);

				GdDataBuffer *gd_db_from = memnew(GdDataBuffer);
				gd_db_from->data_buffer = &db_from;
				a.push_back(gd_db_from);

				GdDataBuffer *gd_db_to = memnew(GdDataBuffer);
				gd_db_to->data_buffer = &db_to;
				a.push_back(gd_db_to);

				p_apply_epoch_func.callv(a);

				memdelete(gd_db_from);
				memdelete(gd_db_to);
			});
}

int GdSceneSynchronizer::get_peer_latency_ms(int p_peer) const {
	return scene_synchronizer.get_peer_latency_ms(p_peer);
}

int GdSceneSynchronizer::get_peer_latency_jitter_ms(int p_peer) const {
	return scene_synchronizer.get_peer_latency_jitter_ms(p_peer);
}

float GdSceneSynchronizer::get_peer_packet_loss_percentage(int p_peer) const {
	return scene_synchronizer.get_peer_packet_loss_percentage(p_peer);
}

bool GdSceneSynchronizer::client_is_object_simulating(Node *p_node) const {
	return client_is_object_simulating(scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(p_node)));
}

bool GdSceneSynchronizer::client_is_object_simulating(NS::ObjectLocalId p_id) const {
	return scene_synchronizer.client_is_simulated_object(p_id);
}

bool GdSceneSynchronizer::client_is_object_simulating(NS::ObjectNetId p_id) const {
	const NS::ObjectData *od = scene_synchronizer.get_object_data(p_id);
	ENSURE_V(od, false);
	return client_is_object_simulating(od->get_net_id());
}

Array GdSceneSynchronizer::local_controller_get_controlled_nodes() const {
	Array a;

	const std::vector<NS::ObjectData *> *objects = scene_synchronizer.get_peer_controlled_objects_data(scene_synchronizer.get_network_interface().fetch_local_peer_id());
	if (objects) {
		for (auto object : *objects) {
			Node *n = scene_synchronizer.from_handle(object->app_object_handle);
			a.push_back(n);
		}
	}

	return a;
}

uint32_t GdSceneSynchronizer::sync_group_create() {
	return scene_synchronizer.sync_group_create().id;
}

const NS::SyncGroup *GdSceneSynchronizer::sync_group_get(uint32_t p_group_id) const {
	return scene_synchronizer.sync_group_get(NS::SyncGroupId{ { p_group_id } });
}

void GdSceneSynchronizer::sync_group_add_node_by_id(uint32_t p_net_id, uint32_t p_group_id, bool p_realtime) {
	scene_synchronizer.sync_group_add_object(NS::ObjectNetId{ { p_net_id } }, NS::SyncGroupId{ { p_group_id } }, p_realtime);
}

void GdSceneSynchronizer::sync_group_add_node(NS::ObjectData *p_object_data, uint32_t p_group_id, bool p_realtime) {
	scene_synchronizer.sync_group_add_object(p_object_data, NS::SyncGroupId{ { p_group_id } }, p_realtime);
}

void GdSceneSynchronizer::sync_group_remove_node_by_id(uint32_t p_net_id, uint32_t p_group_id) {
	scene_synchronizer.sync_group_remove_object(NS::ObjectNetId{ { p_net_id } }, NS::SyncGroupId{ { p_group_id } });
}

void GdSceneSynchronizer::sync_group_remove_node(NS::ObjectData *p_object_data, uint32_t p_group_id) {
	scene_synchronizer.sync_group_remove_object(p_object_data, NS::SyncGroupId{ { p_group_id } });
}

void GdSceneSynchronizer::sync_group_replace_nodes(uint32_t p_group_id, std::vector<NS::SyncGroup::SimulatedObjectInfo> &&p_new_realtime_nodes, std::vector<NS::SyncGroup::TrickledObjectInfo> &&p_new_trickled_nodes) {
	scene_synchronizer.sync_group_replace_objects(NS::SyncGroupId{ { p_group_id } }, std::move(p_new_realtime_nodes), std::move(p_new_trickled_nodes));
}

void GdSceneSynchronizer::sync_group_remove_all_nodes(uint32_t p_group_id) {
	scene_synchronizer.sync_group_remove_all_objects(NS::SyncGroupId{ { p_group_id } });
}

void GdSceneSynchronizer::sync_group_move_peer_to(int p_peer_id, uint32_t p_group_id) {
	scene_synchronizer.sync_group_move_peer_to(p_peer_id, NS::SyncGroupId{ { p_group_id } });
}

uint32_t GdSceneSynchronizer::sync_group_get_peer_group(int p_peer_id) const {
	return scene_synchronizer.sync_group_get_peer_group(p_peer_id).id;
}

const std::vector<int> *GdSceneSynchronizer::sync_group_get_listening_peers(uint32_t p_group_id) const {
	return scene_synchronizer.sync_group_get_listening_peers(NS::SyncGroupId{ { p_group_id } });
}

void GdSceneSynchronizer::sync_group_set_trickled_update_rate_by_id(uint32_t p_net_id, uint32_t p_group_id, real_t p_update_rate) {
	scene_synchronizer.sync_group_set_trickled_update_rate(NS::ObjectNetId{ { p_net_id } }, NS::SyncGroupId{ { p_group_id } }, p_update_rate);
}

void GdSceneSynchronizer::sync_group_set_trickled_update_rate(NS::ObjectData *p_object_data, uint32_t p_group_id, real_t p_update_rate) {
	scene_synchronizer.sync_group_set_trickled_update_rate(p_object_data->get_local_id(), NS::SyncGroupId{ { p_group_id } }, p_update_rate);
}

real_t GdSceneSynchronizer::sync_group_get_trickled_update_rate_by_id(uint32_t p_net_id, uint32_t p_group_id) const {
	return scene_synchronizer.sync_group_get_trickled_update_rate(NS::ObjectNetId{ { p_net_id } }, NS::SyncGroupId{ { p_group_id } });
}

real_t GdSceneSynchronizer::sync_group_get_trickled_update_rate(const NS::ObjectData *p_object_data, uint32_t p_group_id) const {
	return scene_synchronizer.sync_group_get_trickled_update_rate(p_object_data->get_local_id(), NS::SyncGroupId{ { p_group_id } });
}

void GdSceneSynchronizer::sync_group_set_user_data(uint32_t p_group_id, uint64_t p_user_data) {
	scene_synchronizer.sync_group_set_user_data(NS::SyncGroupId{ { p_group_id } }, p_user_data);
}

uint64_t GdSceneSynchronizer::sync_group_get_user_data(uint32_t p_group_id) const {
	return scene_synchronizer.sync_group_get_user_data(NS::SyncGroupId{ { p_group_id } });
}

bool GdSceneSynchronizer::is_recovered() const {
	return scene_synchronizer.is_recovered();
}

bool GdSceneSynchronizer::is_resetted() const {
	return scene_synchronizer.is_resetted();
}

bool GdSceneSynchronizer::is_rewinding() const {
	return scene_synchronizer.is_rewinding();
}

bool GdSceneSynchronizer::is_end_sync() const {
	return scene_synchronizer.is_end_sync();
}

void GdSceneSynchronizer::force_state_notify(uint32_t p_sync_group_id) {
	scene_synchronizer.force_state_notify(NS::SyncGroupId{ { p_sync_group_id } });
}

void GdSceneSynchronizer::force_state_notify_all() {
	scene_synchronizer.force_state_notify_all();
}

void GdSceneSynchronizer::set_enabled(bool p_enable) {
	scene_synchronizer.set_enabled(p_enable);
}

void GdSceneSynchronizer::set_peer_networking_enable(int p_peer, bool p_enable) {
	scene_synchronizer.set_peer_networking_enable(p_peer, p_enable);
}

bool GdSceneSynchronizer::is_peer_networking_enabled(int p_peer) const {
	return scene_synchronizer.is_peer_networking_enabled(p_peer);
}

bool GdSceneSynchronizer::is_server() const {
	return scene_synchronizer.is_server();
}

bool GdSceneSynchronizer::is_client() const {
	return scene_synchronizer.is_client();
}

bool GdSceneSynchronizer::is_no_network() const {
	return scene_synchronizer.is_no_network();
}

bool GdSceneSynchronizer::is_networked() const {
	return scene_synchronizer.is_networked();
}

void GdSceneSynchronizer::encode(NS::DataBuffer &r_buffer, const NS::VarData &p_val) {
	Variant vA;
	convert(vA, p_val);

	GdDataBuffer *gd_db = memnew(GdDataBuffer);
	gd_db->data_buffer = &r_buffer;
	gd_db->add_variant(vA);
	memdelete(gd_db);
}

void GdSceneSynchronizer::decode(NS::VarData &r_val, NS::DataBuffer &p_buffer) {
	GdDataBuffer *gd_db = memnew(GdDataBuffer);
	gd_db->data_buffer = &p_buffer;

	Variant vA = gd_db->read_variant();
	convert(r_val, vA);

	memdelete(gd_db);
}

#define CONVERT_VARDATA(CLAZZ, variant, vardata)           \
	CLAZZ v;                                               \
	std::memcpy((void *)&v, &vardata.data.ptr, sizeof(v)); \
	variant = v;

void GdSceneSynchronizer::convert(Variant &r_variant, const NS::VarData &p_vd) {
	const Variant::Type t = static_cast<Variant::Type>(p_vd.type);
	switch (t) {
		case Variant::NIL: {
			r_variant = Variant();
		} break;
		case Variant::BOOL: {
			CONVERT_VARDATA(bool, r_variant, p_vd);
		} break;
		case Variant::INT: {
			CONVERT_VARDATA(std::int64_t, r_variant, p_vd);
		} break;
		case Variant::FLOAT: {
			CONVERT_VARDATA(double, r_variant, p_vd);
		} break;
		case Variant::VECTOR2: {
			CONVERT_VARDATA(Vector2, r_variant, p_vd);
		} break;
		case Variant::VECTOR2I: {
			CONVERT_VARDATA(Vector2i, r_variant, p_vd);
		} break;
		case Variant::RECT2: {
			CONVERT_VARDATA(Rect2, r_variant, p_vd);
		} break;
		case Variant::RECT2I: {
			CONVERT_VARDATA(Rect2i, r_variant, p_vd);
		} break;
		case Variant::VECTOR3: {
			CONVERT_VARDATA(Vector3, r_variant, p_vd);
		} break;
		case Variant::VECTOR3I: {
			CONVERT_VARDATA(Vector3i, r_variant, p_vd);
		} break;
		case Variant::TRANSFORM2D: {
			CONVERT_VARDATA(Transform2D, r_variant, p_vd);
		} break;
		case Variant::VECTOR4: {
			CONVERT_VARDATA(Vector4, r_variant, p_vd);
		} break;
		case Variant::VECTOR4I: {
			CONVERT_VARDATA(Vector4i, r_variant, p_vd);
		} break;
		case Variant::PLANE: {
			CONVERT_VARDATA(Plane, r_variant, p_vd);
		} break;
		case Variant::QUATERNION: {
			CONVERT_VARDATA(Quaternion, r_variant, p_vd);
		} break;
		case Variant::AABB: {
			CONVERT_VARDATA(AABB, r_variant, p_vd);
		} break;
		case Variant::BASIS: {
			CONVERT_VARDATA(Basis, r_variant, p_vd);
		} break;
		case Variant::TRANSFORM3D: {
			CONVERT_VARDATA(Transform3D, r_variant, p_vd);
		} break;
		case Variant::PROJECTION: {
			CONVERT_VARDATA(Projection, r_variant, p_vd);
		} break;
		case Variant::COLOR: {
			CONVERT_VARDATA(Color, r_variant, p_vd);
		} break;

		case Variant::STRING_NAME:
		case Variant::NODE_PATH:
		case Variant::STRING:
		case Variant::DICTIONARY:
		case Variant::ARRAY:
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY: {
			if (p_vd.shared_buffer) {
				r_variant = *std::static_pointer_cast<Variant>(p_vd.shared_buffer);
			}
		} break;

		default:
			ERR_PRINT("This VarDta can't be converted to a Variant. Type not supported: " + itos(p_vd.type));
			r_variant = Variant();
	}
}

#undef CONVERT_VARDATA
#define CONVERT_VARDATA(CLAZZ, variant, vardata) \
	const CLAZZ v = variant;                     \
	std::memcpy(&vardata.data.ptr, &v, sizeof(v));

void GdSceneSynchronizer::convert(NS::VarData &r_vd, const Variant &p_variant) {
#ifdef NS_PROFILING_ENABLED
	std::string info = std::string("Var type: ") + Variant::get_type_name(p_variant.get_type()).utf8().ptr();
	NS_PROFILE_WITH_INFO(info)
#endif

	r_vd.type = static_cast<std::uint8_t>(p_variant.get_type());
	switch (p_variant.get_type()) {
		case Variant::NIL: {
			r_vd.data.ptr = nullptr;
		} break;
		case Variant::BOOL: {
			CONVERT_VARDATA(bool, p_variant, r_vd);
		} break;
		case Variant::INT: {
			CONVERT_VARDATA(std::int64_t, p_variant, r_vd);
		} break;
		case Variant::FLOAT: {
			CONVERT_VARDATA(double, p_variant, r_vd);
		} break;
		case Variant::VECTOR2: {
			CONVERT_VARDATA(Vector2, p_variant, r_vd);
		} break;
		case Variant::VECTOR2I: {
			CONVERT_VARDATA(Vector2i, p_variant, r_vd);
		} break;
		case Variant::RECT2: {
			CONVERT_VARDATA(Rect2, p_variant, r_vd);
		} break;
		case Variant::RECT2I: {
			CONVERT_VARDATA(Rect2i, p_variant, r_vd);
		} break;
		case Variant::VECTOR3: {
			CONVERT_VARDATA(Vector3, p_variant, r_vd);
		} break;
		case Variant::VECTOR3I: {
			CONVERT_VARDATA(Vector3i, p_variant, r_vd);
		} break;
		case Variant::TRANSFORM2D: {
			CONVERT_VARDATA(Transform2D, p_variant, r_vd);
		} break;
		case Variant::VECTOR4: {
			CONVERT_VARDATA(Vector4, p_variant, r_vd);
		} break;
		case Variant::VECTOR4I: {
			CONVERT_VARDATA(Vector4i, p_variant, r_vd);
		} break;
		case Variant::PLANE: {
			CONVERT_VARDATA(Plane, p_variant, r_vd);
		} break;
		case Variant::QUATERNION: {
			CONVERT_VARDATA(Quaternion, p_variant, r_vd);
		} break;
		case Variant::AABB: {
			CONVERT_VARDATA(AABB, p_variant, r_vd);
		} break;
		case Variant::BASIS: {
			CONVERT_VARDATA(Basis, p_variant, r_vd);
		} break;
		case Variant::TRANSFORM3D: {
			CONVERT_VARDATA(Transform3D, p_variant, r_vd);
		} break;
		case Variant::PROJECTION: {
			CONVERT_VARDATA(Projection, p_variant, r_vd);
		} break;
		case Variant::COLOR: {
			CONVERT_VARDATA(Color, p_variant, r_vd);
		} break;

		case Variant::STRING_NAME:
		case Variant::NODE_PATH:
		case Variant::STRING:
		case Variant::DICTIONARY:
		case Variant::ARRAY:
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY: {
			r_vd.shared_buffer = std::make_shared<Variant>(p_variant.duplicate(true));
		} break;

		default:
			ERR_PRINT("This variant can't be converted: " + p_variant.stringify());
			r_vd.type = Variant::VARIANT_MAX;
	}
}

#undef CONVERT_VARDATA

bool GdSceneSynchronizer::compare(const NS::VarData &p_A, const NS::VarData &p_B) {
	Variant vA;
	Variant vB;
	convert(vA, p_A);
	convert(vB, p_B);
	return vA == vB;
}

// This was needed to optimize the godot stringify for byte arrays.. it was slowing down perfs.
std::string stringify_byte_array_fast(const Vector<uint8_t> &p_array, bool p_verbose) {
	std::string str;
	if (!p_verbose) {
		str = "Bytes (" + std::to_string(p_array.size()) + ") ";
	} else {
		// At the moment printing the bytes is way to heavy. Need to find a better way.
		// NOTE: std::to_string() is faster than itos().
		str.reserve((p_array.size() * 7) + 50);
		str.append("Bytes (" + std::to_string(p_array.size()) + "): ");

		const uint8_t *array_ptr = p_array.ptr();
		for (int i = 0; i < p_array.size(); i++) {
			str.append(std::to_string(array_ptr[i]));
			str.append(", ");
		}
	}
	return str;
}

std::string GdSceneSynchronizer::stringify(const NS::VarData &p_var_data, bool p_verbose) {
	Variant v;
	convert(v, p_var_data);
	if (v.get_type() == Variant::PACKED_BYTE_ARRAY) {
		return stringify_byte_array_fast(v, p_verbose);
	} else {
		return std::string(v.stringify().utf8());
	}
}
