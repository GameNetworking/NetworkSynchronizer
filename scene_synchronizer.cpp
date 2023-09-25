
#include "scene_synchronizer.h"

#include "core/error/error_macros.h"
#include "core/object/object.h"
#include "core/os/os.h"
#include "core/templates/oa_hash_map.h"
#include "core/variant/variant.h"
#include "input_network_encoder.h"
#include "modules/network_synchronizer/core/network_interface.h"
#include "modules/network_synchronizer/core/processor.h"
#include "modules/network_synchronizer/godot4/gd_network_interface.h"
#include "modules/network_synchronizer/net_utilities.h"
#include "modules/network_synchronizer/networked_controller.h"
#include "modules/network_synchronizer/snapshot.h"
#include "scene_diff.h"
#include "scene_synchronizer_debugger.h"

NS_NAMESPACE_BEGIN

const SyncGroupId SceneSynchronizer::GLOBAL_SYNC_GROUP_ID = 0;

SceneSynchronizer::SceneSynchronizer() {
	// Avoid too much useless re-allocations.
	event_listener.reserve(100);
}

SceneSynchronizer::~SceneSynchronizer() {
	clear();
	uninit_synchronizer();
}

void SceneSynchronizer::setup(
		NetworkInterface &p_network_interface,
		SynchronizerManager &p_synchronizer_interface) {
	network_interface = &p_network_interface;
	synchronizer_manager = &p_synchronizer_interface;

	clear();
	reset_synchronizer_mode();

	network_interface->start_listening_peer_connection(
			[this](int p_peer) { on_peer_connected(p_peer); },
			[this](int p_peer) { on_peer_disconnected(p_peer); });

	rpc_handler_state =
			network_interface->rpc_config(
					std::function<void(const Variant &)>(std::bind(&SceneSynchronizer::rpc_receive_state, this, std::placeholders::_1)),
					true,
					false);

	rpc_handler_notify_need_full_snapshot =
			network_interface->rpc_config(
					std::function<void()>(std::bind(&SceneSynchronizer::rpc__notify_need_full_snapshot, this)),
					true,
					false);

	rpc_handler_set_network_enabled =
			network_interface->rpc_config(
					std::function<void(bool)>(std::bind(&SceneSynchronizer::rpc_set_network_enabled, this, std::placeholders::_1)),
					true,
					false);

	rpc_handler_notify_peer_status =
			network_interface->rpc_config(
					std::function<void(bool)>(std::bind(&SceneSynchronizer::rpc_notify_peer_status, this, std::placeholders::_1)),
					true,
					false);

	rpc_handler_deferred_sync_data =
			network_interface->rpc_config(
					std::function<void(const Vector<uint8_t> &)>(std::bind(&SceneSynchronizer::rpc_deferred_sync_data, this, std::placeholders::_1)),
					false,
					false);

	// Make sure to reset all the assigned controllers.
	reset_controllers();

	// Init the peers already connected.
	std::vector<int> peer_ids;
	network_interface->fetch_connected_peers(peer_ids);
	for (int peer_id : peer_ids) {
		on_peer_connected(peer_id);
	}
}

void SceneSynchronizer::conclude() {
	network_interface->stop_listening_peer_connection();
	network_interface->clear();

	clear_peers();
	clear();
	uninit_synchronizer();

	// Make sure to reset all the assigned controllers.
	reset_controllers();

	network_interface = nullptr;
	synchronizer_manager = nullptr;
}

void SceneSynchronizer::process() {
	PROFILE_NODE

#ifdef DEBUG_ENABLED
	validate_nodes();
	CRASH_COND_MSG(synchronizer == nullptr, "Never execute this function unless this synchronizer is ready.");
#endif

	synchronizer->process();
}

void SceneSynchronizer::on_app_object_removed(void *p_app_object) {
	unregister_app_object(p_app_object);
}

void SceneSynchronizer::set_max_deferred_nodes_per_update(int p_rate) {
	max_deferred_nodes_per_update = p_rate;
}

int SceneSynchronizer::get_max_deferred_nodes_per_update() const {
	return max_deferred_nodes_per_update;
}

void SceneSynchronizer::set_server_notify_state_interval(real_t p_interval) {
	server_notify_state_interval = p_interval;
}

real_t SceneSynchronizer::get_server_notify_state_interval() const {
	return server_notify_state_interval;
}

void SceneSynchronizer::set_comparison_float_tolerance(real_t p_tolerance) {
	comparison_float_tolerance = p_tolerance;
}

real_t SceneSynchronizer::get_comparison_float_tolerance() const {
	return comparison_float_tolerance;
}

void SceneSynchronizer::set_nodes_relevancy_update_time(real_t p_time) {
	nodes_relevancy_update_time = p_time;
}

real_t SceneSynchronizer::get_nodes_relevancy_update_time() const {
	return nodes_relevancy_update_time;
}

bool SceneSynchronizer::is_variable_registered(void *p_app_object, const StringName &p_variable) const {
	const NetUtility::NodeData *nd = find_node_data(p_app_object);
	if (nd != nullptr) {
		return nd->vars.find(p_variable) >= 0;
	}
	return false;
}

NetUtility::NodeData *SceneSynchronizer::register_app_object(void *p_app_object) {
	ERR_FAIL_COND_V(p_app_object == nullptr, nullptr);

	NetUtility::NodeData *nd = find_node_data(p_app_object);
	if (unlikely(nd == nullptr)) {
		// TODO consider to put this in a pre-allocated memory buffer.
		nd = memnew(NetUtility::NodeData);
		nd->id = UINT32_MAX;
		nd->instance_id = synchronizer_manager->get_object_id(p_app_object);
		nd->object_name = synchronizer_manager->get_object_name(p_app_object);
		nd->app_object = p_app_object;

		nd->controller = synchronizer_manager->extract_network_controller(p_app_object);
		if (nd->controller) {
			if (unlikely(nd->controller->has_scene_synchronizer())) {
				ERR_FAIL_V_MSG(nullptr, "This controller already has a synchronizer. This is a bug!");
			}

			dirty_peers();
		}

		add_node_data(nd);

		synchronizer_manager->setup_synchronizer_for(p_app_object);

		SceneSynchronizerDebugger::singleton()->debug_print(network_interface, "New node registered" + (generate_id ? String(" #ID: ") + itos(nd->id) : "") + " : " + nd->object_name.c_str());

		if (nd->controller) {
			nd->controller->notify_registered_with_synchronizer(this, *nd);
		}
	}

	return nd;
}

void SceneSynchronizer::unregister_app_object(void *p_app_object) {
	ERR_FAIL_COND(p_app_object == nullptr);

	NetUtility::NodeData *nd = find_node_data(p_app_object);
	if (unlikely(nd == nullptr)) {
		// Nothing to do.
		return;
	}

	drop_node_data(nd);
}

NetNodeId SceneSynchronizer::get_app_object_net_id(void *p_app_object) const {
	const NetUtility::NodeData *nd = find_node_data(p_app_object);
	if (nd) {
		return nd->id;
	} else {
		return NetID_NONE;
	}
}

void *SceneSynchronizer::get_app_object_from_id(uint32_t p_id, bool p_expected) {
	NetUtility::NodeData *nd = get_node_data(p_id, p_expected);
	if (p_expected) {
		ERR_FAIL_COND_V_MSG(nd == nullptr, nullptr, "The ID " + itos(p_id) + " is not assigned to any node.");
		return nd->app_object;
	} else {
		return nd ? nd->app_object : nullptr;
	}
}

const void *SceneSynchronizer::get_app_object_from_id_const(uint32_t p_id, bool p_expected) const {
	const NetUtility::NodeData *nd = get_node_data(p_id, p_expected);
	if (p_expected) {
		ERR_FAIL_COND_V_MSG(nd == nullptr, nullptr, "The ID " + itos(p_id) + " is not assigned to any node.");
		return nd->app_object;
	} else {
		return nd ? nd->app_object : nullptr;
	}
}

void SceneSynchronizer::register_variable(void *p_app_object, const StringName &p_variable, const StringName &p_on_change_notify, NetEventFlag p_flags) {
	ERR_FAIL_COND(p_app_object == nullptr);
	ERR_FAIL_COND(p_variable == StringName());

	NetUtility::NodeData *node_data = register_app_object(p_app_object);
	ERR_FAIL_COND(node_data == nullptr);

	const int index = node_data->vars.find(p_variable);
	if (index == -1) {
		// The variable is not yet registered.
		bool valid = false;
		Variant old_val;
		valid = synchronizer_manager->get_variable(p_app_object, String(p_variable).utf8(), old_val);
		if (valid == false) {
			SceneSynchronizerDebugger::singleton()->debug_error(network_interface, "The variable `" + p_variable + "` on the node `" + String(node_data->object_name.c_str()) + "` was not found, make sure the variable exist.");
		}
		const int var_id = generate_id ? node_data->vars.size() : UINT32_MAX;
		node_data->vars.push_back(
				NetUtility::VarData(
						var_id,
						p_variable,
						old_val,
						false,
						true));
	} else {
		// Make sure the var is active.
		node_data->vars[index].enabled = true;
	}

#ifdef DEBUG_ENABLED
	for (uint32_t v = 0; v < node_data->vars.size(); v += 1) {
		// This can't happen, because the ID is always consecutive, or UINT32_MAX.
		CRASH_COND(node_data->vars[v].id != v && node_data->vars[v].id != UINT32_MAX);
	}
#endif

	if (p_on_change_notify != StringName()) {
		track_variable_changes(
				p_app_object,
				p_variable,
				// TODO this is an hack for now, but this will cause a crash on UnitTests.
				static_cast<Object *>(p_app_object),
				p_on_change_notify,
				p_flags);
	}

	if (synchronizer) {
		synchronizer->on_variable_added(node_data, p_variable);
	}
}

void SceneSynchronizer::unregister_variable(void *p_app_object, const StringName &p_variable) {
	ERR_FAIL_COND(p_app_object == nullptr);
	ERR_FAIL_COND(p_variable == StringName());

	NetUtility::NodeData *nd = find_node_data(p_app_object);
	ERR_FAIL_COND(nd == nullptr);

	const int64_t index = nd->vars.find(p_variable);
	ERR_FAIL_COND(index == -1);

	const NetVarId var_id = index;

	// Never remove the variable values, because the order of the vars matters.
	nd->vars[index].enabled = false;

	for (int i = 0; i < nd->vars[var_id].change_listeners.size(); i += 1) {
		const uint32_t event_index = nd->vars[var_id].change_listeners[i];
		// Just erase the tracked variables without removing the listener to
		// keep the order.
		NetUtility::NodeChangeListener ncl;
		ncl.node_data = nd;
		ncl.var_id = var_id;
		event_listener[event_index].watching_vars.erase(ncl);
	}

	nd->vars[index].change_listeners.clear();
}

uint32_t SceneSynchronizer::get_variable_id(void *p_app_object, const StringName &p_variable) {
	ERR_FAIL_COND_V(p_app_object == nullptr, UINT32_MAX);
	ERR_FAIL_COND_V(p_variable == StringName(), UINT32_MAX);

	NetUtility::NodeData *nd = find_node_data(p_app_object);
	ERR_FAIL_COND_V_MSG(nd == nullptr, UINT32_MAX, "This node " + String(nd->object_name.c_str()) + "is not registered.");

	const int64_t index = nd->vars.find(p_variable);
	ERR_FAIL_COND_V_MSG(index == -1, UINT32_MAX, "This variable " + String(nd->object_name.c_str()) + ":" + p_variable + " is not registered.");

	return uint32_t(index);
}

void SceneSynchronizer::set_skip_rewinding(void *p_app_object, const StringName &p_variable, bool p_skip_rewinding) {
	ERR_FAIL_COND(p_app_object == nullptr);
	ERR_FAIL_COND(p_variable == StringName());

	NetUtility::NodeData *nd = find_node_data(p_app_object);
	ERR_FAIL_COND(nd == nullptr);

	const int64_t index = nd->vars.find(p_variable);
	ERR_FAIL_COND(index == -1);

	nd->vars[index].skip_rewinding = p_skip_rewinding;
}

void SceneSynchronizer::track_variable_changes(void *p_app_object, const StringName &p_variable, Object *p_object, const StringName &p_method, NetEventFlag p_flags) {
	ERR_FAIL_COND(p_app_object == nullptr);
	ERR_FAIL_COND(p_variable == StringName());
	ERR_FAIL_COND(p_method == StringName());

	NetUtility::NodeData *nd = find_node_data(p_app_object);
	ERR_FAIL_COND_MSG(nd == nullptr, "You need to register the variable to track its changes.");

	const int64_t v = nd->vars.find(p_variable);
	ERR_FAIL_COND_MSG(v == -1, "You need to register the variable to track its changes.");

	const NetVarId var_id = v;

	int64_t index;

	{
		NetUtility::ChangeListener listener;
		listener.object_id = p_object->get_instance_id();
		listener.method = p_method;

		index = event_listener.find(listener);

		if (-1 == index) {
			// Add it.
			listener.flag = p_flags;
			listener.method_argument_count = UINT32_MAX;

			// Search the method and get the argument count.
			List<MethodInfo> methods;
			p_object->get_method_list(&methods);
			for (List<MethodInfo>::Element *e = methods.front(); e != nullptr; e = e->next()) {
				if (e->get().name != p_method) {
					continue;
				}

				listener.method_argument_count = e->get().arguments.size();

				break;
			}
			ERR_FAIL_COND_MSG(listener.method_argument_count == UINT32_MAX, "The method " + p_method + " doesn't exist in this node: " + String(nd->object_name.c_str()));

			index = event_listener.size();
			event_listener.push_back(listener);
		} else {
			ERR_FAIL_COND_MSG(event_listener[index].flag != p_flags, "The event listener is already registered with the flag: " + itos(event_listener[index].flag) + ". You can't specify a different one.");
		}
	}

	NetUtility::NodeChangeListener ncl;
	ncl.node_data = nd;
	ncl.var_id = var_id;

	if (event_listener[index].watching_vars.find(ncl) != -1) {
		return;
	}

	event_listener[index].watching_vars.push_back(ncl);
	nd->vars[var_id].change_listeners.push_back(index);
}

void SceneSynchronizer::untrack_variable_changes(void *p_app_object, const StringName &p_variable, Object *p_object, const StringName &p_method) {
	ERR_FAIL_COND(p_app_object == nullptr);
	ERR_FAIL_COND(p_variable == StringName());
	ERR_FAIL_COND(p_method == StringName());

	NetUtility::NodeData *nd = find_node_data(p_app_object);
	ERR_FAIL_COND_MSG(nd == nullptr, "This not is not registered.");

	const int64_t v = nd->vars.find(p_variable);
	ERR_FAIL_COND_MSG(v == -1, "This variable is not registered.");

	const NetVarId var_id = v;

	NetUtility::ChangeListener listener;
	listener.object_id = p_object->get_instance_id();
	listener.method = p_method;

	const int64_t index = event_listener.find(listener);

	ERR_FAIL_COND_MSG(index == -1, "The variable is not know.");

	NetUtility::NodeChangeListener ncl;
	ncl.node_data = nd;
	ncl.var_id = var_id;

	event_listener[index].watching_vars.erase(ncl);
	nd->vars[var_id].change_listeners.erase(index);

	// Don't remove the listener to preserve the order.
}

NS::PHandler SceneSynchronizer::register_process(NetUtility::NodeData *p_node_data, ProcessPhase p_phase, std::function<void(float)> p_func) {
	ERR_FAIL_COND_V(p_node_data == nullptr, NS::NullPHandler);
	ERR_FAIL_COND_V(!p_func, NS::NullPHandler);

	const NS::PHandler EFH = p_node_data->functions[p_phase].bind(p_func);

	process_functions__clear();

	return EFH;
}

void SceneSynchronizer::unregister_process(NetUtility::NodeData *p_node_data, ProcessPhase p_phase, NS::PHandler p_func_handler) {
	ERR_FAIL_COND(p_node_data == nullptr);
	p_node_data->functions[p_phase].unbind(p_func_handler);
	process_functions__clear();
}

void SceneSynchronizer::setup_deferred_sync(void *p_app_object, const Callable &p_collect_epoch_func, const Callable &p_apply_epoch_func) {
	ERR_FAIL_COND(p_app_object == nullptr);
	ERR_FAIL_COND(!p_collect_epoch_func.is_valid());
	ERR_FAIL_COND(!p_apply_epoch_func.is_valid());
	NetUtility::NodeData *node_data = register_app_object(p_app_object);
	node_data->collect_epoch_func = p_collect_epoch_func;
	node_data->apply_epoch_func = p_apply_epoch_func;
	SceneSynchronizerDebugger::singleton()->debug_print(network_interface, "Setup deferred sync functions for: `" + String(node_data->object_name.c_str()) + "`. Collect epoch, method name: `" + p_collect_epoch_func.get_method() + "`. Apply epoch, method name: `" + p_apply_epoch_func.get_method() + "`.");
}

SyncGroupId SceneSynchronizer::sync_group_create() {
	ERR_FAIL_COND_V_MSG(!is_server(), NetID_NONE, "This function CAN be used only on the server.");
	const SyncGroupId id = static_cast<ServerSynchronizer *>(synchronizer)->sync_group_create();
	synchronizer_manager->on_sync_group_created(id);
	return id;
}

