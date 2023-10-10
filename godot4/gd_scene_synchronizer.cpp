#include "gd_scene_synchronizer.h"

#include "core/object/object.h"
#include "core/string/string_name.h"
#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/core/processor.h"
#include "modules/network_synchronizer/data_buffer.h"
#include "modules/network_synchronizer/godot4/gd_network_interface.h"
#include "modules/network_synchronizer/godot4/gd_networked_controller.h"
#include "modules/network_synchronizer/net_utilities.h"
#include "modules/network_synchronizer/scene_synchronizer.h"
#include "modules/network_synchronizer/scene_synchronizer_debugger.h"
#include "modules/network_synchronizer/snapshot.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/node.h"
#include "scene/main/window.h"
#include <vector>

void GdSceneSynchronizer::_bind_methods() {
	BIND_CONSTANT(NS::SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID)

	BIND_ENUM_CONSTANT(CHANGE)
	BIND_ENUM_CONSTANT(SYNC_RECOVER)
	BIND_ENUM_CONSTANT(SYNC_RESET)
	BIND_ENUM_CONSTANT(SYNC_REWIND)
	BIND_ENUM_CONSTANT(END_SYNC)
	BIND_ENUM_CONSTANT(DEFAULT)
	BIND_ENUM_CONSTANT(SYNC)
	BIND_ENUM_CONSTANT(ALWAYS)

	BIND_ENUM_CONSTANT(PROCESSPHASE_EARLY)
	BIND_ENUM_CONSTANT(PROCESSPHASE_PRE)
	BIND_ENUM_CONSTANT(PROCESSPHASE_PROCESS)
	BIND_ENUM_CONSTANT(PROCESSPHASE_POST)
	BIND_ENUM_CONSTANT(PROCESSPHASE_LATE)

	ClassDB::bind_method(D_METHOD("reset_synchronizer_mode"), &GdSceneSynchronizer::reset_synchronizer_mode);
	ClassDB::bind_method(D_METHOD("clear"), &GdSceneSynchronizer::clear);

	ClassDB::bind_method(D_METHOD("set_max_deferred_nodes_per_update", "rate"), &GdSceneSynchronizer::set_max_deferred_nodes_per_update);
	ClassDB::bind_method(D_METHOD("get_max_deferred_nodes_per_update"), &GdSceneSynchronizer::get_max_deferred_nodes_per_update);

	ClassDB::bind_method(D_METHOD("set_server_notify_state_interval", "interval"), &GdSceneSynchronizer::set_server_notify_state_interval);
	ClassDB::bind_method(D_METHOD("get_server_notify_state_interval"), &GdSceneSynchronizer::get_server_notify_state_interval);

	ClassDB::bind_method(D_METHOD("set_comparison_float_tolerance", "tolerance"), &GdSceneSynchronizer::set_comparison_float_tolerance);
	ClassDB::bind_method(D_METHOD("get_comparison_float_tolerance"), &GdSceneSynchronizer::get_comparison_float_tolerance);

	ClassDB::bind_method(D_METHOD("set_nodes_relevancy_update_time", "time"), &GdSceneSynchronizer::set_nodes_relevancy_update_time);
	ClassDB::bind_method(D_METHOD("get_nodes_relevancy_update_time"), &GdSceneSynchronizer::get_nodes_relevancy_update_time);

	ClassDB::bind_method(D_METHOD("register_node", "node"), &GdSceneSynchronizer::register_node_gdscript);
	ClassDB::bind_method(D_METHOD("unregister_node", "node"), &GdSceneSynchronizer::unregister_node);
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

	ClassDB::bind_method(D_METHOD("setup_deferred_sync", "node", "collect_epoch_func", "apply_epoch_func"), &GdSceneSynchronizer::setup_deferred_sync);

	ClassDB::bind_method(D_METHOD("sync_group_create"), &GdSceneSynchronizer::sync_group_create);
	ClassDB::bind_method(D_METHOD("sync_group_add_node", "node_id", "group_id", "realtime"), &GdSceneSynchronizer::sync_group_add_node_by_id);
	ClassDB::bind_method(D_METHOD("sync_group_remove_node", "node_id", "group_id"), &GdSceneSynchronizer::sync_group_remove_node_by_id);
	ClassDB::bind_method(D_METHOD("sync_group_move_peer_to", "peer_id", "group_id"), &GdSceneSynchronizer::sync_group_move_peer_to);
	ClassDB::bind_method(D_METHOD("sync_group_set_deferred_update_rate", "node_id", "group_id", "update_rate"), &GdSceneSynchronizer::sync_group_set_deferred_update_rate_by_id);
	ClassDB::bind_method(D_METHOD("sync_group_get_deferred_update_rate", "node_id", "group_id"), &GdSceneSynchronizer::sync_group_get_deferred_update_rate_by_id);

	ClassDB::bind_method(D_METHOD("start_tracking_scene_changes", "diff_handle"), &GdSceneSynchronizer::start_tracking_scene_changes);
	ClassDB::bind_method(D_METHOD("stop_tracking_scene_changes", "diff_handle"), &GdSceneSynchronizer::stop_tracking_scene_changes);
	ClassDB::bind_method(D_METHOD("pop_scene_changes", "diff_handle"), &GdSceneSynchronizer::pop_scene_changes);
	ClassDB::bind_method(D_METHOD("apply_scene_changes", "sync_data"), &GdSceneSynchronizer::apply_scene_changes);

	ClassDB::bind_method(D_METHOD("is_recovered"), &GdSceneSynchronizer::is_recovered);
	ClassDB::bind_method(D_METHOD("is_resetted"), &GdSceneSynchronizer::is_resetted);
	ClassDB::bind_method(D_METHOD("is_rewinding"), &GdSceneSynchronizer::is_rewinding);
	ClassDB::bind_method(D_METHOD("is_end_sync"), &GdSceneSynchronizer::is_end_sync);

	ClassDB::bind_method(D_METHOD("force_state_notify", "group_id"), &GdSceneSynchronizer::force_state_notify);
	ClassDB::bind_method(D_METHOD("force_state_notify_all"), &GdSceneSynchronizer::force_state_notify_all);

	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &GdSceneSynchronizer::set_enabled);
	ClassDB::bind_method(D_METHOD("set_peer_networking_enable", "peer", "enabled"), &GdSceneSynchronizer::set_peer_networking_enable);
	ClassDB::bind_method(D_METHOD("get_peer_networking_enable", "peer"), &GdSceneSynchronizer::is_peer_networking_enable);

	ClassDB::bind_method(D_METHOD("is_server"), &GdSceneSynchronizer::is_server);
	ClassDB::bind_method(D_METHOD("is_client"), &GdSceneSynchronizer::is_client);
	ClassDB::bind_method(D_METHOD("is_networked"), &GdSceneSynchronizer::is_networked);

	ClassDB::bind_method(D_METHOD("_rpc_net_sync_reliable"), &GdSceneSynchronizer::_rpc_net_sync_reliable);
	ClassDB::bind_method(D_METHOD("_rpc_net_sync_unreliable"), &GdSceneSynchronizer::_rpc_net_sync_unreliable);

	GDVIRTUAL_BIND(_update_nodes_relevancy);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "server_notify_state_interval", PROPERTY_HINT_RANGE, "0.001,10.0,0.0001"), "set_server_notify_state_interval", "get_server_notify_state_interval");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "comparison_float_tolerance", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001"), "set_comparison_float_tolerance", "get_comparison_float_tolerance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "nodes_relevancy_update_time", PROPERTY_HINT_RANGE, "0.0,2.0,0.01"), "set_nodes_relevancy_update_time", "get_nodes_relevancy_update_time");

	ADD_SIGNAL(MethodInfo("sync_started"));
	ADD_SIGNAL(MethodInfo("sync_paused"));
	ADD_SIGNAL(MethodInfo("peer_status_updated", PropertyInfo(Variant::OBJECT, "controlled_node"), PropertyInfo(Variant::INT, "node_data_id"), PropertyInfo(Variant::INT, "peer"), PropertyInfo(Variant::BOOL, "connected"), PropertyInfo(Variant::BOOL, "enabled")));

	ADD_SIGNAL(MethodInfo("state_validated", PropertyInfo(Variant::INT, "input_id")));
	ADD_SIGNAL(MethodInfo("rewind_frame_begin", PropertyInfo(Variant::INT, "input_id"), PropertyInfo(Variant::INT, "index"), PropertyInfo(Variant::INT, "count")));

	ADD_SIGNAL(MethodInfo("desync_detected", PropertyInfo(Variant::INT, "input_id"), PropertyInfo(Variant::OBJECT, "node"), PropertyInfo(Variant::ARRAY, "var_names"), PropertyInfo(Variant::ARRAY, "client_values"), PropertyInfo(Variant::ARRAY, "server_values")));
}

