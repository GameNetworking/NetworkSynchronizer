#include "gd_scene_synchronizer.h"

#include "modules/network_synchronizer/godot4/gd_network_interface.h"
#include "modules/network_synchronizer/scene_synchronizer.h"
#include "scene/main/multiplayer_api.h"

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
}

GdSceneSynchronizer::~GdSceneSynchronizer() {
}

void GdSceneSynchronizer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			if (Engine::get_singleton()->is_editor_hint())
				return;

			if (low_level_peer != get_multiplayer()->get_multiplayer_peer().ptr()) {
				// The low level peer changed, so we need to refresh the synchronizer.
				scene_synchronizer.reset_synchronizer_mode();
			}

			const int lowest_priority_number = INT32_MAX;
			ERR_FAIL_COND_MSG(get_process_priority() != lowest_priority_number, "The process priority MUST not be changed, it's likely there is a better way of doing what you are trying to do, if you really need it please open an issue.");

			scene_synchronizer.process();
		} break;
		case NOTIFICATION_ENTER_TREE: {
			if (Engine::get_singleton()->is_editor_hint())
				return;

			GdNetworkInterface *ni = memnew(GdNetworkInterface);
			ni->owner = this;
			scene_synchronizer.setup(*ni);

			get_tree()->connect(SNAME("node_removed"), Callable(this, SNAME("_on_node_removed")));

		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (Engine::get_singleton()->is_editor_hint())
				return;

			NS::NetworkInterface &ni = scene_synchronizer.get_network_interface();
			scene_synchronizer.pre_destroy();
			memdelete(&ni);

			get_tree()->disconnect(SNAME("node_removed"), Callable(this, SNAME("_on_node_removed")));
		}
	}
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

void GdSceneSynchronizer::on_init_synchronizer() {
	// Always runs the SceneSynchronizer last.
	const int lowest_priority_number = INT32_MAX;
	owner->set_process_priority(lowest_priority_number);
	owner->set_physics_process_internal(true);
}

void GdSceneSynchronizer::on_uninit_synchronizer() {
	owner->set_physics_process_internal(false);
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

void GdSceneSynchronizer::register_process(Node *p_node, ProcessPhase p_phase, const Callable &p_func) {
	scene_synchronizer.register_process(p_node, p_phase, p_func);
}

void GdSceneSynchronizer::unregister_process(Node *p_node, ProcessPhase p_phase, const Callable &p_func) {
	scene_synchronizer.register_process(p_node, p_phase, p_func);
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

void GdSceneSynchronizer::_on_node_removed(Node *p_node) {
	unregister_node(p_node);
}

void GdSceneSynchronizer::_rpc_send_state(const Variant &p_snapshot) {
	ERR_FAIL_COND_MSG(is_client() == false, "Only clients are suposed to receive the server snapshot.");
	static_cast<ClientSynchronizer *>(synchronizer)->receive_snapshot(p_snapshot);
}

void GdSceneSynchronizer::_rpc_notify_need_full_snapshot() {
	ERR_FAIL_COND_MSG(is_server() == false, "Only the server can receive the request to send a full snapshot.");

	const int sender_peer = network_interface->rpc_get_sender();
	NetUtility::PeerData *pd = peer_data.lookup_ptr(sender_peer);
	ERR_FAIL_COND(pd == nullptr);
	pd->need_full_snapshot = true;
}

void GdSceneSynchronizer::_rpc_set_network_enabled(bool p_enabled) {
	ERR_FAIL_COND_MSG(is_server() == false, "The peer status is supposed to be received by the server.");
	set_peer_networking_enable(
			network_interface->rpc_get_sender(),
			p_enabled);
}

void GdSceneSynchronizer::_rpc_notify_peer_status(bool p_enabled) {
	ERR_FAIL_COND_MSG(is_client() == false, "The peer status is supposed to be received by the client.");
	static_cast<ClientSynchronizer *>(synchronizer)->set_enabled(p_enabled);
}

void GdSceneSynchronizer::_rpc_send_deferred_sync_data(const Vector<uint8_t> &p_data) {
	ERR_FAIL_COND_MSG(is_client() == false, "Only clients are supposed to receive this function call.");
	ERR_FAIL_COND_MSG(p_data.size() <= 0, "It's not supposed to receive a 0 size data.");

	static_cast<ClientSynchronizer *>(synchronizer)->receive_deferred_sync_data(p_data);
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