const NetUtility::SyncGroup *SceneSynchronizer::sync_group_get(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(!is_server(), nullptr, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get(p_group_id);
}

void SceneSynchronizer::sync_group_add_node_by_id(NetNodeId p_node_id, SyncGroupId p_group_id, bool p_realtime) {
	NetUtility::NodeData *nd = get_node_data(p_node_id);
	sync_group_add_node(nd, p_group_id, p_realtime);
}

void SceneSynchronizer::sync_group_add_node(NetUtility::NodeData *p_node_data, SyncGroupId p_group_id, bool p_realtime) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_add_node(p_node_data, p_group_id, p_realtime);
}

void SceneSynchronizer::sync_group_remove_node_by_id(NetNodeId p_node_id, SyncGroupId p_group_id) {
	NetUtility::NodeData *nd = get_node_data(p_node_id);
	sync_group_remove_node(nd, p_group_id);
}

void SceneSynchronizer::sync_group_remove_node(NetUtility::NodeData *p_node_data, SyncGroupId p_group_id) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_remove_node(p_node_data, p_group_id);
}

void SceneSynchronizer::sync_group_replace_nodes(SyncGroupId p_group_id, LocalVector<NetUtility::SyncGroup::RealtimeNodeInfo> &&p_new_realtime_nodes, LocalVector<NetUtility::SyncGroup::DeferredNodeInfo> &&p_new_deferred_nodes) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_replace_nodes(p_group_id, std::move(p_new_realtime_nodes), std::move(p_new_deferred_nodes));
}

void SceneSynchronizer::sync_group_remove_all_nodes(SyncGroupId p_group_id) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_remove_all_nodes(p_group_id);
}

void SceneSynchronizer::sync_group_move_peer_to(int p_peer_id, SyncGroupId p_group_id) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");

	NetUtility::PeerData *pd = peer_data.lookup_ptr(p_peer_id);
	ERR_FAIL_COND_MSG(pd == nullptr, "The PeerData doesn't exist. This looks like a bug. Are you sure the peer_id `" + itos(p_peer_id) + "` exists?");

	if (pd->sync_group_id == p_group_id) {
		// Nothing to do.
		return;
	}

	pd->sync_group_id = p_group_id;

	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_move_peer_to(p_peer_id, p_group_id);
}

SyncGroupId SceneSynchronizer::sync_group_get_peer_group(int p_peer_id) const {
	ERR_FAIL_COND_V_MSG(!is_server(), NetID_NONE, "This function CAN be used only on the server.");

	const NetUtility::PeerData *pd = peer_data.lookup_ptr(p_peer_id);
	ERR_FAIL_COND_V_MSG(pd == nullptr, NetID_NONE, "The PeerData doesn't exist. This looks like a bug. Are you sure the peer_id `" + itos(p_peer_id) + "` exists?");

	return pd->sync_group_id;
}

const LocalVector<int> *SceneSynchronizer::sync_group_get_peers(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(!is_server(), nullptr, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get_peers(p_group_id);
}

void SceneSynchronizer::sync_group_set_deferred_update_rate_by_id(NetNodeId p_node_id, SyncGroupId p_group_id, real_t p_update_rate) {
	NetUtility::NodeData *nd = get_node_data(p_node_id);
	sync_group_set_deferred_update_rate(nd, p_group_id, p_update_rate);
}

void SceneSynchronizer::sync_group_set_deferred_update_rate(NetUtility::NodeData *p_node_data, SyncGroupId p_group_id, real_t p_update_rate) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_set_deferred_update_rate(p_node_data, p_group_id, p_update_rate);
}

real_t SceneSynchronizer::sync_group_get_deferred_update_rate_by_id(NetNodeId p_node_id, SyncGroupId p_group_id) const {
	const NetUtility::NodeData *nd = get_node_data(p_node_id);
	return sync_group_get_deferred_update_rate(nd, p_group_id);
}

real_t SceneSynchronizer::sync_group_get_deferred_update_rate(const NetUtility::NodeData *p_node_data, SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(!is_server(), 0.0, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get_deferred_update_rate(p_node_data, p_group_id);
}

void SceneSynchronizer::sync_group_set_user_data(SyncGroupId p_group_id, uint64_t p_user_data) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_set_user_data(p_group_id, p_user_data);
}