GdSceneSynchronizer::GdSceneSynchronizer() :
		Node() {
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
			scene_synchronizer.event_peer_status_updated.bind([this](const NS::ObjectData *p_object_data, int p_peer, bool p_connected, bool p_enabled) -> void {
				Object *obj_null = nullptr;
				emit_signal(
						"peer_status_updated",
						p_object_data ? GdSceneSynchronizer::SyncClass::from_handle(p_object_data->app_object_handle) : obj_null,
						p_object_data ? p_object_data->get_net_id().id : NS::ObjectNetId::NONE.id,
						p_peer,
						p_connected,
						p_enabled);
			});

	event_handler_state_validated =
			scene_synchronizer.event_state_validated.bind([this](uint32_t p_input_id) -> void {
				emit_signal("state_validated", p_input_id);
			});

	event_handler_rewind_frame_begin =
			scene_synchronizer.event_rewind_frame_begin.bind([this](uint32_t p_input_id, int p_index, int p_count) -> void {
				emit_signal("rewind_frame_begin", p_input_id, p_index, p_count);
			});

	event_handler_desync_detected =
			scene_synchronizer.event_desync_detected.bind([this](uint32_t p_input_id, NS::ObjectHandle p_app_object, const std::vector<std::string> &p_var_names, const std::vector<Variant> &p_client_values, const std::vector<Variant> &p_server_values) -> void {
				Vector<String> var_names;
				Vector<Variant> client_values;
				Vector<Variant> server_values;
				for (auto n : p_var_names) {
					var_names.push_back(String(n.c_str()));
				}
				for (auto v : p_client_values) {
					client_values.push_back(v);
				}
				for (auto v : p_server_values) {
					server_values.push_back(v);
				}
				emit_signal("desync_detected", p_input_id, GdSceneSynchronizer::SyncClass::from_handle(p_app_object), var_names, client_values, server_values);
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

	scene_synchronizer.event_desync_detected.unbind(event_handler_desync_detected);
	event_handler_desync_detected = NS::NullPHandler;
}

void GdSceneSynchronizer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}

			if (low_level_peer != get_multiplayer()->get_multiplayer_peer().ptr()) {
				// The low level peer changed, so we need to refresh the synchronizer.
				reset_synchronizer_mode();
			}

			const int lowest_priority_number = INT32_MAX;
			ERR_FAIL_COND_MSG(get_process_priority() != lowest_priority_number, "The process priority MUST not be changed, it's likely there is a better way of doing what you are trying to do, if you really need it please open an issue.");

			scene_synchronizer.process();
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
	const int lowest_priority_number = INT32_MAX;
	set_process_priority(lowest_priority_number);
	set_physics_process_internal(true);
	low_level_peer = get_multiplayer()->get_multiplayer_peer().ptr();

	String debugger_mode;
	if (scene_synchronizer.is_server()) {
		debugger_mode = "server";
	} else if (scene_synchronizer.is_client()) {
		debugger_mode = "client";
	} else if (scene_synchronizer.is_no_network()) {
		debugger_mode = "nonet";
	}
	SceneSynchronizerDebugger::singleton()->setup_debugger(debugger_mode, 0, get_tree());
}

