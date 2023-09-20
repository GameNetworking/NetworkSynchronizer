#include "gd_scene_synchronizer.h"

#include "modules/network_synchronizer/core/processor.h"
#include "modules/network_synchronizer/godot4/gd_network_interface.h"
#include "modules/network_synchronizer/godot4/gd_networked_controller.h"
#include "modules/network_synchronizer/net_utilities.h"
#include "modules/network_synchronizer/scene_synchronizer.h"
#include "modules/network_synchronizer/scene_synchronizer_debugger.h"
#include "modules/network_synchronizer/snapshot.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/window.h"

void GdSceneSynchronizer::_bind_methods() {
	BIND_CONSTANT(NS::SceneSynchronizer::GLOBAL_SYNC_GROUP_ID)

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

	ClassDB::bind_method(D_METHOD("register_variable", "node", "variable", "on_change_notify", "flags"), &GdSceneSynchronizer::register_variable, DEFVAL(StringName()), DEFVAL(NetEventFlag::DEFAULT));
	ClassDB::bind_method(D_METHOD("unregister_variable", "node", "variable"), &GdSceneSynchronizer::unregister_variable);
	ClassDB::bind_method(D_METHOD("get_variable_id", "node", "variable"), &GdSceneSynchronizer::get_variable_id);

	ClassDB::bind_method(D_METHOD("set_skip_rewinding", "node", "variable", "skip_rewinding"), &GdSceneSynchronizer::set_skip_rewinding);

	ClassDB::bind_method(D_METHOD("track_variable_changes", "node", "variable", "object", "method", "flags"), &GdSceneSynchronizer::track_variable_changes, DEFVAL(NetEventFlag::DEFAULT));
	ClassDB::bind_method(D_METHOD("untrack_variable_changes", "node", "variable", "object", "method"), &GdSceneSynchronizer::untrack_variable_changes);

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

	ClassDB::bind_method(D_METHOD("_on_node_removed"), &GdSceneSynchronizer::_on_node_removed);

	ClassDB::bind_method(D_METHOD("_rpc_send_state"), &GdSceneSynchronizer::_rpc_send_state);
	ClassDB::bind_method(D_METHOD("_rpc_notify_need_full_snapshot"), &GdSceneSynchronizer::_rpc_notify_need_full_snapshot);
	ClassDB::bind_method(D_METHOD("_rpc_set_network_enabled", "enabled"), &GdSceneSynchronizer::_rpc_set_network_enabled);
	ClassDB::bind_method(D_METHOD("_rpc_notify_peer_status", "enabled"), &GdSceneSynchronizer::_rpc_notify_peer_status);
	ClassDB::bind_method(D_METHOD("_rpc_send_deferred_sync_data", "data"), &GdSceneSynchronizer::_rpc_send_deferred_sync_data);

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
	Dictionary rpc_config_reliable;
	rpc_config_reliable["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
	rpc_config_reliable["call_local"] = false;
	rpc_config_reliable["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;

	Dictionary rpc_config_unreliable;
	rpc_config_unreliable["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
	rpc_config_unreliable["call_local"] = false;
	rpc_config_unreliable["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_UNRELIABLE;

	rpc_config(SNAME("_rpc_send_state"), rpc_config_reliable);
	rpc_config(SNAME("_rpc_notify_need_full_snapshot"), rpc_config_reliable);
	rpc_config(SNAME("_rpc_set_network_enabled"), rpc_config_reliable);
	rpc_config(SNAME("_rpc_notify_peer_status"), rpc_config_reliable);
	rpc_config(SNAME("_rpc_send_deferred_sync_data"), rpc_config_unreliable);

	event_handler_sync_started =
			scene_synchronizer.event_sync_started.bind([this]() -> void {
				emit_signal("sync_started");
			});

	event_handler_sync_paused =
			scene_synchronizer.event_sync_paused.bind([this]() -> void {
				emit_signal("sync_paused");
			});

	event_handler_peer_status_updated =
			scene_synchronizer.event_peer_status_updated.bind([this](const NetUtility::NodeData *p_node_data, int p_peer, bool p_connected, bool p_enabled) -> void {
				Object *obj_null = nullptr;
				emit_signal(
						"peer_status_updated",
						p_node_data ? p_node_data->node : obj_null,
						p_node_data ? p_node_data->id : NetID_NONE,
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
			scene_synchronizer.event_desync_detected.bind([this](uint32_t p_input_id, Node *p_node, const Vector<StringName> &p_var_names, const Vector<Variant> &p_client_values, const Vector<Variant> &p_server_values) -> void {
				emit_signal("desync_detected", p_input_id, p_node, p_var_names, p_client_values, p_server_values);
			});
}

GdSceneSynchronizer::~GdSceneSynchronizer() {
	scene_synchronizer.event_sync_started.unbind(event_handler_sync_started);
	event_handler_sync_started = NS::NullFuncHandler;

	scene_synchronizer.event_sync_paused.unbind(event_handler_sync_paused);
	event_handler_sync_paused = NS::NullFuncHandler;

	scene_synchronizer.event_peer_status_updated.unbind(event_handler_peer_status_updated);
	event_handler_peer_status_updated = NS::NullFuncHandler;

	scene_synchronizer.event_state_validated.unbind(event_handler_state_validated);
	event_handler_state_validated = NS::NullFuncHandler;

	scene_synchronizer.event_rewind_frame_begin.unbind(event_handler_rewind_frame_begin);
	event_handler_rewind_frame_begin = NS::NullFuncHandler;

	scene_synchronizer.event_desync_detected.unbind(event_handler_desync_detected);
	event_handler_desync_detected = NS::NullFuncHandler;
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

			GdNetworkInterface *ni = memnew(GdNetworkInterface);
			ni->owner = this;
			scene_synchronizer.setup(
					*ni,
					*this);

			get_tree()->connect(SNAME("node_removed"), Callable(this, SNAME("_on_node_removed")));

		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}

			NS::NetworkInterface &ni = scene_synchronizer.get_network_interface();
			scene_synchronizer.conclude();
			memdelete(&ni);

			get_tree()->disconnect(SNAME("node_removed"), Callable(this, SNAME("_on_node_removed")));
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

void GdSceneSynchronizer::update_nodes_relevancy() {
	if (GDVIRTUAL_IS_OVERRIDDEN(_update_nodes_relevancy)) {
		const bool executed = GDVIRTUAL_CALL(_update_nodes_relevancy);
		if (executed == false) {
			NET_DEBUG_ERR("The function _update_nodes_relevancy failed!");
		}
	}
}

Node *GdSceneSynchronizer::get_node_or_null(const NodePath &p_path) {
	if (get_tree() && get_tree()->get_root()) {
		return get_tree()->get_root()->get_node_or_null(p_path);
	}
	return nullptr;
}

NS::NetworkedController *GdSceneSynchronizer::extract_network_controller(Node *p_node) const {
	if (GdNetworkedController *c = Object::cast_to<GdNetworkedController>(p_node)) {
		return c->get_networked_controller();
	}
	return nullptr;
}

const NS::NetworkedController *GdSceneSynchronizer::extract_network_controller(const Node *p_node) const {
	if (const GdNetworkedController *c = Object::cast_to<const GdNetworkedController>(p_node)) {
		return c->get_networked_controller();
	}
	return nullptr;
}

void GdSceneSynchronizer::rpc_send__state(int p_peer, const Variant &p_snapshot) {
	rpc_id(p_peer, SNAME("_rpc_send_state"), p_snapshot);
}

void GdSceneSynchronizer::rpc_send__notify_need_full_snapshot(int p_peer) {
	rpc_id(p_peer, SNAME("_rpc_notify_need_full_snapshot"));
}

void GdSceneSynchronizer::rpc_send__set_network_enabled(int p_peer, bool p_enabled) {
	rpc_id(
			p_peer,
			SNAME("_rpc_set_network_enabled"),
			p_enabled);
}

void GdSceneSynchronizer::rpc_send__notify_peer_status(int p_peer, bool p_enabled) {
	rpc_id(p_peer, SNAME("_rpc_notify_peer_status"), p_enabled);
}

void GdSceneSynchronizer::rpc_send__deferred_sync_data(int p_peer, const Vector<uint8_t> &p_data) {
	rpc_id(p_peer, SNAME("_rpc_send_deferred_sync_data"), p_data);
}

void GdSceneSynchronizer::_rpc_send_state(const Variant &p_snapshot) {
	scene_synchronizer.rpc_receive__state(p_snapshot);
}

void GdSceneSynchronizer::_rpc_notify_need_full_snapshot() {
	scene_synchronizer.rpc_receive__notify_need_full_snapshot();
}

void GdSceneSynchronizer::_rpc_set_network_enabled(bool p_enabled) {
	scene_synchronizer.rpc_receive__set_network_enabled(p_enabled);
}

void GdSceneSynchronizer::_rpc_notify_peer_status(bool p_enabled) {
	scene_synchronizer.rpc_receive__notify_peer_status(p_enabled);
}

void GdSceneSynchronizer::_rpc_send_deferred_sync_data(const Vector<uint8_t> &p_data) {
	scene_synchronizer.rpc_receive__deferred_sync_data(p_data);
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
	scene_synchronizer.set_comparison_float_tolerance(p_tolerance);
}

real_t GdSceneSynchronizer::get_comparison_float_tolerance() const {
	return scene_synchronizer.get_comparison_float_tolerance();
}

void GdSceneSynchronizer::set_nodes_relevancy_update_time(real_t p_time) {
	scene_synchronizer.set_nodes_relevancy_update_time(p_time);
}

real_t GdSceneSynchronizer::get_nodes_relevancy_update_time() const {
	return scene_synchronizer.get_nodes_relevancy_update_time();
}

void GdSceneSynchronizer::reset_synchronizer_mode() {
	scene_synchronizer.reset_synchronizer_mode();
}

void GdSceneSynchronizer::clear() {
	scene_synchronizer.clear();
}

NetUtility::NodeData *GdSceneSynchronizer::register_node(Node *p_node) {
	return scene_synchronizer.register_node(p_node);
}

uint32_t GdSceneSynchronizer::register_node_gdscript(Node *p_node) {
	NetUtility::NodeData *nd = register_node(p_node);
	if (unlikely(nd == nullptr)) {
		return UINT32_MAX;
	}
	return nd->id;
}

void GdSceneSynchronizer::unregister_node(Node *p_node) {
	scene_synchronizer.unregister_node(p_node);
}

uint32_t GdSceneSynchronizer::get_node_id(Node *p_node) {
	return scene_synchronizer.get_node_id(p_node);
}

Node *GdSceneSynchronizer::get_node_from_id(uint32_t p_id, bool p_expected) {
	return scene_synchronizer.get_node_from_id(p_id, p_expected);
}

const Node *GdSceneSynchronizer::get_node_from_id_const(uint32_t p_id, bool p_expected) const {
	return scene_synchronizer.get_node_from_id_const(p_id, p_expected);
}

void GdSceneSynchronizer::register_variable(Node *p_node, const StringName &p_variable, const StringName &p_on_change_notify, NetEventFlag p_flags) {
	scene_synchronizer.register_variable(p_node, p_variable, p_on_change_notify, p_flags);
}

void GdSceneSynchronizer::unregister_variable(Node *p_node, const StringName &p_variable) {
	scene_synchronizer.unregister_variable(p_node, p_variable);
}

uint32_t GdSceneSynchronizer::get_variable_id(Node *p_node, const StringName &p_variable) {
	return scene_synchronizer.get_variable_id(p_node, p_variable);
}

void GdSceneSynchronizer::set_skip_rewinding(Node *p_node, const StringName &p_variable, bool p_skip_rewinding) {
	scene_synchronizer.set_skip_rewinding(p_node, p_variable, p_skip_rewinding);
}

void GdSceneSynchronizer::track_variable_changes(Node *p_node, const StringName &p_variable, Object *p_object, const StringName &p_method, NetEventFlag p_flags) {
	scene_synchronizer.track_variable_changes(p_node, p_variable, p_object, p_method);
}

void GdSceneSynchronizer::untrack_variable_changes(Node *p_node, const StringName &p_variable, Object *p_object, const StringName &p_method) {
	scene_synchronizer.untrack_variable_changes(p_node, p_variable, p_object, p_method);
}

uint64_t GdSceneSynchronizer::register_process(Node *p_node, ProcessPhase p_phase, const Callable &p_func) {
	NetUtility::NodeData *nd = scene_synchronizer.register_node(p_node);
	const NS::FuncHandler EFH = scene_synchronizer.register_process(nd, p_phase, [p_func](float p_delta) {
		Array a;
		a.push_back(p_delta);
		p_func.callv(a);
	});
	return reinterpret_cast<uint64_t>(EFH);
}

void GdSceneSynchronizer::unregister_process(Node *p_node, ProcessPhase p_phase, uint64_t p_handler) {
	NetUtility::NodeData *nd = scene_synchronizer.find_node_data(p_node);
	if (nd) {
		scene_synchronizer.unregister_process(
				nd,
				p_phase,
				reinterpret_cast<NS::FuncHandler>(p_handler));
	}
}

void GdSceneSynchronizer::setup_deferred_sync(Node *p_node, const Callable &p_collect_epoch_func, const Callable &p_apply_epoch_func) {
	scene_synchronizer.setup_deferred_sync(p_node, p_collect_epoch_func, p_apply_epoch_func);
}

SyncGroupId GdSceneSynchronizer::sync_group_create() {
	return scene_synchronizer.sync_group_create();
}

const NetUtility::SyncGroup *GdSceneSynchronizer::sync_group_get(SyncGroupId p_group_id) const {
	return scene_synchronizer.sync_group_get(p_group_id);
}

void GdSceneSynchronizer::sync_group_add_node_by_id(NetNodeId p_node_id, SyncGroupId p_group_id, bool p_realtime) {
	scene_synchronizer.sync_group_add_node_by_id(p_node_id, p_group_id, p_realtime);
}

void GdSceneSynchronizer::sync_group_add_node(NetUtility::NodeData *p_node_data, SyncGroupId p_group_id, bool p_realtime) {
	scene_synchronizer.sync_group_add_node(p_node_data, p_group_id, p_realtime);
}

void GdSceneSynchronizer::sync_group_remove_node_by_id(NetNodeId p_node_id, SyncGroupId p_group_id) {
	scene_synchronizer.sync_group_remove_node_by_id(p_node_id, p_group_id);
}

void GdSceneSynchronizer::sync_group_remove_node(NetUtility::NodeData *p_node_data, SyncGroupId p_group_id) {
	scene_synchronizer.sync_group_remove_node(p_node_data, p_group_id);
}

void GdSceneSynchronizer::sync_group_replace_nodes(SyncGroupId p_group_id, LocalVector<NetUtility::SyncGroup::RealtimeNodeInfo> &&p_new_realtime_nodes, LocalVector<NetUtility::SyncGroup::DeferredNodeInfo> &&p_new_deferred_nodes) {
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

void GdSceneSynchronizer::sync_group_set_deferred_update_rate_by_id(NetNodeId p_node_id, SyncGroupId p_group_id, real_t p_update_rate) {
	scene_synchronizer.sync_group_set_deferred_update_rate_by_id(p_node_id, p_group_id, p_update_rate);
}

void GdSceneSynchronizer::sync_group_set_deferred_update_rate(NetUtility::NodeData *p_node_data, SyncGroupId p_group_id, real_t p_update_rate) {
	scene_synchronizer.sync_group_set_deferred_update_rate(p_node_data, p_group_id, p_update_rate);
}

real_t GdSceneSynchronizer::sync_group_get_deferred_update_rate_by_id(NetNodeId p_node_id, SyncGroupId p_group_id) const {
	return scene_synchronizer.sync_group_get_deferred_update_rate_by_id(p_node_id, p_group_id);
}

real_t GdSceneSynchronizer::sync_group_get_deferred_update_rate(const NetUtility::NodeData *p_node_data, SyncGroupId p_group_id) const {
	return scene_synchronizer.sync_group_get_deferred_update_rate(p_node_data, p_group_id);
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

void GdSceneSynchronizer::apply_scene_changes(const Variant &p_sync_data) {
	scene_synchronizer.apply_scene_changes(p_sync_data);
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

void GdSceneSynchronizer::_on_node_removed(Node *p_node) {
	unregister_node(p_node);
}