uint64_t SceneSynchronizer::sync_group_get_user_data(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(!is_server(), 0, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get_user_data(p_group_id);
}

void SceneSynchronizer::start_tracking_scene_changes(Object *p_diff_handle) const {
	ERR_FAIL_COND_MSG(!is_server(), "This function is supposed to be called only on server.");
	SceneDiff *diff = Object::cast_to<SceneDiff>(p_diff_handle);
	ERR_FAIL_COND_MSG(diff == nullptr, "The object is not a SceneDiff class.");

	diff->start_tracking_scene_changes(this, organized_node_data);
}

void SceneSynchronizer::stop_tracking_scene_changes(Object *p_diff_handle) const {
	ERR_FAIL_COND_MSG(!is_server(), "This function is supposed to be called only on server.");
	SceneDiff *diff = Object::cast_to<SceneDiff>(p_diff_handle);
	ERR_FAIL_COND_MSG(diff == nullptr, "The object is not a SceneDiff class.");

	diff->stop_tracking_scene_changes(this);
}

Variant SceneSynchronizer::pop_scene_changes(Object *p_diff_handle) const {
	ERR_FAIL_COND_V_MSG(
			synchronizer_type != SYNCHRONIZER_TYPE_SERVER,
			Variant(),
			"This function is supposed to be called only on server.");

	SceneDiff *diff = Object::cast_to<SceneDiff>(p_diff_handle);
	ERR_FAIL_COND_V_MSG(
			diff == nullptr,
			Variant(),
			"The object is not a SceneDiff class.");

	ERR_FAIL_COND_V_MSG(
			diff->is_tracking_in_progress(),
			Variant(),
			"You can't pop the changes while the tracking is still in progress.");

	// Generates a sync_data and returns it.
	Vector<Variant> ret;
	for (NetNodeId node_id = 0; node_id < diff->diff.size(); node_id += 1) {
		if (diff->diff[node_id].size() == 0) {
			// Nothing to do.
			continue;
		}

		bool node_id_in_ret = false;
		for (NetVarId var_id = 0; var_id < diff->diff[node_id].size(); var_id += 1) {
			if (diff->diff[node_id][var_id].is_different == false) {
				continue;
			}
			if (node_id_in_ret == false) {
				node_id_in_ret = true;
				// Set the node id.
				ret.push_back(node_id);
			}
			ret.push_back(var_id);
			ret.push_back(diff->diff[node_id][var_id].value);
		}
		if (node_id_in_ret) {
			// Close the Node data.
			ret.push_back(Variant());
		}
	}

	// Clear the diff data.
	diff->diff.clear();

	return ret.size() > 0 ? Variant(ret) : Variant();
}

void SceneSynchronizer::apply_scene_changes(const Variant &p_sync_data) {
	ERR_FAIL_COND_MSG(is_client() == false, "This function is not supposed to be called on server.");

	ClientSynchronizer *client_sync = static_cast<ClientSynchronizer *>(synchronizer);

	change_events_begin(NetEventFlag::CHANGE);

	const bool success = client_sync->parse_sync_data(
			p_sync_data,
			this,

			// Custom data:
			[](void *p_user_pointer, const LocalVector<const Variant *> &p_custom_data) {
				for (int i = 0; i < int(p_custom_data.size()); i++) {
					if (p_custom_data[i] != nullptr) {
						ERR_PRINT("[ERROR] Please add support to this feature.");
					}
				}
			},

			// Parse the Node:
			[](void *p_user_pointer, NetUtility::NodeData *p_node_data) {},

			// Parse InputID:
			[](void *p_user_pointer, uint32_t p_input_id) {},

			// Parse controller:
			[](void *p_user_pointer, NetUtility::NodeData *p_node_data) {},

			// Parse variable:
			[](void *p_user_pointer, NetUtility::NodeData *p_node_data, uint32_t p_var_id, const Variant &p_value) {
				SceneSynchronizer *scene_sync = static_cast<SceneSynchronizer *>(p_user_pointer);

				const Variant current_val = p_node_data->vars[p_var_id].var.value;

				if (scene_sync->compare(current_val, p_value) == false) {
					// There is a difference.
					// Set the new value.
					p_node_data->vars[p_var_id].var.value = p_value;
					scene_sync->synchronizer_manager->set_variable(
							p_node_data->app_object,
							String(p_node_data->vars[p_var_id].var.name).utf8().get_data(),
							p_value);

					// Add an event.
					scene_sync->change_event_add(
							p_node_data,
							p_var_id,
							current_val);
				}
			},

			// Parse node activation:
			[](void *p_user_pointer, NetUtility::NodeData *p_node_data, bool p_is_active) {
			});

	if (success == false) {
		SceneSynchronizerDebugger::singleton()->debug_error(network_interface, "Scene changes:");
		SceneSynchronizerDebugger::singleton()->debug_error(network_interface, NetUtility::stringify_fast(p_sync_data));
	}

	change_events_flush();
}

bool SceneSynchronizer::is_recovered() const {
	return recover_in_progress;
}

bool SceneSynchronizer::is_resetted() const {
	return reset_in_progress;
}

bool SceneSynchronizer::is_rewinding() const {
	return rewinding_in_progress;
}

bool SceneSynchronizer::is_end_sync() const {
	return end_sync;
}

void SceneSynchronizer::force_state_notify(SyncGroupId p_sync_group_id) {
	ERR_FAIL_COND(is_server() == false);
	ServerSynchronizer *r = static_cast<ServerSynchronizer *>(synchronizer);
	// + 1.0 is just a ridiculous high number to be sure to avoid float
	// precision error.
	ERR_FAIL_COND_MSG(p_sync_group_id >= r->sync_groups.size(), "The group id `" + itos(p_sync_group_id) + "` doesn't exist.");
	r->sync_groups[p_sync_group_id].state_notifier_timer = get_server_notify_state_interval() + 1.0;
}

void SceneSynchronizer::force_state_notify_all() {
	ERR_FAIL_COND(is_server() == false);
	ServerSynchronizer *r = static_cast<ServerSynchronizer *>(synchronizer);

	for (uint32_t i = 0; i < r->sync_groups.size(); ++i) {
		// + 1.0 is just a ridiculous high number to be sure to avoid float
		// precision error.
		r->sync_groups[i].state_notifier_timer = get_server_notify_state_interval() + 1.0;
	}
}

void SceneSynchronizer::dirty_peers() {
	peer_dirty = true;
}

void SceneSynchronizer::set_enabled(bool p_enable) {
	ERR_FAIL_COND_MSG(synchronizer_type == SYNCHRONIZER_TYPE_SERVER, "The server is always enabled.");
	if (synchronizer_type == SYNCHRONIZER_TYPE_CLIENT) {
		network_interface->rpc(rpc_handler_set_network_enabled, network_interface->get_server_peer(), p_enable);
		if (p_enable == false) {
			// If the peer want to disable, we can disable it locally
			// immediately. When it wants to enable the networking, the server
			// must be notified so it decides when to start the networking
			// again.
			static_cast<ClientSynchronizer *>(synchronizer)->set_enabled(p_enable);
		}
	} else if (synchronizer_type == SYNCHRONIZER_TYPE_NONETWORK) {
		set_peer_networking_enable(0, p_enable);
	}
}

bool SceneSynchronizer::is_enabled() const {
	ERR_FAIL_COND_V_MSG(synchronizer_type == SYNCHRONIZER_TYPE_SERVER, false, "The server is always enabled.");
	if (likely(synchronizer_type == SYNCHRONIZER_TYPE_CLIENT)) {
		return static_cast<ClientSynchronizer *>(synchronizer)->enabled;
	} else if (synchronizer_type == SYNCHRONIZER_TYPE_NONETWORK) {
		return static_cast<NoNetSynchronizer *>(synchronizer)->enabled;
	} else {
		return true;
	}
}

void SceneSynchronizer::set_peer_networking_enable(int p_peer, bool p_enable) {
	if (synchronizer_type == SYNCHRONIZER_TYPE_SERVER) {
		ERR_FAIL_COND_MSG(p_peer == 1, "Disable the server is not possible.");

		NetUtility::PeerData *pd = peer_data.lookup_ptr(p_peer);
		ERR_FAIL_COND_MSG(pd == nullptr, "The peer: " + itos(p_peer) + " is not know. [bug]");

		if (pd->enabled == p_enable) {
			// Nothing to do.
			return;
		}

		pd->enabled = p_enable;
		// Set to true, so next time this peer connects a full snapshot is sent.
		pd->force_notify_snapshot = true;
		pd->need_full_snapshot = true;

		if (p_enable) {
			static_cast<ServerSynchronizer *>(synchronizer)->sync_group_move_peer_to(p_peer, pd->sync_group_id);
		} else {
			static_cast<ServerSynchronizer *>(synchronizer)->sync_group_move_peer_to(p_peer, NetID_NONE);
		}

		dirty_peers();

		// Just notify the peer status.
		network_interface->rpc(rpc_handler_notify_peer_status, p_peer, p_enable);
	} else {
		ERR_FAIL_COND_MSG(synchronizer_type != SYNCHRONIZER_TYPE_NONETWORK, "At this point no network is expected.");
		static_cast<NoNetSynchronizer *>(synchronizer)->set_enabled(p_enable);
	}
}

bool SceneSynchronizer::is_peer_networking_enable(int p_peer) const {
	if (synchronizer_type == SYNCHRONIZER_TYPE_SERVER) {
		if (p_peer == 1) {
			// Server is always enabled.
			return true;
		}

		const NetUtility::PeerData *pd = peer_data.lookup_ptr(p_peer);
		ERR_FAIL_COND_V_MSG(pd == nullptr, false, "The peer: " + itos(p_peer) + " is not know. [bug]");
		return pd->enabled;
	} else {
		ERR_FAIL_COND_V_MSG(synchronizer_type != SYNCHRONIZER_TYPE_NONETWORK, false, "At this point no network is expected.");
		return static_cast<NoNetSynchronizer *>(synchronizer)->is_enabled();
	}
}

void SceneSynchronizer::on_peer_connected(int p_peer) {
	peer_data.set(p_peer, NetUtility::PeerData());

	event_peer_status_updated.broadcast(nullptr, p_peer, true, false);

	dirty_peers();
	if (synchronizer) {
		synchronizer->on_peer_connected(p_peer);
	}
}

void SceneSynchronizer::on_peer_disconnected(int p_peer) {
	// Emit a signal notifying this peer is gone.
	NetUtility::PeerData *pd = peer_data.lookup_ptr(p_peer);
	NetNodeId id = NetID_NONE;
	NetUtility::NodeData *node_data = nullptr;
	if (pd) {
		id = pd->controller_id;
		node_data = get_node_data(id);
	}

	event_peer_status_updated.broadcast(node_data, p_peer, false, false);

	peer_data.remove(p_peer);
#ifdef DEBUG_ENABLED
	CRASH_COND_MSG(peer_data.has(p_peer), "The peer was just removed. This can't be triggered.");
#endif

	if (synchronizer) {
		synchronizer->on_peer_disconnected(p_peer);
	}
}

void SceneSynchronizer::init_synchronizer(bool p_was_generating_ids) {
	if (!network_interface->is_local_peer_networked()) {
		synchronizer_type = SYNCHRONIZER_TYPE_NONETWORK;
		synchronizer = memnew(NoNetSynchronizer(this));
		generate_id = true;

	} else if (network_interface->is_local_peer_server()) {
		synchronizer_type = SYNCHRONIZER_TYPE_SERVER;
		synchronizer = memnew(ServerSynchronizer(this));
		generate_id = true;
	} else {
		synchronizer_type = SYNCHRONIZER_TYPE_CLIENT;
		synchronizer = memnew(ClientSynchronizer(this));
	}

	if (p_was_generating_ids != generate_id) {
		organized_node_data.resize(node_data.size());
		for (uint32_t i = 0; i < node_data.size(); i += 1) {
			if (node_data[i] == nullptr) {
				continue;
			}

			// Handle the node ID.
			if (generate_id) {
				node_data[i]->id = i;
				organized_node_data[i] = node_data[i];
			} else {
				node_data[i]->id = UINT32_MAX;
				organized_node_data[i] = nullptr;
			}

			// Handle the variables ID.
			for (uint32_t v = 0; v < node_data[i]->vars.size(); v += 1) {
				if (generate_id) {
					node_data[i]->vars[v].id = v;
				} else {
					node_data[i]->vars[v].id = UINT32_MAX;
				}
			}
		}
	}

	// Notify the presence all available nodes and its variables to the synchronizer.
	for (uint32_t i = 0; i < node_data.size(); i += 1) {
		synchronizer->on_node_added(node_data[i]);
		for (uint32_t y = 0; y < node_data[i]->vars.size(); y += 1) {
			synchronizer->on_variable_added(node_data[i], node_data[i]->vars[y].var.name);
		}
	}

	// Notify the presence all available peers
	for (
			OAHashMap<int, NetUtility::PeerData>::Iterator peer_it = peer_data.iter();
			peer_it.valid;
			peer_it = peer_data.next_iter(peer_it)) {
		synchronizer->on_peer_connected(*peer_it.key);
	}

	// Reset the controllers.
	reset_controllers();

	process_functions__clear();
	synchronizer_manager->on_init_synchronizer(p_was_generating_ids);
}

void SceneSynchronizer::uninit_synchronizer() {
	if (synchronizer_manager) {
		synchronizer_manager->on_uninit_synchronizer();
	}

	generate_id = false;

	if (synchronizer) {
		memdelete(synchronizer);
		synchronizer = nullptr;
		synchronizer_type = SYNCHRONIZER_TYPE_NULL;
	}
}

void SceneSynchronizer::reset_synchronizer_mode() {
	debug_rewindings_enabled = ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/log_debug_rewindings");
	const bool was_generating_ids = generate_id;
	uninit_synchronizer();
	init_synchronizer(was_generating_ids);
}

void SceneSynchronizer::clear() {
	// Drop the node_data.
	for (uint32_t i = 0; i < node_data.size(); i += 1) {
		if (node_data[i] != nullptr) {
			drop_node_data(node_data[i]);
		}
	}

	node_data.reset();
	organized_node_data.reset();
	node_data_controllers.reset();
	event_listener.reset();

	// Avoid too much useless re-allocations.
	event_listener.reserve(100);

	if (synchronizer) {
		synchronizer->clear();
	}

	process_functions__clear();
}

void SceneSynchronizer::notify_controller_control_mode_changed(NetworkedController *controller) {
	reset_controller(find_node_data(controller));
}

void SceneSynchronizer::rpc_receive_state(const Variant &p_snapshot) {
	ERR_FAIL_COND_MSG(is_client() == false, "Only clients are suposed to receive the server snapshot.");
	static_cast<ClientSynchronizer *>(synchronizer)->receive_snapshot(p_snapshot);
}

void SceneSynchronizer::rpc__notify_need_full_snapshot() {
	ERR_FAIL_COND_MSG(is_server() == false, "Only the server can receive the request to send a full snapshot.");

	const int sender_peer = network_interface->rpc_get_sender();
	NetUtility::PeerData *pd = peer_data.lookup_ptr(sender_peer);
	ERR_FAIL_COND(pd == nullptr);
	pd->need_full_snapshot = true;
}

void SceneSynchronizer::rpc_set_network_enabled(bool p_enabled) {
	ERR_FAIL_COND_MSG(is_server() == false, "The peer status is supposed to be received by the server.");
	set_peer_networking_enable(
			network_interface->rpc_get_sender(),
			p_enabled);
}

void SceneSynchronizer::rpc_notify_peer_status(bool p_enabled) {
	ERR_FAIL_COND_MSG(is_client() == false, "The peer status is supposed to be received by the client.");
	static_cast<ClientSynchronizer *>(synchronizer)->set_enabled(p_enabled);
}

void SceneSynchronizer::rpc_deferred_sync_data(const Vector<uint8_t> &p_data) {
	ERR_FAIL_COND_MSG(is_client() == false, "Only clients are supposed to receive this function call.");
	ERR_FAIL_COND_MSG(p_data.size() <= 0, "It's not supposed to receive a 0 size data.");

	static_cast<ClientSynchronizer *>(synchronizer)->receive_deferred_sync_data(p_data);
}

void SceneSynchronizer::update_peers() {
#ifdef DEBUG_ENABLED
	// This function is only called on server.
	CRASH_COND(synchronizer_type != SYNCHRONIZER_TYPE_SERVER);
#endif

	if (likely(peer_dirty == false)) {
		return;
	}

	peer_dirty = false;

	for (OAHashMap<int, NetUtility::PeerData>::Iterator it = peer_data.iter();
			it.valid;
			it = peer_data.next_iter(it)) {
		// Validate the peer.
		if (it.value->controller_id != UINT32_MAX) {
			NetUtility::NodeData *nd = get_node_data(it.value->controller_id);
			if (nd == nullptr ||
					nd->controller == nullptr ||
					nd->controller->network_interface->get_unit_authority() != (*it.key)) {
				// Invalidate the controller id
				it.value->controller_id = UINT32_MAX;
			}
		}

		if (it.value->controller_id == UINT32_MAX) {
			// The controller_id is not assigned, search it.
			for (uint32_t i = 0; i < node_data_controllers.size(); i += 1) {
				const NetworkedController *nc = node_data_controllers[i]->controller;
				if (nc && nc->network_interface->get_unit_authority() == (*it.key)) {
					// Controller found.
					it.value->controller_id = node_data_controllers[i]->id;
					break;
				}
			}
		}

		NetUtility::NodeData *nd = get_node_data(it.value->controller_id);
		if (nd) {
			nd->realtime_sync_enabled_on_client = it.value->enabled;
			event_peer_status_updated.broadcast(nd, *it.key, true, it.value->enabled);
		}
	}
}

void SceneSynchronizer::clear_peers() {
	// Copy, so we can safely remove the peers from `peer_data`.
	OAHashMap<int, NetUtility::PeerData> peer_data_tmp = peer_data;
	for (OAHashMap<int, NetUtility::PeerData>::Iterator it = peer_data_tmp.iter();
			it.valid;
			it = peer_data_tmp.next_iter(it)) {
		on_peer_disconnected(*it.key);
	}

	CRASH_COND_MSG(!peer_data.is_empty(), "The above loop should have cleared this peer_data by calling `_on_peer_disconnected` for all the peers.");
}

void SceneSynchronizer::detect_and_signal_changed_variables(int p_flags) {
	// Pull the changes.
	if (event_flag != p_flags) {
		// The flag was not set yet.
		change_events_begin(p_flags);
	}

	for (NetNodeId i = 0; i < node_data.size(); i += 1) {
		NetUtility::NodeData *nd = node_data[i];
		CRASH_COND_MSG(nd == nullptr, "This can't happen because we are looping over the `node_data`.");
		pull_node_changes(nd);
	}
	change_events_flush();
}

void SceneSynchronizer::change_events_begin(int p_flag) {
#ifdef DEBUG_ENABLED
	// This can't happen because at the end these are reset.
	CRASH_COND(recover_in_progress);
	CRASH_COND(reset_in_progress);
	CRASH_COND(rewinding_in_progress);
	CRASH_COND(end_sync);
#endif
	event_flag = p_flag;
	recover_in_progress = NetEventFlag::SYNC & p_flag;
	reset_in_progress = NetEventFlag::SYNC_RESET & p_flag;
	rewinding_in_progress = NetEventFlag::SYNC_REWIND & p_flag;
	end_sync = NetEventFlag::END_SYNC & p_flag;
}

void SceneSynchronizer::change_event_add(NetUtility::NodeData *p_node_data, NetVarId p_var_id, const Variant &p_old) {
	for (int i = 0; i < p_node_data->vars[p_var_id].change_listeners.size(); i += 1) {
		const uint32_t listener_index = p_node_data->vars[p_var_id].change_listeners[i];
		NetUtility::ChangeListener &listener = event_listener[listener_index];
		if ((listener.flag & event_flag) == 0) {
			// Not listening to this event.
			continue;
		}

		listener.emitted = false;

		NetUtility::NodeChangeListener ncl;
		ncl.node_data = p_node_data;
		ncl.var_id = p_var_id;

		const int64_t index = listener.watching_vars.find(ncl);
#ifdef DEBUG_ENABLED
		// This can't never happen because the `NodeData::change_listeners`
		// tracks the correct listener.
		CRASH_COND(index == -1);
#endif
		listener.watching_vars[index].old_value = p_old;
		listener.watching_vars[index].old_set = true;
	}

	// Notify the synchronizer.
	if (synchronizer) {
		synchronizer->on_variable_changed(
				p_node_data,
				p_var_id,
				p_old,
				event_flag);
	}
}

void SceneSynchronizer::change_events_flush() {
	LocalVector<Variant> vars;
	LocalVector<const Variant *> vars_ptr;

	// TODO this can be optimized by storing the changed listener in a separate
	// vector. This change must be inserted into the `change_event_add`.
	for (uint32_t listener_i = 0; listener_i < event_listener.size(); listener_i += 1) {
		NetUtility::ChangeListener &listener = event_listener[listener_i];
		if (listener.emitted) {
			continue;
		}
		listener.emitted = true;

		Object *obj = ObjectDB::get_instance(listener.object_id);
		if (obj == nullptr) {
			// Setting the flag to 0 so no events trigger this anymore.
			listener.flag = NetEventFlag::EMPTY;
			listener.object_id = ObjectID();
			listener.method = StringName();

			// Make sure this listener is not tracking any variable.
			for (uint32_t wv = 0; wv < listener.watching_vars.size(); wv += 1) {
				NetUtility::NodeData *nd = listener.watching_vars[wv].node_data;
				uint32_t var_id = listener.watching_vars[wv].var_id;
				nd->vars[var_id].change_listeners.erase(listener_i);
			}
			listener.watching_vars.clear();
			continue;
		}

		// Initialize the arguments
		ERR_CONTINUE_MSG(listener.method_argument_count > listener.watching_vars.size(), "This method " + listener.method + " has more arguments than the watched variables. This listener is broken.");

		vars.resize(MIN(listener.watching_vars.size(), listener.method_argument_count));
		vars_ptr.resize(vars.size());
		for (uint32_t v = 0; v < MIN(listener.watching_vars.size(), listener.method_argument_count); v += 1) {
			if (listener.watching_vars[v].old_set) {
				vars[v] = listener.watching_vars[v].old_value;
				listener.watching_vars[v].old_set = false;
			} else {
				// This value is not changed, so just retrive the current one.
				vars[v] = listener.watching_vars[v].node_data->vars[listener.watching_vars[v].var_id].var.value;
			}
			vars_ptr[v] = vars.ptr() + v;
		}

		Callable::CallError e;
		obj->callp(listener.method, vars_ptr.ptr(), vars_ptr.size(), e);
	}

	recover_in_progress = false;
	reset_in_progress = false;
	rewinding_in_progress = false;
	end_sync = false;
}

void SceneSynchronizer::add_node_data(NetUtility::NodeData *p_node_data) {
	if (generate_id) {
#ifdef DEBUG_ENABLED
		// When generate_id is true, the id must always be undefined.
		CRASH_COND(p_node_data->id != UINT32_MAX);
#endif
		p_node_data->id = organized_node_data.size();
	}

#ifdef DEBUG_ENABLED
	// Make sure the registered nodes have an unique ID.
	// Due to an engine bug, it's possible to have two different nodes with the
	// exact same path:
	//		- Create a scene.
	//		- Add a child with the name `BadChild`.
	//		- Instance the scene into another scene.
	//		- Add a child, under the instanced scene, with the name `BadChild`.
	//	Now you have the scene with two different nodes but same path.
	for (uint32_t i = 0; i < node_data.size(); i += 1) {
		if (node_data[i]->object_name == p_node_data->object_name) {
			SceneSynchronizerDebugger::singleton()->debug_error(network_interface, "You have two different nodes with the same object name: " + String(p_node_data->object_name.c_str()) + ". This will cause troubles. Fix it.");
			break;
		}
	}
#endif

	node_data.push_back(p_node_data);

	if (generate_id) {
		organized_node_data.push_back(p_node_data);
	} else {
		if (p_node_data->id != UINT32_MAX) {
			// This node has an ID, make sure to organize it properly.

			if (organized_node_data.size() <= p_node_data->id) {
				expand_organized_node_data_vector((p_node_data->id + 1) - organized_node_data.size());
			}

			organized_node_data[p_node_data->id] = p_node_data;
		}
	}

	if (p_node_data->controller) {
		node_data_controllers.push_back(p_node_data);
		reset_controller(p_node_data);
	}

	if (p_node_data->has_registered_process_functions()) {
		process_functions__clear();
	}

	if (synchronizer) {
		synchronizer->on_node_added(p_node_data);
	}

	synchronizer_manager->on_add_node_data(p_node_data);
}

void SceneSynchronizer::drop_node_data(NetUtility::NodeData *p_node_data) {
	synchronizer_manager->on_drop_node_data(p_node_data);

	if (synchronizer) {
		synchronizer->on_node_removed(p_node_data);
	}

	if (p_node_data->controller) {
		// This is a controller, make sure to reset the peers.
		p_node_data->controller->notify_registered_with_synchronizer(nullptr, *p_node_data);
		dirty_peers();
		node_data_controllers.erase(p_node_data);
	}

	node_data.erase(p_node_data);

	if (p_node_data->id < organized_node_data.size()) {
		// Never resize this vector to keep it sort.
		organized_node_data[p_node_data->id] = nullptr;
	}

	// Remove this `NodeData` from any event listener.
	for (uint32_t i = 0; i < event_listener.size(); i += 1) {
		while (true) {
			uint32_t index_to_remove = UINT32_MAX;

			// Search.
			for (uint32_t v = 0; v < event_listener[i].watching_vars.size(); v += 1) {
				if (event_listener[i].watching_vars[v].node_data == p_node_data) {
					index_to_remove = v;
					break;
				}
			}

			if (index_to_remove == UINT32_MAX) {
				// Nothing more to do.
				break;
			} else {
				event_listener[i].watching_vars.remove_at_unordered(index_to_remove);
			}
		}
	}

	if (p_node_data->has_registered_process_functions()) {
		process_functions__clear();
	}

	memdelete(p_node_data);
}

void SceneSynchronizer::set_node_data_id(NetUtility::NodeData *p_node_data, NetNodeId p_id) {
#ifdef DEBUG_ENABLED
	CRASH_COND_MSG(generate_id, "This function is not supposed to be called, because this instance is generating the IDs");
#endif
	if (organized_node_data.size() <= p_id) {
		expand_organized_node_data_vector((p_id + 1) - organized_node_data.size());
	}
	p_node_data->id = p_id;
	organized_node_data[p_id] = p_node_data;
	if (p_node_data->has_registered_process_functions()) {
		process_functions__clear();
	}
	SceneSynchronizerDebugger::singleton()->debug_print(network_interface, "NetNodeId: " + itos(p_id) + " just assigned to: " + String(p_node_data->object_name.c_str()));
}

NetworkedController *SceneSynchronizer::fetch_controller_by_peer(int peer) {
	const NetUtility::PeerData *data = peer_data.lookup_ptr(peer);
	if (data && data->controller_id != UINT32_MAX) {
		NetUtility::NodeData *nd = get_node_data(data->controller_id);
		if (nd) {
			return nd->controller;
		}
	}
	return nullptr;
}

bool SceneSynchronizer::compare(const Vector2 &p_first, const Vector2 &p_second) const {
	return compare(p_first, p_second, comparison_float_tolerance);
}

bool SceneSynchronizer::compare(const Vector3 &p_first, const Vector3 &p_second) const {
	return compare(p_first, p_second, comparison_float_tolerance);
}

bool SceneSynchronizer::compare(const Variant &p_first, const Variant &p_second) const {
	return compare(p_first, p_second, comparison_float_tolerance);
}

bool SceneSynchronizer::compare(const Vector2 &p_first, const Vector2 &p_second, real_t p_tolerance) {
	return Math::is_equal_approx(p_first.x, p_second.x, p_tolerance) &&
			Math::is_equal_approx(p_first.y, p_second.y, p_tolerance);
}

bool SceneSynchronizer::compare(const Vector3 &p_first, const Vector3 &p_second, real_t p_tolerance) {
	return Math::is_equal_approx(p_first.x, p_second.x, p_tolerance) &&
			Math::is_equal_approx(p_first.y, p_second.y, p_tolerance) &&
			Math::is_equal_approx(p_first.z, p_second.z, p_tolerance);
}

bool SceneSynchronizer::compare(const Variant &p_first, const Variant &p_second, real_t p_tolerance) {
	if (p_first.get_type() != p_second.get_type()) {
		return false;
	}

	// Custom evaluation methods
	switch (p_first.get_type()) {
		case Variant::FLOAT: {
			return Math::is_equal_approx(p_first, p_second, p_tolerance);
		}
		case Variant::VECTOR2: {
			return compare(Vector2(p_first), Vector2(p_second), p_tolerance);
		}
		case Variant::RECT2: {
			const Rect2 a(p_first);
			const Rect2 b(p_second);
			if (compare(a.position, b.position, p_tolerance)) {
				if (compare(a.size, b.size, p_tolerance)) {
					return true;
				}
			}
			return false;
		}
		case Variant::TRANSFORM2D: {
			const Transform2D a(p_first);
			const Transform2D b(p_second);
			if (compare(a.columns[0], b.columns[0], p_tolerance)) {
				if (compare(a.columns[1], b.columns[1], p_tolerance)) {
					if (compare(a.columns[2], b.columns[2], p_tolerance)) {
						return true;
					}
				}
			}
			return false;
		}
		case Variant::VECTOR3: {
			return compare(Vector3(p_first), Vector3(p_second), p_tolerance);
		}
		case Variant::QUATERNION: {
			const Quaternion a = p_first;
			const Quaternion b = p_second;
			const Quaternion r(a - b); // Element wise subtraction.
			return (r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w) <= (p_tolerance * p_tolerance);
		}
		case Variant::PLANE: {
			const Plane a(p_first);
			const Plane b(p_second);
			if (Math::is_equal_approx(a.d, b.d, p_tolerance)) {
				if (compare(a.normal, b.normal, p_tolerance)) {
					return true;
				}
			}
			return false;
		}
		case Variant::AABB: {
			const AABB a(p_first);
			const AABB b(p_second);
			if (compare(a.position, b.position, p_tolerance)) {
				if (compare(a.size, b.size, p_tolerance)) {
					return true;
				}
			}
			return false;
		}
		case Variant::BASIS: {
			const Basis a = p_first;
			const Basis b = p_second;
			if (compare(a.rows[0], b.rows[0], p_tolerance)) {
				if (compare(a.rows[1], b.rows[1], p_tolerance)) {
					if (compare(a.rows[2], b.rows[2], p_tolerance)) {
						return true;
					}
				}
			}
			return false;
		}
		case Variant::TRANSFORM3D: {
			const Transform3D a = p_first;
			const Transform3D b = p_second;
			if (compare(a.origin, b.origin, p_tolerance)) {
				if (compare(a.basis.rows[0], b.basis.rows[0], p_tolerance)) {
					if (compare(a.basis.rows[1], b.basis.rows[1], p_tolerance)) {
						if (compare(a.basis.rows[2], b.basis.rows[2], p_tolerance)) {
							return true;
						}
					}
				}
			}
			return false;
		}
		case Variant::ARRAY: {
			const Array a = p_first;
			const Array b = p_second;
			if (a.size() != b.size()) {
				return false;
			}
			for (int i = 0; i < a.size(); i += 1) {
				if (compare(a[i], b[i], p_tolerance) == false) {
					return false;
				}
			}
			return true;
		}
		case Variant::DICTIONARY: {
			const Dictionary a = p_first;
			const Dictionary b = p_second;

			if (a.size() != b.size()) {
				return false;
			}

			List<Variant> l;
			a.get_key_list(&l);

			for (const List<Variant>::Element *key = l.front(); key; key = key->next()) {
				if (b.has(key->get()) == false) {
					return false;
				}

				if (compare(
							a.get(key->get(), Variant()),
							b.get(key->get(), Variant()),
							p_tolerance) == false) {
					return false;
				}
			}

			return true;
		}
		default:
			return p_first == p_second;
	}
}

bool SceneSynchronizer::is_server() const {
	return synchronizer_type == SYNCHRONIZER_TYPE_SERVER;
}

bool SceneSynchronizer::is_client() const {
	return synchronizer_type == SYNCHRONIZER_TYPE_CLIENT;
}

bool SceneSynchronizer::is_no_network() const {
	return synchronizer_type == SYNCHRONIZER_TYPE_NONETWORK;
}

bool SceneSynchronizer::is_networked() const {
	return is_client() || is_server();
}

#ifdef DEBUG_ENABLED
void SceneSynchronizer::validate_nodes() {
	LocalVector<NetUtility::NodeData *> null_objects;
	null_objects.reserve(node_data.size());

	for (uint32_t i = 0; i < node_data.size(); i += 1) {
		if (ObjectDB::get_instance(ObjectID(node_data[i]->instance_id)) == nullptr) {
			// Mark for removal.
			null_objects.push_back(node_data[i]);
		}
	}

	// Removes the invalidated `NodeData`.
	if (null_objects.size()) {
		SceneSynchronizerDebugger::singleton()->debug_error(network_interface, "At least one node has been removed from the tree without the SceneSynchronizer noticing. This shouldn't happen.");
		for (uint32_t i = 0; i < null_objects.size(); i += 1) {
			drop_node_data(null_objects[i]);
		}
	}
}
#endif

void SceneSynchronizer::update_nodes_relevancy() {
	synchronizer_manager->update_nodes_relevancy();

	const bool log_debug_nodes_relevancy_update = ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/log_debug_nodes_relevancy_update");
	if (log_debug_nodes_relevancy_update) {
		static_cast<ServerSynchronizer *>(synchronizer)->sync_group_debug_print();
	}
}

void SceneSynchronizer::process_functions__clear() {
	cached_process_functions_valid = false;
}

void SceneSynchronizer::process_functions__execute(const double p_delta) {
	if (cached_process_functions_valid == false) {
		// Clear the process_functions.
		for (int process_phase = PROCESSPHASE_EARLY; process_phase < PROCESSPHASE_COUNT; ++process_phase) {
			cached_process_functions[process_phase].clear();
		}

		// Build the cached_process_functions, making sure the node data order is kept.
		for (uint32_t i = 0; i < organized_node_data.size(); ++i) {
			NetUtility::NodeData *nd = organized_node_data[i];
			if (nd == nullptr || (is_client() && nd->realtime_sync_enabled_on_client == false)) {
				// Nothing to process
				continue;
			}

			// For each valid NodeData.
			for (int process_phase = PROCESSPHASE_EARLY; process_phase < PROCESSPHASE_COUNT; ++process_phase) {
				// Append the contained functions.
				cached_process_functions[process_phase].append(nd->functions[process_phase]);
			}
		}

		cached_process_functions_valid = true;
	}

	SceneSynchronizerDebugger::singleton()->debug_print(network_interface, "Process functions START", true);

	for (int process_phase = PROCESSPHASE_EARLY; process_phase < PROCESSPHASE_COUNT; ++process_phase) {
		cached_process_functions[process_phase].broadcast(p_delta);
	}
}

void SceneSynchronizer::expand_organized_node_data_vector(uint32_t p_size) {
	const uint32_t from = organized_node_data.size();
	organized_node_data.resize(from + p_size);
	memset(organized_node_data.ptr() + from, 0, sizeof(void *) * p_size);
}

NetUtility::NodeData *SceneSynchronizer::find_node_data(const void *p_app_object) {
	for (uint32_t i = 0; i < node_data.size(); i += 1) {
		if (node_data[i] == nullptr) {
			continue;
		}
		if (node_data[i]->app_object == p_app_object) {
			return node_data[i];
		}
	}
	return nullptr;
}

const NetUtility::NodeData *SceneSynchronizer::find_node_data(const void *p_app_object) const {
	for (uint32_t i = 0; i < node_data.size(); i += 1) {
		if (node_data[i] == nullptr) {
			continue;
		}
		if (node_data[i]->app_object == p_app_object) {
			return node_data[i];
		}
	}
	return nullptr;
}

NetUtility::NodeData *SceneSynchronizer::find_node_data(const NetworkedController *p_controller) {
	for (NetUtility::NodeData *nd : node_data_controllers) {
		if (nd == nullptr) {
			continue;
		}
		if (nd->controller == p_controller) {
			return nd;
		}
	}
	return nullptr;
}

const NetUtility::NodeData *SceneSynchronizer::find_node_data(const NetworkedController *p_controller) const {
	for (const NetUtility::NodeData *nd : node_data_controllers) {
		if (nd == nullptr) {
			continue;
		}
		if (nd->controller == p_controller) {
			return nd;
		}
	}
	return nullptr;
}

NetUtility::NodeData *SceneSynchronizer::get_node_data(NetNodeId p_id, bool p_expected) {
	if (p_expected) {
		ERR_FAIL_UNSIGNED_INDEX_V(p_id, organized_node_data.size(), nullptr);
		return organized_node_data[p_id];
	} else {
		if (p_id >= organized_node_data.size()) {
			return nullptr;
		}
		return organized_node_data[p_id];
	}
}

const NetUtility::NodeData *SceneSynchronizer::get_node_data(NetNodeId p_id, bool p_expected) const {
	if (p_expected) {
		ERR_FAIL_UNSIGNED_INDEX_V(p_id, organized_node_data.size(), nullptr);
		return organized_node_data[p_id];
	} else {
		if (p_id >= organized_node_data.size()) {
			return nullptr;
		}
		return organized_node_data[p_id];
	}
}

NetworkedController *SceneSynchronizer::get_controller_for_peer(int p_peer, bool p_expected) {
	const NetUtility::PeerData *pd = peer_data.lookup_ptr(p_peer);
	if (p_expected) {
		ERR_FAIL_COND_V_MSG(pd == nullptr, nullptr, "The peer is unknown `" + itos(p_peer) + "`.");
	}
	NetUtility::NodeData *nd = get_node_data(pd->controller_id, p_expected);
	if (nd) {
		return nd->controller;
	}
	return nullptr;
}

const NetworkedController *SceneSynchronizer::get_controller_for_peer(int p_peer, bool p_expected) const {
	const NetUtility::PeerData *pd = peer_data.lookup_ptr(p_peer);
	if (p_expected) {
		ERR_FAIL_COND_V_MSG(pd == nullptr, nullptr, "The peer is unknown `" + itos(p_peer) + "`.");
	}
	const NetUtility::NodeData *nd = get_node_data(pd->controller_id, p_expected);
	if (nd) {
		return nd->controller;
	}
	return nullptr;
}

NetNodeId SceneSynchronizer::get_biggest_node_id() const {
	return organized_node_data.size() == 0 ? UINT32_MAX : organized_node_data.size() - 1;
}

void SceneSynchronizer::reset_controllers() {
	for (uint32_t i = 0; i < node_data_controllers.size(); i += 1) {
		reset_controller(node_data_controllers[i]);
	}
}

void SceneSynchronizer::reset_controller(NetUtility::NodeData *p_controller_nd) {
#ifdef DEBUG_ENABLED
	// This can't happen because the callers make sure the `NodeData` is a
	// controller.
	CRASH_COND(p_controller_nd->controller == nullptr);
#endif

	NetworkedController *controller = p_controller_nd->controller;

	// Reset the controller type.
	if (controller->controller != nullptr) {
		memdelete(controller->controller);
		controller->controller = nullptr;
		controller->controller_type = NetworkedController::CONTROLLER_TYPE_NULL;
	}

	if (!synchronizer_manager) {
		if (synchronizer) {
			synchronizer->on_controller_reset(p_controller_nd);
		}

		// Nothing to do.
		return;
	}

	if (!network_interface->is_local_peer_networked()) {
		controller->controller_type = NetworkedController::CONTROLLER_TYPE_NONETWORK;
		controller->controller = memnew(NoNetController(controller));
	} else if (network_interface->is_local_peer_server()) {
		if (controller->get_server_controlled()) {
			controller->controller_type = NetworkedController::CONTROLLER_TYPE_AUTONOMOUS_SERVER;
			controller->controller = memnew(AutonomousServerController(controller));
		} else {
			controller->controller_type = NetworkedController::CONTROLLER_TYPE_SERVER;
			controller->controller = memnew(ServerController(controller, controller->get_network_traced_frames()));
		}
	} else if (controller->network_interface->is_local_peer_authority_of_this_unit() && controller->get_server_controlled() == false) {
		controller->controller_type = NetworkedController::CONTROLLER_TYPE_PLAYER;
		controller->controller = memnew(PlayerController(controller));
	} else {
		controller->controller_type = NetworkedController::CONTROLLER_TYPE_DOLL;
		controller->controller = memnew(DollController(controller));
	}

	dirty_peers();
	controller->controller->ready();
	controller->notify_controller_reset();

	if (synchronizer) {
		synchronizer->on_controller_reset(p_controller_nd);
	}
}

void SceneSynchronizer::pull_node_changes(NetUtility::NodeData *p_node_data) {
	for (NetVarId var_id = 0; var_id < p_node_data->vars.size(); var_id += 1) {
		if (p_node_data->vars[var_id].enabled == false) {
			continue;
		}

		const Variant old_val = p_node_data->vars[var_id].var.value;
		Variant new_val;
		synchronizer_manager->get_variable(
				p_node_data->app_object,
				String(p_node_data->vars[var_id].var.name).utf8(),
				new_val);

		if (!compare(old_val, new_val)) {
			p_node_data->vars[var_id].var.value = new_val.duplicate(true);
			change_event_add(
					p_node_data,
					var_id,
					old_val);
		}
	}
}

Synchronizer::Synchronizer(SceneSynchronizer *p_node) :
		scene_synchronizer(p_node) {
}

NoNetSynchronizer::NoNetSynchronizer(SceneSynchronizer *p_node) :
		Synchronizer(p_node) {
}

void NoNetSynchronizer::clear() {
	enabled = true;
	frame_count = 0;
}

void NoNetSynchronizer::process() {
	if (unlikely(enabled == false)) {
		return;
	}

	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "NoNetSynchronizer::process", true);

	const uint32_t frame_index = frame_count;
	frame_count += 1;

	SceneSynchronizerDebugger::singleton()->scene_sync_process_start(scene_synchronizer);

	const double physics_ticks_per_second = Engine::get_singleton()->get_physics_ticks_per_second();
	const double delta = 1.0 / physics_ticks_per_second;

	// Process the scene.
	scene_synchronizer->process_functions__execute(delta);

	scene_synchronizer->detect_and_signal_changed_variables(NetEventFlag::CHANGE);

	SceneSynchronizerDebugger::singleton()->scene_sync_process_end(scene_synchronizer);
	SceneSynchronizerDebugger::singleton()->write_dump(0, frame_index);
	SceneSynchronizerDebugger::singleton()->start_new_frame();
}