void GdSceneSynchronizer::on_uninit_synchronizer() {
	set_physics_process_internal(false);
	low_level_peer = nullptr;
}

void GdSceneSynchronizer::on_add_object_data(NS::ObjectData &p_object_data) {
	SceneSynchronizerDebugger::singleton()->register_class_for_node_to_dump(SyncClass::from_handle(p_object_data.app_object_handle));
}

#ifdef DEBUG_ENABLED
void GdSceneSynchronizer::debug_only_validate_nodes() {
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
		SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer.get_network_interface(), "At least one node has been removed from the tree without the SceneSynchronizer noticing. This shouldn't happen.");
		for (uint32_t i = 0; i < null_objects.size(); i += 1) {
			scene_synchronizer.on_app_object_removed(null_objects[i]);
		}
	}
}
#endif

void GdSceneSynchronizer::update_nodes_relevancy() {
	if (GDVIRTUAL_IS_OVERRIDDEN(_update_nodes_relevancy)) {
		const bool executed = GDVIRTUAL_CALL(_update_nodes_relevancy);
		if (executed == false) {
			NET_DEBUG_ERR("The function _update_nodes_relevancy failed!");
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
		SceneSynchronizerDebugger::singleton()->debug_error(nullptr, "[ERROR] The registered node `" + node->get_path() + "` doesn't override the method `_setup_synchronizer`, which is called by the SceneSynchronizer to know the node sync properties. Pleaes implement it.");
	}
}

void GdSceneSynchronizer::set_variable(NS::ObjectHandle p_app_object_handle, const char *p_name, const Variant &p_val) {
	Node *node = scene_synchronizer.from_handle(p_app_object_handle);
	node->set(StringName(p_name), p_val);
}

bool GdSceneSynchronizer::get_variable(NS::ObjectHandle p_app_object_handle, const char *p_name, Variant &p_val) const {
	const Node *node = scene_synchronizer.from_handle(p_app_object_handle);
	bool valid = false;
	p_val = node->get(StringName(p_name), &valid);
	if (valid) {
		p_val = p_val.duplicate(true);
	}
	return valid;
}

NS::NetworkedControllerBase *GdSceneSynchronizer::extract_network_controller(NS::ObjectHandle p_app_object_handle) {
	if (GdNetworkedController *c = Object::cast_to<GdNetworkedController>(scene_synchronizer.from_handle(p_app_object_handle))) {
		return c->get_networked_controller();
	}
	return nullptr;
}

const NS::NetworkedControllerBase *GdSceneSynchronizer::extract_network_controller(NS::ObjectHandle p_app_object_handle) const {
	if (const GdNetworkedController *c = Object::cast_to<const GdNetworkedController>(scene_synchronizer.from_handle(p_app_object_handle))) {
		return c->get_networked_controller();
	}
	return nullptr;
}

void GdSceneSynchronizer::set_max_deferred_nodes_per_update(int p_rate) {
	scene_synchronizer.set_max_deferred_nodes_per_update(p_rate);
}

int GdSceneSynchronizer::get_max_deferred_nodes_per_update() const {
	return scene_synchronizer.get_max_deferred_nodes_per_update();
}

void GdSceneSynchronizer::set_server_notify_state_interval(real_t p_interval) {
	scene_synchronizer.set_server_notify_state_interval(p_interval);
}

real_t GdSceneSynchronizer::get_server_notify_state_interval() const {
	return scene_synchronizer.get_server_notify_state_interval();
}

void GdSceneSynchronizer::set_comparison_float_tolerance(real_t p_tolerance) {
	comparison_float_tolerance = p_tolerance;
}

real_t GdSceneSynchronizer::get_comparison_float_tolerance() const {
	return comparison_float_tolerance;
}

void GdSceneSynchronizer::set_nodes_relevancy_update_time(real_t p_time) {
	scene_synchronizer.set_nodes_relevancy_update_time(p_time);
}

real_t GdSceneSynchronizer::get_nodes_relevancy_update_time() const {
	return scene_synchronizer.get_nodes_relevancy_update_time();
}

void GdSceneSynchronizer::_rpc_net_sync_reliable(const Vector<uint8_t> &p_args) {
	static_cast<GdNetworkInterface *>(&scene_synchronizer.get_network_interface())->gd_rpc_receive(p_args);
}

void GdSceneSynchronizer::_rpc_net_sync_unreliable(const Vector<uint8_t> &p_args) {
	static_cast<GdNetworkInterface *>(&scene_synchronizer.get_network_interface())->gd_rpc_receive(p_args);
}

void GdSceneSynchronizer::reset_synchronizer_mode() {
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

void GdSceneSynchronizer::unregister_node(Node *p_node) {
	scene_synchronizer.unregister_app_object(scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(p_node)));
}

uint32_t GdSceneSynchronizer::get_node_id(Node *p_node) {
	return scene_synchronizer.get_app_object_net_id(scene_synchronizer.to_handle(p_node)).id;
}

Node *GdSceneSynchronizer::get_node_from_id(uint32_t p_id, bool p_expected) {
	return SyncClass::from_handle(scene_synchronizer.get_app_object_from_id(NS::ObjectNetId{ p_id }, p_expected));
}

const Node *GdSceneSynchronizer::get_node_from_id_const(uint32_t p_id, bool p_expected) const {
	return SyncClass::from_handle(scene_synchronizer.get_app_object_from_id_const(NS::ObjectNetId{ p_id }, p_expected));
}

void GdSceneSynchronizer::register_variable(Node *p_node, const StringName &p_variable) {
	scene_synchronizer.register_variable(scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(p_node)), p_variable);
}