void NoNetSynchronizer::set_enabled(bool p_enabled) {
	if (enabled == p_enabled) {
		// Nothing to do.
		return;
	}

	enabled = p_enabled;

	if (enabled) {
		scene_synchronizer->event_sync_started.broadcast();
	} else {
		scene_synchronizer->event_sync_paused.broadcast();
	}
}

bool NoNetSynchronizer::is_enabled() const {
	return enabled;
}

ServerSynchronizer::ServerSynchronizer(SceneSynchronizer *p_node) :
		Synchronizer(p_node) {
	CRASH_COND(SceneSynchronizer::GLOBAL_SYNC_GROUP_ID != sync_group_create());
}

void ServerSynchronizer::clear() {
	nodes_relevancy_update_timer = 0.0;
	// Release the internal memory.
	sync_groups.clear();
}

void ServerSynchronizer::process() {
	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "ServerSynchronizer::process", true);

	scene_synchronizer->update_peers();

	const double physics_ticks_per_second = Engine::get_singleton()->get_physics_ticks_per_second();
	const double delta = 1.0 / physics_ticks_per_second;

	if (nodes_relevancy_update_timer >= scene_synchronizer->nodes_relevancy_update_time) {
		scene_synchronizer->update_nodes_relevancy();
		nodes_relevancy_update_timer = 0.0;
	}
	nodes_relevancy_update_timer += delta;

	SceneSynchronizerDebugger::singleton()->scene_sync_process_start(scene_synchronizer);

	epoch += 1;

	// Process the scene
	scene_synchronizer->process_functions__execute(delta);

	scene_synchronizer->detect_and_signal_changed_variables(NetEventFlag::CHANGE);

	process_snapshot_notificator(delta);
	process_deferred_sync(delta);

	SceneSynchronizerDebugger::singleton()->scene_sync_process_end(scene_synchronizer);

#if DEBUG_ENABLED
	// Write the debug dump for each peer.
	for (
			OAHashMap<int, NetUtility::PeerData>::Iterator peer_it = scene_synchronizer->peer_data.iter();
			peer_it.valid;
			peer_it = scene_synchronizer->peer_data.next_iter(peer_it)) {
		if (unlikely(peer_it.value->controller_id == UINT32_MAX)) {
			continue;
		}

		const NetUtility::NodeData *nd = scene_synchronizer->get_node_data(peer_it.value->controller_id);
		const uint32_t current_input_id = nd->controller->get_server_controller()->get_current_input_id();
		SceneSynchronizerDebugger::singleton()->write_dump(*(peer_it.key), current_input_id);
	}
	SceneSynchronizerDebugger::singleton()->start_new_frame();
#endif
}

void ServerSynchronizer::on_peer_connected(int p_peer_id) {
	sync_group_move_peer_to(p_peer_id, SceneSynchronizer::GLOBAL_SYNC_GROUP_ID);
}

void ServerSynchronizer::on_peer_disconnected(int p_peer_id) {
	for (uint32_t i = 0; i < sync_groups.size(); ++i) {
		sync_groups[i].peers.erase(p_peer_id);
	}
}

void ServerSynchronizer::on_node_added(NetUtility::NodeData *p_node_data) {
#ifdef DEBUG_ENABLED
	// Can't happen on server
	CRASH_COND(scene_synchronizer->is_recovered());
	// On server the ID is always known.
	CRASH_COND(p_node_data->id == UINT32_MAX);
#endif

	sync_groups[SceneSynchronizer::GLOBAL_SYNC_GROUP_ID].add_new_node(p_node_data, true);
}

void ServerSynchronizer::on_node_removed(NetUtility::NodeData *p_node_data) {
	// Make sure to remove this `NodeData` from any sync group.
	for (uint32_t i = 0; i < sync_groups.size(); ++i) {
		sync_groups[i].remove_node(p_node_data);
	}
}

void ServerSynchronizer::on_variable_added(NetUtility::NodeData *p_node_data, const StringName &p_var_name) {
#ifdef DEBUG_ENABLED
	// Can't happen on server
	CRASH_COND(scene_synchronizer->is_recovered());
	// On server the ID is always known.
	CRASH_COND(p_node_data->id == UINT32_MAX);
#endif

	for (uint32_t g = 0; g < sync_groups.size(); ++g) {
		sync_groups[g].notify_new_variable(p_node_data, p_var_name);
	}
}

void ServerSynchronizer::on_variable_changed(NetUtility::NodeData *p_node_data, NetVarId p_var_id, const Variant &p_old_value, int p_flag) {
#ifdef DEBUG_ENABLED
	// Can't happen on server
	CRASH_COND(scene_synchronizer->is_recovered());
	// On server the ID is always known.
	CRASH_COND(p_node_data->id == UINT32_MAX);
#endif

	for (uint32_t g = 0; g < sync_groups.size(); ++g) {
		sync_groups[g].notify_variable_changed(p_node_data, p_node_data->vars[p_var_id].var.name);
	}
}

SyncGroupId ServerSynchronizer::sync_group_create() {
	const SyncGroupId id = sync_groups.size();
	sync_groups.resize(id + 1);
	return id;
}

const NetUtility::SyncGroup *ServerSynchronizer::sync_group_get(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(p_group_id >= sync_groups.size(), nullptr, "The group id `" + itos(p_group_id) + "` doesn't exist.");
	return &sync_groups[p_group_id];
}

void ServerSynchronizer::sync_group_add_node(NetUtility::NodeData *p_node_data, SyncGroupId p_group_id, bool p_realtime) {
	ERR_FAIL_COND(p_node_data == nullptr);
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SceneSynchronizer::GLOBAL_SYNC_GROUP_ID, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id].add_new_node(p_node_data, p_realtime);
}

void ServerSynchronizer::sync_group_remove_node(NetUtility::NodeData *p_node_data, SyncGroupId p_group_id) {
	ERR_FAIL_COND(p_node_data == nullptr);
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SceneSynchronizer::GLOBAL_SYNC_GROUP_ID, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id].remove_node(p_node_data);
}

void ServerSynchronizer::sync_group_replace_nodes(SyncGroupId p_group_id, LocalVector<NetUtility::SyncGroup::RealtimeNodeInfo> &&p_new_realtime_nodes, LocalVector<NetUtility::SyncGroup::DeferredNodeInfo> &&p_new_deferred_nodes) {
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SceneSynchronizer::GLOBAL_SYNC_GROUP_ID, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id].replace_nodes(std::move(p_new_realtime_nodes), std::move(p_new_deferred_nodes));
}

void ServerSynchronizer::sync_group_remove_all_nodes(SyncGroupId p_group_id) {
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SceneSynchronizer::GLOBAL_SYNC_GROUP_ID, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id].remove_all_nodes();
}

void ServerSynchronizer::sync_group_move_peer_to(int p_peer_id, SyncGroupId p_group_id) {
	// remove the peer from any sync_group.
	for (uint32_t i = 0; i < sync_groups.size(); ++i) {
		sync_groups[i].peers.erase(p_peer_id);
	}

	if (p_group_id == NetID_NONE) {
		// This peer is not listening to anything.
		return;
	}

	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	sync_groups[p_group_id].peers.push_back(p_peer_id);

	// Also mark the peer as need full snapshot, as it's into a new group now.
	NetUtility::PeerData *pd = scene_synchronizer->peer_data.lookup_ptr(p_peer_id);
	ERR_FAIL_COND(pd == nullptr);
	pd->force_notify_snapshot = true;
	pd->need_full_snapshot = true;

	// Make sure the controller is added into this group.
	NetUtility::NodeData *nd = scene_synchronizer->get_node_data(pd->controller_id, false);
	if (nd) {
		sync_group_add_node(nd, p_group_id, true);
	}
}

const LocalVector<int> *ServerSynchronizer::sync_group_get_peers(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(p_group_id >= sync_groups.size(), nullptr, "The group id `" + itos(p_group_id) + "` doesn't exist.");
	return &sync_groups[p_group_id].peers;
}

void ServerSynchronizer::sync_group_set_deferred_update_rate(NetUtility::NodeData *p_node_data, SyncGroupId p_group_id, real_t p_update_rate) {
	ERR_FAIL_COND(p_node_data == nullptr);
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SceneSynchronizer::GLOBAL_SYNC_GROUP_ID, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id].set_deferred_update_rate(p_node_data, p_update_rate);
}

real_t ServerSynchronizer::sync_group_get_deferred_update_rate(const NetUtility::NodeData *p_node_data, SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V(p_node_data == nullptr, 0.0);
	ERR_FAIL_COND_V_MSG(p_group_id >= sync_groups.size(), 0.0, "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_V_MSG(p_group_id == SceneSynchronizer::GLOBAL_SYNC_GROUP_ID, 0.0, "You can't change this SyncGroup in any way. Create a new one.");
	return sync_groups[p_group_id].get_deferred_update_rate(p_node_data);
}

void ServerSynchronizer::sync_group_set_user_data(SyncGroupId p_group_id, uint64_t p_user_data) {
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	sync_groups[p_group_id].user_data = p_user_data;
}

uint64_t ServerSynchronizer::sync_group_get_user_data(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(p_group_id >= sync_groups.size(), 0, "The group id `" + itos(p_group_id) + "` doesn't exist.");
	return sync_groups[p_group_id].user_data;
}

void ServerSynchronizer::sync_group_debug_print() {
	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "");
	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|-----------------------");
	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "| Sync groups");
	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|-----------------------");

	for (int g = 0; g < int(sync_groups.size()); ++g) {
		NetUtility::SyncGroup &group = sync_groups[g];

		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "| [Group " + itos(g) + "#]");
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|    Listening peers");
		for (int peer : group.peers) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|      |- " + itos(peer));
		}

		const LocalVector<NetUtility::SyncGroup::RealtimeNodeInfo> &realtime_node_info = group.get_realtime_sync_nodes();
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|");
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|    [Realtime nodes]");
		for (auto info : realtime_node_info) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|      |- " + String(info.nd->object_name.c_str()));
		}

		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|");

		const LocalVector<NetUtility::SyncGroup::DeferredNodeInfo> &deferred_node_info = group.get_deferred_sync_nodes();
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|    [Deferred nodes (UR: Update Rate)]");
		for (auto info : deferred_node_info) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|      |- [UR: " + rtos(info.update_rate) + "] " + info.nd->object_name.c_str());
		}
	}
	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|-----------------------");
	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "");
}

void ServerSynchronizer::process_snapshot_notificator(real_t p_delta) {
	if (scene_synchronizer->peer_data.is_empty()) {
		// No one is listening.
		return;
	}

	for (int g = 0; g < int(sync_groups.size()); ++g) {
		NetUtility::SyncGroup &group = sync_groups[g];

		if (group.peers.size() == 0) {
			// No one is interested to this group.
			continue;
		}

		// Notify the state if needed
		group.state_notifier_timer += p_delta;
		const bool notify_state = group.state_notifier_timer >= scene_synchronizer->get_server_notify_state_interval();

		if (notify_state) {
			group.state_notifier_timer = 0.0;
		}

		Vector<Variant> full_global_nodes_snapshot;
		Vector<Variant> delta_global_nodes_snapshot;

		for (int pi = 0; pi < int(group.peers.size()); ++pi) {
			const int peer_id = group.peers[pi];
			NetUtility::PeerData *peer = scene_synchronizer->peer_data.lookup_ptr(peer_id);
			if (peer == nullptr) {
				ERR_PRINT("The `process_snapshot_notificator` failed to lookup the peer_id `" + itos(peer_id) + "`. Was it removed but never cleared from sync_groups. Report this error, as this is a bug.");
				continue;
			}

			if (peer->force_notify_snapshot == false && notify_state == false) {
				// Nothing to sync.
				continue;
			}

			peer->force_notify_snapshot = false;

			Vector<Variant> snap;

			NetUtility::NodeData *nd = scene_synchronizer->get_node_data(peer->controller_id);

			if (nd) {
				CRASH_COND_MSG(nd->controller == nullptr, "The NodeData fetched is not a controller: `" + String(nd->object_name.c_str()) + "`, this is not supposed to happen.");

				// Add the controller input id at the beginning of the snapshot.
				snap.push_back(true);
				NetworkedController *controller = nd->controller;
				snap.push_back(controller->get_current_input_id());
			} else {
				// No `input_id`.
				snap.push_back(false);
			}

			if (peer->need_full_snapshot) {
				peer->need_full_snapshot = false;
				if (full_global_nodes_snapshot.size() == 0) {
					full_global_nodes_snapshot = generate_snapshot(true, group);
				}
				snap.append_array(full_global_nodes_snapshot);

			} else {
				if (delta_global_nodes_snapshot.size() == 0) {
					delta_global_nodes_snapshot = generate_snapshot(false, group);
				}
				snap.append_array(delta_global_nodes_snapshot);
			}

			scene_synchronizer->network_interface->rpc(
					scene_synchronizer->rpc_handler_state,
					peer_id,
					snap);

			if (nd) {
				NetworkedController *controller = nd->controller;
				controller->get_server_controller()->notify_send_state();
			}
		}

		if (notify_state) {
			// The state got notified, mark this as checkpoint so the next state
			// will contains only the changed variables.
			group.mark_changes_as_notified();
		}
	}
}

Vector<Variant> ServerSynchronizer::generate_snapshot(
		bool p_force_full_snapshot,
		const NetUtility::SyncGroup &p_group) const {
	const LocalVector<NetUtility::SyncGroup::RealtimeNodeInfo> &relevant_node_data = p_group.get_realtime_sync_nodes();

	Vector<Variant> snapshot_data;

	// First insert the list of ALL enabled nodes, if changed.
	if (p_group.is_realtime_node_list_changed() || p_force_full_snapshot) {
		snapshot_data.push_back(true);
		// Here we create a bit array: The bit position is significant as it
		// refers to a specific ID, the bit is set to 1 if the Node is relevant
		// to this group.
		BitArray bit_array;
		bit_array.resize_in_bits(scene_synchronizer->organized_node_data.size());
		bit_array.zero();
		for (uint32_t i = 0; i < relevant_node_data.size(); i += 1) {
			const NetUtility::NodeData *nd = relevant_node_data[i].nd;
			CRASH_COND(nd->id == NetID_NONE);
			bit_array.store_bits(nd->id, 1, 1);
		}
		snapshot_data.push_back(bit_array.get_bytes());
	} else {
		snapshot_data.push_back(false);
	}

	// Calling this function to allow customize the snapshot per group.
	scene_synchronizer->synchronizer_manager->snapshot_add_custom_data(&p_group, snapshot_data);

	if (p_group.is_deferred_node_list_changed() || p_force_full_snapshot) {
		for (int i = 0; i < int(p_group.get_deferred_sync_nodes().size()); ++i) {
			if (p_group.get_deferred_sync_nodes()[i]._unknown || p_force_full_snapshot) {
				generate_snapshot_node_data(
						p_group.get_deferred_sync_nodes()[i].nd,
						SNAPSHOT_GENERATION_MODE_FORCE_NODE_PATH_ONLY,
						NetUtility::SyncGroup::Change(),
						snapshot_data);
			}
		}
	}

	const SnapshotGenerationMode mode = p_force_full_snapshot ? SNAPSHOT_GENERATION_MODE_FORCE_FULL : SNAPSHOT_GENERATION_MODE_NORMAL;

	// Then, generate the snapshot for the relevant nodes.
	for (uint32_t i = 0; i < relevant_node_data.size(); i += 1) {
		const NetUtility::NodeData *node_data = relevant_node_data[i].nd;

		if (node_data != nullptr) {
			generate_snapshot_node_data(
					node_data,
					mode,
					relevant_node_data[i].change,
					snapshot_data);
		}
	}

	return snapshot_data;
}

void ServerSynchronizer::generate_snapshot_node_data(
		const NetUtility::NodeData *p_node_data,
		SnapshotGenerationMode p_mode,
		const NetUtility::SyncGroup::Change &p_change,
		Vector<Variant> &r_snapshot_data) const {
	// The packet data is an array that contains the informations to update the
	// client snapshot.
	//
	// It's composed as follows:
	//  [NODE, VARIABLE, Value, VARIABLE, Value, VARIABLE, value, NIL,
	//  NODE, INPUT ID, VARIABLE, Value, VARIABLE, Value, NIL,
	//  NODE, VARIABLE, Value, VARIABLE, Value, NIL]
	//
	// Each node ends with a NIL, and the NODE and the VARIABLE are special:
	// - NODE, can be an array of two variables [Net Node ID, NodePath] or directly
	//         a Node ID. Obviously the array is sent only the first time.
	// - INPUT ID, this is optional and is used only when the node is a controller.
	// - VARIABLE, can be an array with the ID and the variable name, or just
	//              the ID; similarly as is for the NODE the array is send only
	//              the first time.

	if (p_node_data->app_object == nullptr) {
		return;
	}

	const bool force_using_node_path = p_mode == SNAPSHOT_GENERATION_MODE_FORCE_FULL || p_mode == SNAPSHOT_GENERATION_MODE_FORCE_NODE_PATH_ONLY || p_mode == SNAPSHOT_GENERATION_MODE_NODE_PATH_ONLY;
	const bool force_snapshot_node_path = p_mode == SNAPSHOT_GENERATION_MODE_FORCE_FULL || p_mode == SNAPSHOT_GENERATION_MODE_FORCE_NODE_PATH_ONLY;
	const bool force_snapshot_variables = p_mode == SNAPSHOT_GENERATION_MODE_FORCE_FULL;
	const bool skip_snapshot_variables = p_mode == SNAPSHOT_GENERATION_MODE_FORCE_NODE_PATH_ONLY || p_mode == SNAPSHOT_GENERATION_MODE_NODE_PATH_ONLY;
	const bool force_using_variable_name = p_mode == SNAPSHOT_GENERATION_MODE_FORCE_FULL;

	const bool unknown = p_change.unknown;
	const bool node_has_changes = p_change.vars.is_empty() == false;

	// Insert NODE DATA.
	Variant snap_node_data;
	if (force_using_node_path || unknown) {
		Vector<Variant> _snap_node_data;
		_snap_node_data.resize(2);
		_snap_node_data.write[0] = p_node_data->id;
		_snap_node_data.write[1] = p_node_data->object_name.c_str();
		snap_node_data = _snap_node_data;
	} else {
		// This node is already known on clients, just set the node ID.
		snap_node_data = p_node_data->id;
	}

	if ((node_has_changes && skip_snapshot_variables == false) || force_snapshot_node_path || unknown) {
		r_snapshot_data.push_back(snap_node_data);
	} else {
		// It has no changes, skip this node.
		return;
	}

	if (force_snapshot_variables || (node_has_changes && skip_snapshot_variables == false)) {
		// Insert the node variables.
		for (uint32_t i = 0; i < p_node_data->vars.size(); i += 1) {
			const NetUtility::VarData &var = p_node_data->vars[i];
			if (var.enabled == false) {
				continue;
			}

			if (force_snapshot_variables == false && p_change.vars.has(var.var.name) == false) {
				// This is a delta snapshot and this variable is the same as
				// before. Skip it.
				continue;
			}

			Variant var_info;
			if (force_using_variable_name || p_change.uknown_vars.has(var.var.name)) {
				Vector<Variant> _var_info;
				_var_info.resize(2);
				_var_info.write[0] = var.id;
				_var_info.write[1] = var.var.name;
				var_info = _var_info;
			} else {
				var_info = var.id;
			}

			r_snapshot_data.push_back(var_info);
			r_snapshot_data.push_back(var.var.value);
		}
	}

	// Insert NIL.
	r_snapshot_data.push_back(Variant());
}

void ServerSynchronizer::process_deferred_sync(real_t p_delta) {
	DataBuffer *tmp_buffer = memnew(DataBuffer);
	const Variant var_data_buffer = tmp_buffer;
	const Variant *fake_array_vars = &var_data_buffer;

	Variant r;

	for (int g = 0; g < int(sync_groups.size()); ++g) {
		NetUtility::SyncGroup &group = sync_groups[g];

		if (group.peers.size() == 0) {
			// No one is interested to this group.
			continue;
		}

		LocalVector<NetUtility::SyncGroup::DeferredNodeInfo> &node_info = group.get_deferred_sync_nodes();
		if (node_info.size() == 0) {
			// Nothing to sync.
			continue;
		}

		int update_node_count = 0;

		group.sort_deferred_node_by_update_priority();

		DataBuffer global_buffer;
		global_buffer.begin_write(0);
		global_buffer.add_uint(epoch, DataBuffer::COMPRESSION_LEVEL_1);

		for (int i = 0; i < int(node_info.size()); ++i) {
			bool send = true;
			if (node_info[i]._update_priority < 1.0 || update_node_count >= scene_synchronizer->max_deferred_nodes_per_update) {
				send = false;
			}

			if (node_info[i].nd->id > UINT16_MAX) {
				SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "[FATAL] The `process_deferred_sync` found a node with ID `" + itos(node_info[i].nd->id) + "::" + node_info[i].nd->object_name.c_str() + "` that exceedes the max ID this function can network at the moment. Please report this, we will consider improving this function.");
				send = false;
			}

			if (node_info[i].nd->collect_epoch_func.is_null()) {
				SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The `process_deferred_sync` found a node `" + itos(node_info[i].nd->id) + "::" + node_info[i].nd->object_name.c_str() + "` with an invalid function `collect_epoch_func`. Please use `setup_deferred_sync` to correctly initialize this node for deferred sync.");
				send = false;
			}

			if (send) {
				node_info[i]._update_priority = 0.0;

				// Read the state and write into the tmp_buffer:
				tmp_buffer->begin_write(0);

				Callable::CallError e;
				node_info[i].nd->collect_epoch_func.callp(&fake_array_vars, 1, r, e);

				if (e.error != Callable::CallError::CALL_OK) {
					SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The `process_deferred_sync` was not able to execute the function `" + node_info[i].nd->collect_epoch_func.get_method() + "` for the node `" + itos(node_info[i].nd->id) + "::" + node_info[i].nd->object_name.c_str() + "`.");
					continue;
				}

				if (tmp_buffer->total_size() > UINT16_MAX) {
					SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The `process_deferred_sync` failed because the method `" + node_info[i].nd->collect_epoch_func.get_method() + "` for the node `" + itos(node_info[i].nd->id) + "::" + node_info[i].nd->object_name.c_str() + "` collected more than " + itos(UINT16_MAX) + " bits. Please optimize your netcode to send less data.");
					continue;
				}

				++update_node_count;

				if (node_info[i].nd->id > UINT8_MAX) {
					global_buffer.add_bool(true);
					global_buffer.add_uint(node_info[i].nd->id, DataBuffer::COMPRESSION_LEVEL_2);
				} else {
					global_buffer.add_bool(false);
					global_buffer.add_uint(node_info[i].nd->id, DataBuffer::COMPRESSION_LEVEL_3);
				}

				// Collapse the two DataBuffer.
				global_buffer.add_uint(uint32_t(tmp_buffer->total_size()), DataBuffer::COMPRESSION_LEVEL_2);
				global_buffer.add_bits(tmp_buffer->get_buffer().get_bytes(), tmp_buffer->total_size());

			} else {
				node_info[i]._update_priority += node_info[i].update_rate;
			}
		}

		if (update_node_count > 0) {
			global_buffer.dry();
			for (int i = 0; i < int(group.peers.size()); ++i) {
				scene_synchronizer->network_interface->rpc(
						scene_synchronizer->rpc_handler_deferred_sync_data,
						group.peers[i],
						global_buffer.get_buffer().get_bytes());
			}
		}
	}

	memdelete(tmp_buffer);
}