void GdSceneSynchronizer::unregister_variable(Node *p_node, const StringName &p_variable) {
	scene_synchronizer.unregister_variable(scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(p_node)), p_variable);
}

uint32_t GdSceneSynchronizer::get_variable_id(Node *p_node, const StringName &p_variable) {
	NS::ObjectLocalId id = scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(p_node));
	if (id != NS::ObjectLocalId::NONE) {
		return scene_synchronizer.get_variable_id(id, p_variable).id;
	}
	return NS::VarId::NONE.id;
}

void GdSceneSynchronizer::set_skip_rewinding(Node *p_node, const StringName &p_variable, bool p_skip_rewinding) {
	NS::ObjectLocalId id = scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(p_node));
	if (id != NS::ObjectLocalId::NONE) {
		scene_synchronizer.set_skip_rewinding(id, p_variable, p_skip_rewinding);
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
	std::vector<StringName> var_names;

	for (int i = 0; i < int(p_nodes.size()); i++) {
		Object *obj = p_nodes[i];
		Node *node = dynamic_cast<Node *>(obj);
		NS::ObjectLocalId lid = scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(node));
		objects_ids.push_back(lid);
		var_names.push_back(p_vars[i]);
	}

	NS::ListenerHandle raw_handle =
			scene_synchronizer.track_variables_changes(
					objects_ids,
					var_names,
					[p_callable](const std::vector<Variant> &p_old_variables) {
						Array arguments;
						for (auto ov : p_old_variables) {
							arguments.push_back(ov);
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

void GdSceneSynchronizer::setup_deferred_sync(Node *p_node, const Callable &p_collect_epoch_func, const Callable &p_apply_epoch_func) {
	scene_synchronizer.setup_deferred_sync(scene_synchronizer.find_object_local_id(scene_synchronizer.to_handle(p_node)), p_collect_epoch_func, p_apply_epoch_func);
}

SyncGroupId GdSceneSynchronizer::sync_group_create() {
	return scene_synchronizer.sync_group_create();
}

const NS::SyncGroup *GdSceneSynchronizer::sync_group_get(SyncGroupId p_group_id) const {
	return scene_synchronizer.sync_group_get(p_group_id);
}

void GdSceneSynchronizer::sync_group_add_node_by_id(uint32_t p_net_id, SyncGroupId p_group_id, bool p_realtime) {
	scene_synchronizer.sync_group_add_node_by_id({ p_net_id }, p_group_id, p_realtime);
}

void GdSceneSynchronizer::sync_group_add_node(NS::ObjectData *p_object_data, SyncGroupId p_group_id, bool p_realtime) {
	scene_synchronizer.sync_group_add_node(p_object_data, p_group_id, p_realtime);
}

void GdSceneSynchronizer::sync_group_remove_node_by_id(uint32_t p_net_id, SyncGroupId p_group_id) {
	scene_synchronizer.sync_group_remove_node_by_id({ p_net_id }, p_group_id);
}

void GdSceneSynchronizer::sync_group_remove_node(NS::ObjectData *p_object_data, SyncGroupId p_group_id) {
	scene_synchronizer.sync_group_remove_node(p_object_data, p_group_id);
}

void GdSceneSynchronizer::sync_group_replace_nodes(SyncGroupId p_group_id, LocalVector<NS::SyncGroup::RealtimeNodeInfo> &&p_new_realtime_nodes, LocalVector<NS::SyncGroup::DeferredNodeInfo> &&p_new_deferred_nodes) {
	scene_synchronizer.sync_group_replace_nodes(p_group_id, std::move(p_new_realtime_nodes), std::move(p_new_deferred_nodes));
}

void GdSceneSynchronizer::sync_group_remove_all_nodes(SyncGroupId p_group_id) {
	scene_synchronizer.sync_group_remove_all_nodes(p_group_id);
}

void GdSceneSynchronizer::sync_group_move_peer_to(int p_peer_id, SyncGroupId p_group_id) {
	scene_synchronizer.sync_group_move_peer_to(p_peer_id, p_group_id);
}

SyncGroupId GdSceneSynchronizer::sync_group_get_peer_group(int p_peer_id) const {
	return scene_synchronizer.sync_group_get_peer_group(p_peer_id);
}

const LocalVector<int> *GdSceneSynchronizer::sync_group_get_peers(SyncGroupId p_group_id) const {
	return scene_synchronizer.sync_group_get_peers(p_group_id);
}

void GdSceneSynchronizer::sync_group_set_deferred_update_rate_by_id(uint32_t p_net_id, SyncGroupId p_group_id, real_t p_update_rate) {
	scene_synchronizer.sync_group_set_deferred_update_rate(NS::ObjectNetId{ p_net_id }, p_group_id, p_update_rate);
}

void GdSceneSynchronizer::sync_group_set_deferred_update_rate(NS::ObjectData *p_object_data, SyncGroupId p_group_id, real_t p_update_rate) {
	scene_synchronizer.sync_group_set_deferred_update_rate(p_object_data->get_local_id(), p_group_id, p_update_rate);
}

real_t GdSceneSynchronizer::sync_group_get_deferred_update_rate_by_id(uint32_t p_net_id, SyncGroupId p_group_id) const {
	return scene_synchronizer.sync_group_get_deferred_update_rate(NS::ObjectNetId{ p_net_id }, p_group_id);
}

real_t GdSceneSynchronizer::sync_group_get_deferred_update_rate(const NS::ObjectData *p_object_data, SyncGroupId p_group_id) const {
	return scene_synchronizer.sync_group_get_deferred_update_rate(p_object_data->get_local_id(), p_group_id);
}

void GdSceneSynchronizer::sync_group_set_user_data(SyncGroupId p_group_id, uint64_t p_user_data) {
	scene_synchronizer.sync_group_set_user_data(p_group_id, p_user_data);
}

uint64_t GdSceneSynchronizer::sync_group_get_user_data(SyncGroupId p_group_id) const {
	return scene_synchronizer.sync_group_get_user_data(p_group_id);
}

void GdSceneSynchronizer::start_tracking_scene_changes(Object *p_diff_handle) const {
	scene_synchronizer.start_tracking_scene_changes(p_diff_handle);
}

void GdSceneSynchronizer::stop_tracking_scene_changes(Object *p_diff_handle) const {
	scene_synchronizer.stop_tracking_scene_changes(p_diff_handle);
}

Variant GdSceneSynchronizer::pop_scene_changes(Object *p_diff_handle) const {
	return scene_synchronizer.pop_scene_changes(p_diff_handle);
}

void GdSceneSynchronizer::apply_scene_changes(Object *p_sync_data) {
	DataBuffer *db = Object::cast_to<DataBuffer>(p_sync_data);
	if (db) {
		scene_synchronizer.apply_scene_changes(*db);
	}
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

void GdSceneSynchronizer::force_state_notify(SyncGroupId p_sync_group_id) {
	scene_synchronizer.force_state_notify(p_sync_group_id);
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

bool GdSceneSynchronizer::is_peer_networking_enable(int p_peer) const {
	return scene_synchronizer.is_peer_networking_enable(p_peer);
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