ClientSynchronizer::ClientSynchronizer(SceneSynchronizer *p_node) :
		Synchronizer(p_node) {
	clear();

	notify_server_full_snapshot_is_needed();
}

void ClientSynchronizer::clear() {
	player_controller_node_data = nullptr;
	node_paths.clear();
	last_received_snapshot.input_id = UINT32_MAX;
	last_received_snapshot.node_vars.clear();
	client_snapshots.clear();
	server_snapshots.clear();
	last_checked_input = 0;
	enabled = true;
	need_full_snapshot_notified = false;
}

void ClientSynchronizer::process() {
	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "ClientSynchronizer::process", true);

	if (unlikely(player_controller_node_data == nullptr || enabled == false)) {
		// No player controller or disabled so nothing to do.
		return;
	}

	const double physics_ticks_per_second = Engine::get_singleton()->get_physics_ticks_per_second();
	const double delta = 1.0 / physics_ticks_per_second;

#ifdef DEBUG_ENABLED
	if (unlikely(Engine::get_singleton()->get_frames_per_second() < physics_ticks_per_second)) {
		const bool silent = !ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/debugger/log_debug_fps_warnings");
		SceneSynchronizerDebugger::singleton()->debug_warning(&scene_synchronizer->get_network_interface(), "Current FPS is " + itos(Engine::get_singleton()->get_frames_per_second()) + ", but the minimum required FPS is " + itos(physics_ticks_per_second) + ", the client is unable to generate enough inputs for the server.", silent);
	}
#endif

	NetworkedController *controller = player_controller_node_data->controller;
	PlayerController *player_controller = controller->get_player_controller();

	// Reset this here, so even when `sub_ticks` is zero (and it's not
	// updated due to process is not called), we can still have the corect
	// data.
	controller->player_set_has_new_input(false);

	// Due to some lag we may want to speed up the input_packet
	// generation, for this reason here I'm performing a sub tick.
	//
	// keep in mind that we are just pretending that the time
	// is advancing faster, for this reason we are still using
	// `delta` to step the controllers_node_data.
	//
	// The dolls may want to speed up too, so to consume the inputs faster
	// and get back in time with the server.
	int sub_ticks = player_controller->calculates_sub_ticks(delta, physics_ticks_per_second);

	if (sub_ticks == 0) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "No sub ticks: this is not bu a bug; it's the lag compensation algorithm.", true);
	}

	while (sub_ticks > 0) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "ClientSynchronizer::process::sub_process " + itos(sub_ticks), true);
		SceneSynchronizerDebugger::singleton()->scene_sync_process_start(scene_synchronizer);

		// Process the scene.
		scene_synchronizer->process_functions__execute(delta);

		scene_synchronizer->detect_and_signal_changed_variables(NetEventFlag::CHANGE);

		if (controller->player_has_new_input()) {
			store_snapshot();
		}

		sub_ticks -= 1;
		SceneSynchronizerDebugger::singleton()->scene_sync_process_end(scene_synchronizer);

#if DEBUG_ENABLED
		if (sub_ticks > 0) {
			// This is an intermediate sub tick, so store the dumping.
			// The last sub frame is not dumped, untile the end of the frame, so we can capture any subsequent message.
			const int client_peer = scene_synchronizer->network_interface->fetch_local_peer_id();
			SceneSynchronizerDebugger::singleton()->write_dump(client_peer, player_controller->get_current_input_id());
			SceneSynchronizerDebugger::singleton()->start_new_frame();
		}
#endif
	}

	process_controllers_recovery(delta);

	// Now trigger the END_SYNC event.
	signal_end_sync_changed_variables_events();

	process_received_deferred_sync_data(delta);

#if DEBUG_ENABLED
	const int client_peer = scene_synchronizer->network_interface->fetch_local_peer_id();
	SceneSynchronizerDebugger::singleton()->write_dump(client_peer, player_controller->get_current_input_id());
	SceneSynchronizerDebugger::singleton()->start_new_frame();
#endif
}

void ClientSynchronizer::receive_snapshot(Variant p_snapshot) {
	// The received snapshot is parsed and stored into the `last_received_snapshot`
	// that contains always the last received snapshot.
	// Later, the snapshot is stored into the server queue.
	// In this way, we are free to pop snapshot from the queue without wondering
	// about losing the data. Indeed the received snapshot is just and
	// incremental update so the last received data is always needed to fully
	// reconstruct it.

	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The Client received the server snapshot.", true);

	// Parse server snapshot.
	const bool success = parse_snapshot(p_snapshot);

	if (success == false) {
		return;
	}

	// Finalize data.

	store_controllers_snapshot(
			last_received_snapshot,
			server_snapshots);
}

void ClientSynchronizer::on_node_added(NetUtility::NodeData *p_node_data) {
}

void ClientSynchronizer::on_node_removed(NetUtility::NodeData *p_node_data) {
	if (player_controller_node_data == p_node_data) {
		player_controller_node_data = nullptr;
		server_snapshots.clear();
		client_snapshots.clear();
	}

	if (p_node_data->id < uint32_t(last_received_snapshot.node_vars.size())) {
		last_received_snapshot.node_vars.ptrw()[p_node_data->id].clear();
	}

	remove_node_from_deferred_sync(p_node_data);
}

void ClientSynchronizer::on_variable_changed(NetUtility::NodeData *p_node_data, NetVarId p_var_id, const Variant &p_old_value, int p_flag) {
	if (p_flag & NetEventFlag::SYNC) {
		sync_end_events.insert(
				EndSyncEvent{
						p_node_data,
						p_var_id,
						p_old_value });
	}
}

void ClientSynchronizer::signal_end_sync_changed_variables_events() {
	scene_synchronizer->change_events_begin(NetEventFlag::END_SYNC);
	for (const RBSet<EndSyncEvent>::Element *e = sync_end_events.front();
			e != nullptr;
			e = e->next()) {
		// Check if the values between the variables before the sync and the
		// current one are different.
		if (scene_synchronizer->compare(
					e->get().node_data->vars[e->get().var_id].var.value,
					e->get().old_value) == false) {
			// Are different so we need to emit the `END_SYNC`.
			scene_synchronizer->change_event_add(
					e->get().node_data,
					e->get().var_id,
					e->get().old_value);
		}
	}
	sync_end_events.clear();

	scene_synchronizer->change_events_flush();
}

void ClientSynchronizer::on_controller_reset(NetUtility::NodeData *p_node_data) {
#ifdef DEBUG_ENABLED
	CRASH_COND(p_node_data->controller == nullptr);
#endif

	if (player_controller_node_data == p_node_data) {
		// Reset the node_data.
		player_controller_node_data = nullptr;
		server_snapshots.clear();
		client_snapshots.clear();
	}

	if (p_node_data->controller->is_player_controller()) {
		if (player_controller_node_data != nullptr) {
			SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "Only one player controller is supported, at the moment. Make sure this is the case.");
		} else {
			// Set this player controller as active.
			player_controller_node_data = p_node_data;
			server_snapshots.clear();
			client_snapshots.clear();
		}
	}
}

void ClientSynchronizer::store_snapshot() {
	NetworkedController *controller = player_controller_node_data->controller;

#ifdef DEBUG_ENABLED
	if (unlikely(client_snapshots.size() > 0 && controller->get_current_input_id() <= client_snapshots.back().input_id)) {
		CRASH_NOW_MSG("[FATAL] During snapshot creation, for controller " + String(player_controller_node_data->object_name.c_str()) + ", was found an ID for an older snapshots. New input ID: " + itos(controller->get_current_input_id()) + " Last saved snapshot input ID: " + itos(client_snapshots.back().input_id) + ".");
	}
#endif

	client_snapshots.push_back(NetUtility::Snapshot());

	NetUtility::Snapshot &snap = client_snapshots.back();
	snap.input_id = controller->get_current_input_id();

	update_client_snapshot(snap);
}

void ClientSynchronizer::store_controllers_snapshot(
		const NetUtility::Snapshot &p_snapshot,
		std::deque<NetUtility::Snapshot> &r_snapshot_storage) {
	// Put the parsed snapshot into the queue.

	if (p_snapshot.input_id == UINT32_MAX && player_controller_node_data != nullptr) {
		// The snapshot doesn't have any info for this controller; Skip it.
		return;
	}

	if (p_snapshot.input_id == UINT32_MAX) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The Client received the server snapshot WITHOUT `input_id`.", true);
		// The controller node is not registered so just assume this snapshot is the most up-to-date.
		r_snapshot_storage.clear();
		r_snapshot_storage.push_back(p_snapshot);

	} else {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The Client received the server snapshot: " + itos(p_snapshot.input_id), true);

		// Store the snapshot sorted by controller input ID.
		if (r_snapshot_storage.empty() == false) {
			// Make sure the snapshots are stored in order.
			const uint32_t last_stored_input_id = r_snapshot_storage.back().input_id;
			if (p_snapshot.input_id == last_stored_input_id) {
				// Update the snapshot.
				r_snapshot_storage.back() = p_snapshot;
				return;
			} else {
				ERR_FAIL_COND_MSG(p_snapshot.input_id < last_stored_input_id, "This snapshot (with ID: " + itos(p_snapshot.input_id) + ") is not expected because the last stored id is: " + itos(last_stored_input_id));
			}
		}

		r_snapshot_storage.push_back(p_snapshot);
	}
}

void ClientSynchronizer::process_controllers_recovery(real_t p_delta) {
	// The client is responsible to recover only its local controller, while all
	// the other controllers_node_data (dolls) have their state interpolated. There is
	// no need to check the correctness of the doll state nor the needs to
	// rewind those.
	//
	// The scene, (global nodes), are always in sync with the reference frame
	// of the client.

	NetworkedController *controller = player_controller_node_data->controller;
	PlayerController *player_controller = controller->get_player_controller();

	// --- Phase one: find the snapshot to check. ---
	if (server_snapshots.empty()) {
		// No snapshots to recover for this controller. Nothing to do.
		return;
	}

	if (server_snapshots.back().input_id == UINT32_MAX) {
		// The server last received snapshot is a no input snapshot. Just assume it's the most up-to-date.
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The client received a \"no input\" snapshot, so the client is setting it right away assuming is the most updated one.", true);

		apply_snapshot(server_snapshots.back(), NetEventFlag::SYNC_RECOVER, nullptr);

		server_snapshots.clear();
		client_snapshots.clear();
		return;
	}

#ifdef DEBUG_ENABLED
	if (client_snapshots.empty() == false) {
		// The SceneSynchronizer and the PlayerController are always in sync.
		CRASH_COND_MSG(client_snapshots.back().input_id != player_controller->last_known_input(), "This should not be possible: snapshot input: " + itos(client_snapshots.back().input_id) + " last_know_input: " + itos(player_controller->last_known_input()));
	}
#endif

	// Find the best recoverable input_id.
	uint32_t checkable_input_id = UINT32_MAX;
	// Find the best snapshot to recover from the one already
	// processed.
	if (client_snapshots.empty() == false) {
		for (
				auto s_snap = server_snapshots.rbegin();
				checkable_input_id == UINT32_MAX && s_snap != server_snapshots.rend();
				++s_snap) {
			for (auto c_snap = client_snapshots.begin(); c_snap != client_snapshots.end(); ++c_snap) {
				if (c_snap->input_id == s_snap->input_id) {
					// Server snapshot also found on client, can be checked.
					checkable_input_id = c_snap->input_id;
					break;
				}
			}
		}
	} else {
		// No client input, this happens when the stream is paused.
		process_paused_controller_recovery(p_delta);
		return;
	}

	if (checkable_input_id == UINT32_MAX) {
		// No snapshot found, nothing to do.
		return;
	}

#ifdef DEBUG_ENABLED
	// Unreachable cause the above check
	CRASH_COND(server_snapshots.empty());
	CRASH_COND(client_snapshots.empty());
#endif

	// Drop all the old server snapshots until the one that we need.
	while (server_snapshots.front().input_id < checkable_input_id) {
		server_snapshots.pop_front();
	}

	// Drop all the old client snapshots until the one that we need.
	while (client_snapshots.front().input_id < checkable_input_id) {
		client_snapshots.pop_front();
	}

#ifdef DEBUG_ENABLED
	// These are unreachable at this point.
	CRASH_COND(server_snapshots.empty());
	CRASH_COND(server_snapshots.front().input_id != checkable_input_id);

	// This is unreachable, because we store all the client shapshots
	// each time a new input is processed. Since the `checkable_input_id`
	// is taken by reading the processed doll inputs, it's guaranteed
	// that here the snapshot exists.
	CRASH_COND(client_snapshots.empty());
	CRASH_COND(client_snapshots.front().input_id != checkable_input_id);
#endif

	// --- Phase two: compare the server snapshot with the client snapshot. ---
	NetUtility::Snapshot no_rewind_recover;

	const bool need_rewind = __pcr__fetch_recovery_info(
			checkable_input_id,
			no_rewind_recover);

	// Popout the client snapshot.
	client_snapshots.pop_front();

	// --- Phase three: recover and rewind. ---

	if (need_rewind) {
		SceneSynchronizerDebugger::singleton()->notify_event(SceneSynchronizerDebugger::FrameEvent::CLIENT_DESYNC_DETECTED);
		SceneSynchronizerDebugger::singleton()->add_node_message(
				scene_synchronizer->get_network_interface().get_name(),
				"Recover input: " + itos(checkable_input_id) + " - Last input: " + itos(player_controller->get_stored_input_id(-1)));

		// Sync.
		__pcr__sync__rewind();

		// Rewind.
		__pcr__rewind(
				p_delta,
				checkable_input_id,
				player_controller_node_data,
				controller,
				player_controller);
	} else {
		if (no_rewind_recover.input_id == 0) {
			SceneSynchronizerDebugger::singleton()->notify_event(SceneSynchronizerDebugger::FrameEvent::CLIENT_DESYNC_DETECTED_SOFT);

			// Sync.
			__pcr__sync__no_rewind(no_rewind_recover);
		}

		// No rewind.
		__pcr__no_rewind(checkable_input_id, player_controller);
	}

	// Popout the server snapshot.
	server_snapshots.pop_front();

	last_checked_input = checkable_input_id;
}

bool ClientSynchronizer::__pcr__fetch_recovery_info(
		const uint32_t p_input_id,
		NetUtility::Snapshot &r_no_rewind_recover) {
	LocalVector<String> differences_info;

#ifdef DEBUG_ENABLED
	LocalVector<NetNodeId> different_node_data;
	const bool is_equal = NetUtility::Snapshot::compare(
			*scene_synchronizer,
			server_snapshots.front(),
			client_snapshots.front(),
			&r_no_rewind_recover,
			scene_synchronizer->debug_rewindings_enabled ? &differences_info : nullptr,
			&different_node_data);

	if (!is_equal) {
		Vector<StringName> variable_names;
		Vector<Variant> server_values;
		Vector<Variant> client_values;
		const Vector<NetUtility::Var> const_empty_vector;

		// Emit the de-sync detected signal.
		for (
				int i = 0;
				i < int(different_node_data.size());
				i += 1) {
			const NetNodeId net_node_id = different_node_data[i];
			NetUtility::NodeData *rew_node_data = scene_synchronizer->get_node_data(net_node_id);

			const Vector<NetUtility::Var> &server_node_vars = uint32_t(server_snapshots.front().node_vars.size()) <= net_node_id ? const_empty_vector : server_snapshots.front().node_vars[net_node_id];
			const Vector<NetUtility::Var> &client_node_vars = uint32_t(client_snapshots.front().node_vars.size()) <= net_node_id ? const_empty_vector : client_snapshots.front().node_vars[net_node_id];

			const int count = MAX(server_node_vars.size(), client_node_vars.size());

			variable_names.resize(count);
			server_values.resize(count);
			client_values.resize(count);

			for (int g = 0; g < count; ++g) {
				if (g < server_node_vars.size()) {
					variable_names.ptrw()[g] = server_node_vars[g].name;
					server_values.ptrw()[g] = server_node_vars[g].value;
				} else {
					server_values.ptrw()[g] = Variant();
				}

				if (g < client_node_vars.size()) {
					variable_names.ptrw()[g] = client_node_vars[g].name;
					client_values.ptrw()[g] = client_node_vars[g].value;
				} else {
					client_values.ptrw()[g] = Variant();
				}
			}

			scene_synchronizer->event_desync_detected.broadcast(p_input_id, rew_node_data->app_object, variable_names, client_values, server_values);
		}
	}
#else
	const bool is_equal = NetUtility::Snapshot::compare(
			*scene_synchronizer,
			server_snapshots.front(),
			client_snapshots.front(),
			&r_no_rewind_recover,
			scene_synchronizer->debug_rewindings_enabled ? &differences_info : nullptr);
#endif

	// Prints the comparison info.
	if (differences_info.size() > 0 && scene_synchronizer->debug_rewindings_enabled) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "Rewind on frame " + itos(p_input_id) + " is needed because:");
		for (int i = 0; i < int(differences_info.size()); i++) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|- " + differences_info[i]);
		}
	}

	return !is_equal;
}

void ClientSynchronizer::__pcr__sync__rewind() {
	// Apply the server snapshot so to go back in time till that moment,
	// so to be able to correctly reply the movements.

	LocalVector<String> applied_data_info;

	const NetUtility::Snapshot &server_snapshot = server_snapshots.front();
	apply_snapshot(
			server_snapshot,
			NetEventFlag::SYNC_RECOVER | NetEventFlag::SYNC_RESET,
			scene_synchronizer->debug_rewindings_enabled ? &applied_data_info : nullptr);

	if (applied_data_info.size() > 0) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "Full reset:");
		for (int i = 0; i < int(applied_data_info.size()); i++) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|- " + applied_data_info[i]);
		}
	}
}

void ClientSynchronizer::__pcr__rewind(
		real_t p_delta,
		const uint32_t p_checkable_input_id,
		NetUtility::NodeData *p_local_controller_node,
		NetworkedController *p_local_controller,
		PlayerController *p_local_player_controller) {
	scene_synchronizer->event_state_validated.broadcast(p_checkable_input_id);
	const int remaining_inputs = p_local_player_controller->get_frames_input_count();

#ifdef DEBUG_ENABLED
	// Unreachable because the SceneSynchronizer and the PlayerController
	// have the same stored data at this point.
	CRASH_COND_MSG(client_snapshots.size() != size_t(remaining_inputs), "Beware that `client_snapshots.size()` (" + itos(client_snapshots.size()) + ") and `remaining_inputs` (" + itos(remaining_inputs) + ") should be the same.");
#endif

#ifdef DEBUG_ENABLED
	// Used to double check all the instants have been processed.
	bool has_next = false;
#endif
	for (int i = 0; i < remaining_inputs; i += 1) {
		scene_synchronizer->change_events_begin(NetEventFlag::SYNC_RECOVER | NetEventFlag::SYNC_REWIND);

		// Step 1 -- Notify the local controller about the instant to process
		//           on the next process.
		scene_synchronizer->event_rewind_frame_begin.broadcast(p_local_player_controller->get_stored_input_id(i), i, remaining_inputs);
#ifdef DEBUG_ENABLED
		has_next = p_local_controller->has_another_instant_to_process_after(i);
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "Rewind, processed controller: " + String(p_local_controller_node->object_name.c_str()), !scene_synchronizer->debug_rewindings_enabled);
#endif

		// Step 2 -- Process the scene.
		scene_synchronizer->process_functions__execute(p_delta);

		// Step 3 -- Pull node changes.
		scene_synchronizer->detect_and_signal_changed_variables(NetEventFlag::SYNC_RECOVER | NetEventFlag::SYNC_REWIND);

		// Step 4 -- Update snapshots.
		update_client_snapshot(client_snapshots[i]);
	}

#ifdef DEBUG_ENABLED
	// Unreachable because the above loop consume all instants, so the last
	// process will set this to false.
	CRASH_COND(has_next);
#endif
}

void ClientSynchronizer::__pcr__sync__no_rewind(const NetUtility::Snapshot &p_no_rewind_recover) {
	CRASH_COND_MSG(p_no_rewind_recover.input_id != 0, "This function is never called unless there is something to recover without rewinding.");

	// Apply found differences without rewind.
	LocalVector<String> applied_data_info;

	apply_snapshot(
			p_no_rewind_recover,
			NetEventFlag::SYNC_RECOVER,
			scene_synchronizer->debug_rewindings_enabled ? &applied_data_info : nullptr,
			// ALWAYS skips custom data because partial snapshots don't contain custom_data.
			true);

	if (applied_data_info.size() > 0) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "Partial reset:");
		for (int i = 0; i < int(applied_data_info.size()); i++) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|- " + applied_data_info[i]);
		}
	}

	// Update the last client snapshot.
	if (!client_snapshots.empty()) {
		update_client_snapshot(client_snapshots.back());
	}
}

void ClientSynchronizer::__pcr__no_rewind(
		const uint32_t p_checkable_input_id,
		PlayerController *p_player_controller) {
	scene_synchronizer->event_state_validated.broadcast(p_checkable_input_id);
}

void ClientSynchronizer::process_paused_controller_recovery(real_t p_delta) {
#ifdef DEBUG_ENABLED
	CRASH_COND(server_snapshots.empty());
	CRASH_COND(client_snapshots.empty() == false);
#endif

	// Drop the snapshots till the newest.
	while (server_snapshots.size() != 1) {
		server_snapshots.pop_front();
	}

#ifdef DEBUG_ENABLED
	CRASH_COND(server_snapshots.empty());
#endif
	LocalVector<String> applied_data_info;

	apply_snapshot(
			server_snapshots.front(),
			NetEventFlag::SYNC_RECOVER,
			&applied_data_info);

	server_snapshots.pop_front();

	if (applied_data_info.size() > 0) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "Paused controller recover:");
		for (int i = 0; i < int(applied_data_info.size()); i++) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|- " + applied_data_info[i]);
		}
	}
}

bool ClientSynchronizer::parse_sync_data(
		Variant p_sync_data,
		void *p_user_pointer,
		void (*p_custom_data_parse)(void *p_user_pointer, const LocalVector<const Variant *> &p_custom_data),
		void (*p_node_parse)(void *p_user_pointer, NetUtility::NodeData *p_node_data),
		void (*p_input_id_parse)(void *p_user_pointer, uint32_t p_input_id),
		void (*p_controller_parse)(void *p_user_pointer, NetUtility::NodeData *p_node_data),
		void (*p_variable_parse)(void *p_user_pointer, NetUtility::NodeData *p_node_data, uint32_t p_var_id, const Variant &p_value),
		void (*p_node_activation_parse)(void *p_user_pointer, NetUtility::NodeData *p_node_data, bool p_is_active)) {
	// The sync data is an array that contains the scene informations.
	// It's used for several things, for this reason this function allows to
	// customize the parsing.
	//
	// The data is composed as follows:
	//  [TRUE/FALSE, InputID,
	//  CUSTOM DATA (optional),
	//	NODE, VARIABLE, Value, VARIABLE, Value, VARIABLE, value, NIL,
	//  NODE, INPUT ID, VARIABLE, Value, VARIABLE, Value, NIL,
	//  NODE, VARIABLE, Value, VARIABLE, Value, NIL]
	//
	// Each node ends with a NIL, and the NODE and the VARIABLE are special:
	// - InputID: The first parameter is a boolean; when is true the following input is the `InputID`.
	// - NODE, can be an array of two variables [Node ID, NodePath] or directly
	//         a Node ID. Obviously the array is sent only the first time.
	// - INPUT ID, this is optional and is used only when the node is a controller.
	// - VARIABLE, can be an array with the ID and the variable name, or just
	//              the ID; similarly as is for the NODE the array is send only
	//              the first time.

	if (p_sync_data.get_type() == Variant::NIL) {
		// Nothing to do.
		return true;
	}

	ERR_FAIL_COND_V(!p_sync_data.is_array(), false);

	const Vector<Variant> raw_snapshot = p_sync_data;
	const Variant *raw_snapshot_ptr = raw_snapshot.ptr();

	Vector<uint8_t> active_node_list_byte_array;

	int snap_data_index = 0;

	// Fetch the `InputID`.
	ERR_FAIL_COND_V_MSG(raw_snapshot.size() < snap_data_index + 1, false, "This snapshot is corrupted as it doesn't even contains the first parameter used to specify the `InputID`.");
	ERR_FAIL_COND_V_MSG(raw_snapshot_ptr[snap_data_index].get_type() != Variant::BOOL, false, "This snapshot is corrupted as the first parameter is not a boolean.");
	const bool has_input_id = raw_snapshot_ptr[snap_data_index].operator bool();
	snap_data_index += 1;
	if (has_input_id) {
		// The InputId is set.
		ERR_FAIL_COND_V_MSG(raw_snapshot.size() < snap_data_index + 1, false, "This snapshot is corrupted as the `InputID` expected is not set.");
		ERR_FAIL_COND_V_MSG(raw_snapshot_ptr[1].get_type() != Variant::INT, false, "This snapshot is corrupted as the `InputID` set is not an INTEGER.");
		const uint32_t input_id = raw_snapshot_ptr[snap_data_index];
		p_input_id_parse(p_user_pointer, input_id);
		snap_data_index += 1;
	} else {
		p_input_id_parse(p_user_pointer, UINT32_MAX);
	}

	// Fetch `active_node_list_byte_array`.
	ERR_FAIL_COND_V_MSG(raw_snapshot.size() < snap_data_index + 1, false, "This snapshot is corrupted as it doesn't even contains the boolean to specify if the `ActiveNodeList` is set.");
	ERR_FAIL_COND_V_MSG(raw_snapshot_ptr[snap_data_index].get_type() != Variant::BOOL, false, "This snapshot is corrupted the `ActiveNodeList` parameter is not a boolean.");
	const bool has_active_list_array = raw_snapshot_ptr[snap_data_index].operator bool();
	snap_data_index += 1;
	if (has_active_list_array) {
		// Fetch the array.
		ERR_FAIL_COND_V_MSG(raw_snapshot.size() < snap_data_index + 1, false, "This snapshot is corrupted as the parameter `ActiveNodeList` is not set.");
		ERR_FAIL_COND_V_MSG(raw_snapshot_ptr[snap_data_index].get_type() != Variant::PACKED_BYTE_ARRAY, false, "This snapshot is corrupted as the ActiveNodeList` parameter is not a BYTE array.");
		active_node_list_byte_array = raw_snapshot_ptr[snap_data_index];
		snap_data_index += 1;
	}

	{
		LocalVector<const Variant *> custom_data;
		const bool cd_success = scene_synchronizer->synchronizer_manager->snapshot_extract_custom_data(raw_snapshot, snap_data_index, custom_data);
		if (!cd_success) {
			return false;
		}
		snap_data_index += custom_data.size();
		p_custom_data_parse(p_user_pointer, custom_data);
	}

	NetUtility::NodeData *synchronizer_node_data = nullptr;
	uint32_t var_id = UINT32_MAX;

	for (; snap_data_index < raw_snapshot.size(); snap_data_index += 1) {
		const Variant v = raw_snapshot_ptr[snap_data_index];
		if (synchronizer_node_data == nullptr) {
			// Node is null so we expect `v` has the node info.

			bool skip_this_node = false;
			void *app_object = nullptr;
			uint32_t net_node_id = UINT32_MAX;
			std::string object_name;

			if (v.is_array()) {
				// Node info are in verbose form, extract it.

				const Vector<Variant> node_data = v;
				ERR_FAIL_COND_V(node_data.size() != 2, false);
				ERR_FAIL_COND_V_MSG(node_data[0].get_type() != Variant::INT, false, "This snapshot is corrupted.");
				ERR_FAIL_COND_V_MSG(node_data[1].get_type() != Variant::STRING, false, "This snapshot is corrupted.");

				net_node_id = node_data[0];
				object_name = String(node_data[1]).utf8();

				// Associate the ID with the path.
				node_paths.set(net_node_id, object_name);

			} else if (v.get_type() == Variant::INT) {
				// Node info are in short form.
				net_node_id = v;
				NetUtility::NodeData *nd = scene_synchronizer->get_node_data(net_node_id);
				if (nd != nullptr) {
					synchronizer_node_data = nd;
					goto node_lookup_out;
				}
			} else {
				// The arrived snapshot does't seems to be in the expected form.
				ERR_FAIL_V_MSG(false, "This snapshot is corrupted. Now the node is expected, " + String(v) + " was submitted instead.");
			}

			if (synchronizer_node_data == nullptr) {
				if (object_name.size() > 0) {
					const std::string *object_name_ptr = node_paths.lookup_ptr(net_node_id);

					if (object_name_ptr == nullptr) {
						// Was not possible lookup the node_path.
						SceneSynchronizerDebugger::singleton()->debug_warning(&scene_synchronizer->get_network_interface(), "The node with ID `" + itos(net_node_id) + "` is not know by this peer, this is not supposed to happen.");
						notify_server_full_snapshot_is_needed();
						skip_this_node = true;
						goto node_lookup_check;
					} else {
						object_name = *object_name_ptr;
					}
				}

				app_object = scene_synchronizer->synchronizer_manager->fetch_app_object(object_name);
				if (app_object == nullptr) {
					// The node doesn't exists.
					SceneSynchronizerDebugger::singleton()->debug_warning(&scene_synchronizer->get_network_interface(), "The node " + String(object_name.c_str()) + " still doesn't exist.");
					skip_this_node = true;
					goto node_lookup_check;
				}

				// Register this node, so to make sure the client is tracking it.
				NetUtility::NodeData *nd = scene_synchronizer->register_app_object(app_object);
				if (nd != nullptr) {
					// Set the node ID.
					scene_synchronizer->set_node_data_id(nd, net_node_id);
					synchronizer_node_data = nd;
				} else {
					SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "[BUG] This node " + String(nd->object_name.c_str()) + " was not know on this client. Though, was not possible to register it.");
					skip_this_node = true;
				}
			}

		node_lookup_check:
			if (skip_this_node || synchronizer_node_data == nullptr) {
				synchronizer_node_data = nullptr;

				// This node does't exist; skip it entirely.
				for (snap_data_index += 1; snap_data_index < raw_snapshot.size(); snap_data_index += 1) {
					if (raw_snapshot_ptr[snap_data_index].get_type() == Variant::NIL) {
						break;
					}
				}

				if (!skip_this_node) {
					SceneSynchronizerDebugger::singleton()->debug_warning(&scene_synchronizer->get_network_interface(), "This NetNodeId " + itos(net_node_id) + " doesn't exist on this client.");
				}
				continue;
			}

		node_lookup_out:

#ifdef DEBUG_ENABLED
			// At this point the ID is never UINT32_MAX thanks to the above
			// mechanism.
			CRASH_COND(synchronizer_node_data->id == UINT32_MAX);
#endif

			p_node_parse(p_user_pointer, synchronizer_node_data);

			if (synchronizer_node_data->controller) {
				p_controller_parse(p_user_pointer, synchronizer_node_data);
			}

		} else if (var_id == UINT32_MAX) {
			// When the node is known and the `var_id` not, we expect a
			// new variable or the end of this node data.

			if (v.get_type() == Variant::NIL) {
				// NIL found, so this node is done.
				synchronizer_node_data = nullptr;
				continue;
			}

			// This is a new variable, so let's take the variable name.

			if (v.is_array()) {
				// The variable info are stored in verbose mode.

				const Vector<Variant> var_data = v;
				ERR_FAIL_COND_V(var_data.size() != 2, false);
				ERR_FAIL_COND_V(var_data[0].get_type() != Variant::INT, false);
				ERR_FAIL_COND_V(var_data[1].get_type() != Variant::STRING_NAME, false);

				var_id = var_data[0];
				StringName variable_name = var_data[1];

				{
					int64_t index = synchronizer_node_data->vars.find(variable_name);
					if (index == -1) {
						// The variable is not known locally, so just add it so
						// to store the variable ID.
						index = synchronizer_node_data->vars.size();

						const bool skip_rewinding = false;
						const bool enabled = false;
						synchronizer_node_data->vars
								.push_back(
										NetUtility::VarData(
												var_id,
												variable_name,
												Variant(),
												skip_rewinding,
												enabled));
						SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The variable " + variable_name + " for the node " + synchronizer_node_data->object_name.c_str() + " was not known on this client. This should never happen, make sure to register the same nodes on the client and server.");
					}

					if (index != var_id) {
						if (synchronizer_node_data[var_id].id != UINT32_MAX) {
							// It's not expected because if index is different to
							// var_id, var_id should have a not yet initialized
							// variable.
							SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "This snapshot is corrupted. The var_id, at this point, must have a not yet init variable.");
							notify_server_full_snapshot_is_needed();
							return false;
						}

						// Make sure the variable is at the right index.
						SWAP(synchronizer_node_data->vars[index], synchronizer_node_data->vars[var_id]);
					}
				}

				// Make sure the ID is properly assigned.
				synchronizer_node_data->vars[var_id].id = var_id;

			} else if (v.get_type() == Variant::INT) {
				// The variable is stored in the compact form.

				var_id = v;

				if (var_id >= synchronizer_node_data->vars.size() ||
						synchronizer_node_data->vars[var_id].id == UINT32_MAX) {
					SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The var with ID `" + itos(var_id) + "` is not know by this peer, this is not supposed to happen.");

					notify_server_full_snapshot_is_needed();

					// Skip the next data since it's the value of this variable.
					snap_data_index += 1;
					var_id = UINT32_MAX;
					continue;
				}

			} else {
				ERR_FAIL_V_MSG(false, "The snapshot received seems corrupted. The variable is expected but " + String(v) + " received instead.");
			}

		} else {
			// The node is known, also the variable name is known, so the value
			// is expected.

			p_variable_parse(
					p_user_pointer,
					synchronizer_node_data,
					var_id,
					v);

			// Just reset the variable name so we can continue iterate.
			var_id = UINT32_MAX;
		}
	}

	// Fetch the active node list, and execute the callback to notify if the
	// node is active or not.
	{
		const uint8_t *active_node_list_byte_array_ptr = active_node_list_byte_array.ptr();
		NetNodeId node_id = 0;
		for (int j = 0; j < active_node_list_byte_array.size(); ++j) {
			const uint8_t bit = active_node_list_byte_array_ptr[j];
			for (int offset = 0; offset < 8; ++offset) {
				if (node_id >= scene_synchronizer->organized_node_data.size()) {
					// This check is needed because we are iterating the full 8 bits
					// into the byte: we don't have a count where to stop.
					break;
				}
				const int bit_mask = 1 << offset;
				const bool is_active = (bit & bit_mask) > 0;
				NetUtility::NodeData *nd = scene_synchronizer->get_node_data(node_id, false);
				if (nd) {
					p_node_activation_parse(p_user_pointer, nd, is_active);
				} else {
					if (is_active) {
						// This node data doesn't exist but it should be activated.
						SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The node_data `" + itos(node_id) + "` was not found on this client but the server marked this as realtime_sync_active node. This is likely a bug, pleasae report.");
						notify_server_full_snapshot_is_needed();
					}
				}
				++node_id;
			}
		}
	}

	return true;
}

void ClientSynchronizer::set_enabled(bool p_enabled) {
	if (enabled == p_enabled) {
		// Nothing to do.
		return;
	}

	if (p_enabled) {
		// Postpone enabling when the next server snapshot is received.
		want_to_enable = true;
	} else {
		// Disabling happens immediately.
		enabled = false;
		want_to_enable = false;
		scene_synchronizer->event_sync_paused.broadcast();
	}
}

void ClientSynchronizer::receive_deferred_sync_data(const Vector<uint8_t> &p_data) {
	DataBuffer future_epoch_buffer(p_data);
	future_epoch_buffer.begin_read();

	int remaining_size = future_epoch_buffer.size() - future_epoch_buffer.get_bit_offset();
	if (remaining_size < DataBuffer::get_bit_taken(DataBuffer::DATA_TYPE_UINT, DataBuffer::COMPRESSION_LEVEL_1)) {
		SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "[FATAL] The function `receive_deferred_sync_data` received malformed data.");
		// Nothing to fetch.
		return;
	}

	const uint32_t epoch = future_epoch_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_1);

	DataBuffer *db = memnew(DataBuffer);
	Variant var_data_buffer = db;
	const Variant *fake_array_vars = &var_data_buffer;

	Variant r;

	while (true) {
		// 1. Decode the received data.
		remaining_size = future_epoch_buffer.size() - future_epoch_buffer.get_bit_offset();
		if (remaining_size < future_epoch_buffer.get_bool_size()) {
			// buffer entirely consumed, nothing else to do.
			break;
		}

		// Fetch the `node_id`.
		NetNodeId node_id;
		if (future_epoch_buffer.read_bool()) {
			remaining_size = future_epoch_buffer.size() - future_epoch_buffer.get_bit_offset();
			if (remaining_size < future_epoch_buffer.get_uint_size(DataBuffer::COMPRESSION_LEVEL_2)) {
				// buffer entirely consumed, nothing else to do.
				break;
			}

			node_id = future_epoch_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_2);
		} else {
			if (remaining_size < future_epoch_buffer.get_uint_size(DataBuffer::COMPRESSION_LEVEL_3)) {
				// buffer entirely consumed, nothing else to do.
				break;
			}
			node_id = future_epoch_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_3);
		}

		remaining_size = future_epoch_buffer.size() - future_epoch_buffer.get_bit_offset();
		if (remaining_size < future_epoch_buffer.get_uint_size(DataBuffer::COMPRESSION_LEVEL_2)) {
			// buffer entirely consumed, nothing else to do.
			break;
		}
		const int buffer_bit_count = future_epoch_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_2);

		remaining_size = future_epoch_buffer.size() - future_epoch_buffer.get_bit_offset();
		if (remaining_size < buffer_bit_count) {
			SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "[FATAL] The function `receive_deferred_sync_data` failed applying the epoch because the received buffer is malformed. The node with ID `" + itos(node_id) + "` reported that the sub buffer size is `" + itos(buffer_bit_count) + "` but the main-buffer doesn't have so many bits.");
			break;
		}

		const int current_offset = future_epoch_buffer.get_bit_offset();
		const int expected_bit_offset_after_apply = current_offset + buffer_bit_count;

		NetUtility::NodeData *nd = scene_synchronizer->get_node_data(node_id, false);
		if (nd == nullptr) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The function `receive_deferred_sync_data` is skipping the node with ID `" + itos(node_id) + "` as it was not found locally.");
			future_epoch_buffer.seek(expected_bit_offset_after_apply);
			continue;
		}

		Vector<uint8_t> future_buffer_data = future_epoch_buffer.read_bits(buffer_bit_count);
		CRASH_COND_MSG(future_epoch_buffer.get_bit_offset() != expected_bit_offset_after_apply, "At this point the buffer is expected to be exactly at this bit.");

		int64_t index = deferred_sync_array.find(nd);
		if (index == -1) {
			index = deferred_sync_array.size();
			deferred_sync_array.push_back(DeferredSyncInterpolationData(nd));
		}
		DeferredSyncInterpolationData &stream = deferred_sync_array[index];
#ifdef DEBUG_ENABLED
		CRASH_COND(stream.nd != nd);
#endif
		stream.future_epoch_buffer.copy(future_buffer_data);

		stream.past_epoch_buffer.begin_write(0);

		// 2. Now collect the past epoch buffer by reading the current values.
		db->begin_write(0);

		Callable::CallError e;
		stream.nd->collect_epoch_func.callp(&fake_array_vars, 1, r, e);

		stream.past_epoch_buffer.copy(*db);

		if (e.error != Callable::CallError::CALL_OK) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The function `receive_deferred_sync_data` is skipping the node `" + String(stream.nd->object_name.c_str()) + "` as the function `" + stream.nd->collect_epoch_func.get_method() + "` failed executing.");
			future_epoch_buffer.seek(expected_bit_offset_after_apply);
			continue;
		}

		// 3. Initialize the past_epoch and the future_epoch.
		stream.past_epoch = stream.future_epoch;
		stream.future_epoch = epoch;

		if (stream.past_epoch < stream.future_epoch) {
			// Reset the alpha so we can start interpolating.
			stream.alpha = 0;
			stream.alpha_advacing_per_epoch = 1.0 / (double(stream.future_epoch) - double(stream.past_epoch));
		} else {
			// The interpolation didn't start yet, so put this really high.
			stream.alpha = FLT_MAX;
			stream.alpha_advacing_per_epoch = FLT_MAX;
		}
	}

	memdelete(db);
}

void ClientSynchronizer::process_received_deferred_sync_data(real_t p_delta) {
	DataBuffer *db1 = memnew(DataBuffer);
	DataBuffer *db2 = memnew(DataBuffer);

	Variant array_vars[4];
	array_vars[0] = p_delta;
	array_vars[2] = db1;
	array_vars[3] = db2;
	const Variant *array_vars_ptr[4] = { array_vars + 0, array_vars + 1, array_vars + 2, array_vars + 3 };

	Variant r;

	for (int i = 0; i < int(deferred_sync_array.size()); ++i) {
		DeferredSyncInterpolationData &stream = deferred_sync_array[i];
		if (stream.alpha > 1.2) {
			// The stream is not yet started.
			// OR
			// The stream for this node is stopped as the data received is old.
			continue;
		}

		NetUtility::NodeData *nd = stream.nd;
		if (nd == nullptr) {
			SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The function `process_received_deferred_sync_data` found a null NodeData into the `deferred_sync_array`; this is not supposed to happen.");
			continue;
		}

#ifdef DEBUG_ENABLED
		if (nd->apply_epoch_func.is_null()) {
			SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The function `process_received_deferred_sync_data` skip the node `" + String(nd->object_name.c_str()) + "` has an invalid apply epoch function named `" + nd->apply_epoch_func.get_method() + "`. Remotely you used the function `setup_deferred_sync` properly, while locally you didn't. Fix it.");
			continue;
		}
#endif

		stream.alpha += stream.alpha_advacing_per_epoch;
		stream.past_epoch_buffer.begin_read();
		stream.future_epoch_buffer.begin_read();

		db1->copy(stream.past_epoch_buffer);
		db2->copy(stream.future_epoch_buffer);
		db1->begin_read();
		db2->begin_read();

		array_vars[1] = stream.alpha;

		Callable::CallError e;
		nd->apply_epoch_func.callp(array_vars_ptr, 4, r, e);

		if (e.error != Callable::CallError::CALL_OK) {
			SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The `process_received_deferred_sync_data` failed executing the function`" + nd->collect_epoch_func.get_method() + "` for the node `" + nd->object_name.c_str() + "`.");
			continue;
		}
	}

	memdelete(db1);
	memdelete(db2);
}

void ClientSynchronizer::remove_node_from_deferred_sync(NetUtility::NodeData *p_node_data) {
	const int64_t index = deferred_sync_array.find(p_node_data);
	if (index >= 0) {
		deferred_sync_array.remove_at_unordered(index);
	}
}

bool ClientSynchronizer::parse_snapshot(Variant p_snapshot) {
	if (want_to_enable) {
		if (enabled) {
			SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "At this point the client is supposed to be disabled. This is a bug that must be solved.");
		}
		// The netwroking is disabled and we can re-enable it.
		enabled = true;
		want_to_enable = false;
		scene_synchronizer->event_sync_started.broadcast();
	}

	need_full_snapshot_notified = false;

	NetUtility::Snapshot received_snapshot = last_received_snapshot;
	received_snapshot.input_id = UINT32_MAX;

	struct ParseData {
		NetUtility::Snapshot &snapshot;
		NetUtility::NodeData *player_controller_node_data;
		SceneSynchronizer *scene_synchronizer;
		ClientSynchronizer *client_synchronizer;
	};

	ParseData parse_data{
		received_snapshot,
		player_controller_node_data,
		scene_synchronizer,
		this
	};

	const bool success = parse_sync_data(
			p_snapshot,
			&parse_data,

			// Custom data:
			[](void *p_user_pointer, const LocalVector<const Variant *> &p_custom_data) {
				ParseData *pd = static_cast<ParseData *>(p_user_pointer);

				pd->snapshot.custom_data.resize(p_custom_data.size());
				Variant *arr = pd->snapshot.custom_data.ptrw();
				for (int i = 0; i < int(p_custom_data.size()); i++) {
					if (p_custom_data[i] != nullptr) {
						arr[i] = *p_custom_data[i];
#ifdef DEBUG_ENABLED
						CRASH_COND(arr[i].get_type() != p_custom_data[i]->get_type());
#endif
					}
				}
			},

			// Parse node:
			[](void *p_user_pointer, NetUtility::NodeData *p_node_data) {
				ParseData *pd = static_cast<ParseData *>(p_user_pointer);

#ifdef DEBUG_ENABLED
				// This function should never receive undefined IDs.
				CRASH_COND(p_node_data->id == UINT32_MAX);
#endif

				// Make sure this node is part of the server node too.
				if (uint32_t(pd->snapshot.node_vars.size()) <= p_node_data->id) {
					pd->snapshot.node_vars.resize(p_node_data->id + 1);
				}
			},

			// Parse InputID:
			[](void *p_user_pointer, uint32_t p_input_id) {
				ParseData *pd = static_cast<ParseData *>(p_user_pointer);
				if (pd->player_controller_node_data != nullptr) {
					// This is the main controller, store the `InputID`.
					pd->snapshot.input_id = p_input_id;
				}
			},

			// Parse controller:
			[](void *p_user_pointer, NetUtility::NodeData *p_node_data) {},

			// Parse variable:
			[](void *p_user_pointer, NetUtility::NodeData *p_node_data, uint32_t p_var_id, const Variant &p_value) {
				ParseData *pd = static_cast<ParseData *>(p_user_pointer);

				if (p_node_data->vars.size() != uint32_t(pd->snapshot.node_vars[p_node_data->id].size())) {
					// The parser may have added a variable, so make sure to resize the vars array.
					pd->snapshot.node_vars.write[p_node_data->id].resize(p_node_data->vars.size());
				}

				pd->snapshot.node_vars.write[p_node_data->id].write[p_var_id].name = p_node_data->vars[p_var_id].var.name;
				pd->snapshot.node_vars.write[p_node_data->id].write[p_var_id].value = p_value.duplicate(true);
			},

			// Parse node activation:
			[](void *p_user_pointer, NetUtility::NodeData *p_node_data, bool p_is_active) {
				ParseData *pd = static_cast<ParseData *>(p_user_pointer);
				if (p_node_data->realtime_sync_enabled_on_client != p_is_active) {
					p_node_data->realtime_sync_enabled_on_client = p_is_active;

					// Make sure the process_function cache is cleared.
					pd->scene_synchronizer->process_functions__clear();
				}

				// Make sure this node is not into the deferred sync list.
				if (p_is_active) {
					pd->client_synchronizer->remove_node_from_deferred_sync(p_node_data);
				}
			});

	if (success == false) {
		SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "Snapshot parsing failed.");
		SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "Snapshot:", !scene_synchronizer->debug_rewindings_enabled);
		SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), NetUtility::stringify_fast(p_snapshot), !scene_synchronizer->debug_rewindings_enabled);
		return false;
	}

	if (unlikely(received_snapshot.input_id == UINT32_MAX && player_controller_node_data != nullptr)) {
		// We espect that the player_controller is updated by this new snapshot,
		// so make sure it's done so.
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "[INFO] the player controller (" + String(player_controller_node_data->object_name.c_str()) + ") was not part of the received snapshot, this happens when the server destroys the peer controller.");
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "NetUtility::Snapshot:", !scene_synchronizer->debug_rewindings_enabled);
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), NetUtility::stringify_fast(p_snapshot), !scene_synchronizer->debug_rewindings_enabled);
	}

	last_received_snapshot = received_snapshot;

	// Success.
	return true;
}

void ClientSynchronizer::notify_server_full_snapshot_is_needed() {
	if (need_full_snapshot_notified) {
		return;
	}

	// Notify the server that a full snapshot is needed.
	need_full_snapshot_notified = true;
	scene_synchronizer->network_interface->rpc(
			scene_synchronizer->rpc_handler_notify_need_full_snapshot,
			scene_synchronizer->network_interface->get_server_peer());
}

void ClientSynchronizer::update_client_snapshot(NetUtility::Snapshot &p_snapshot) {
	p_snapshot.custom_data.clear();
	scene_synchronizer->synchronizer_manager->snapshot_add_custom_data(nullptr, p_snapshot.custom_data);

	// Make sure we have room for all the NodeData.
	p_snapshot.node_vars.resize(scene_synchronizer->organized_node_data.size());

	// Fetch the data.
	for (NetNodeId net_node_id = 0; net_node_id < NetNodeId(scene_synchronizer->organized_node_data.size()); net_node_id += 1) {
		NetUtility::NodeData *nd = scene_synchronizer->organized_node_data[net_node_id];
		if (nd == nullptr || nd->realtime_sync_enabled_on_client == false) {
			continue;
		}

		// Make sure this ID is valid.
		ERR_FAIL_COND_MSG(nd->id == UINT32_MAX, "[BUG] It's not expected that the client has an uninitialized NetNodeId into the `organized_node_data` ");

#ifdef DEBUG_ENABLED
		CRASH_COND_MSG(nd->id >= uint32_t(p_snapshot.node_vars.size()), "This array was resized above, this can't be triggered.");
#endif

		Vector<NetUtility::Var> *snap_node_vars = p_snapshot.node_vars.ptrw() + nd->id;
		snap_node_vars->resize(nd->vars.size());

		NetUtility::Var *snap_node_vars_ptr = snap_node_vars->ptrw();
		for (uint32_t v = 0; v < nd->vars.size(); v += 1) {
			if (nd->vars[v].enabled) {
				snap_node_vars_ptr[v] = nd->vars[v].var;
			} else {
				snap_node_vars_ptr[v].name = StringName();
			}
		}
	}
}

void ClientSynchronizer::apply_snapshot(
		const NetUtility::Snapshot &p_snapshot,
		int p_flag,
		LocalVector<String> *r_applied_data_info,
		bool p_skip_custom_data) {
	const Vector<NetUtility::Var> *nodes_vars = p_snapshot.node_vars.ptr();

	scene_synchronizer->change_events_begin(p_flag);

	for (int net_node_id = 0; net_node_id < p_snapshot.node_vars.size(); net_node_id += 1) {
		NetUtility::NodeData *nd = scene_synchronizer->get_node_data(net_node_id);

		if (nd == nullptr) {
			// This can happen, and it's totally expected, because the server
			// doesn't always sync ALL the node_data: so that will result in a
			// not registered node.
			continue;
		}

		if (nd->realtime_sync_enabled_on_client == false) {
			// This node sync is disabled.
			continue;
		}

		const Vector<NetUtility::Var> &vars = nodes_vars[net_node_id];
		const NetUtility::Var *vars_ptr = vars.ptr();

		if (r_applied_data_info) {
			r_applied_data_info->push_back("Applied snapshot data on the node: " + String(nd->object_name.c_str()));
		}

		// NOTE: The vars may not contain ALL the variables: it depends on how
		//       the snapshot was captured.
		for (int v = 0; v < vars.size(); v += 1) {
			if (vars_ptr[v].name == StringName()) {
				// This variable was not set, skip it.
				continue;
			}

			const Variant current_val = nd->vars[v].var.value;
			nd->vars[v].var.value = vars_ptr[v].value.duplicate(true);

			if (!scene_synchronizer->compare(current_val, vars_ptr[v].value)) {
				scene_synchronizer->synchronizer_manager->set_variable(
						nd->app_object,
						String(vars_ptr[v].name).utf8(),
						vars_ptr[v].value);
				scene_synchronizer->change_event_add(
						nd,
						v,
						current_val);

				if (r_applied_data_info) {
					r_applied_data_info->push_back(" |- Variable: " + vars_ptr[v].name + " New value: " + NetUtility::stringify_fast(vars_ptr[v].value));
				}
			}
		}
	}

	if (!p_skip_custom_data) {
		scene_synchronizer->synchronizer_manager->snapshot_apply_custom_data(p_snapshot.custom_data);
	}

	scene_synchronizer->change_events_flush();
}

NS_NAMESPACE_END
