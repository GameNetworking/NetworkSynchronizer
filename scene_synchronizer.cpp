
#include "scene_synchronizer.h"

#include "core/error/error_macros.h"
#include "core/object/object.h"
#include "core/os/os.h"
#include "core/templates/oa_hash_map.h"
#include "core/variant/variant.h"
#include "input_network_encoder.h"
#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/core/network_interface.h"
#include "modules/network_synchronizer/core/object_data.h"
#include "modules/network_synchronizer/core/processor.h"
#include "modules/network_synchronizer/godot4/gd_network_interface.h"
#include "modules/network_synchronizer/net_utilities.h"
#include "modules/network_synchronizer/networked_controller.h"
#include "modules/network_synchronizer/snapshot.h"
#include "modules/network_synchronizer/tests/local_scene.h"
#include "scene_diff.h"
#include "scene_synchronizer_debugger.h"
#include <stdexcept>

NS_NAMESPACE_BEGIN

const SyncGroupId SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID = 0;

SceneSynchronizerBase::SceneSynchronizerBase(NetworkInterface *p_network_interface) :
		network_interface(p_network_interface),
		objects_data_storage(*this) {
	// Avoid too much useless re-allocations.
	changes_listeners.reserve(100);
}

SceneSynchronizerBase::~SceneSynchronizerBase() {
	clear();
	uninit_synchronizer();
	network_interface = nullptr;
}

void SceneSynchronizerBase::setup(SynchronizerManager &p_synchronizer_interface) {
	synchronizer_manager = &p_synchronizer_interface;
	network_interface->start_listening_peer_connection(
			[this](int p_peer) { on_peer_connected(p_peer); },
			[this](int p_peer) { on_peer_disconnected(p_peer); });

	rpc_handler_state =
			network_interface->rpc_config(
					std::function<void(const Variant &)>(std::bind(&SceneSynchronizerBase::rpc_receive_state, this, std::placeholders::_1)),
					true,
					false);

	rpc_handler_notify_need_full_snapshot =
			network_interface->rpc_config(
					std::function<void()>(std::bind(&SceneSynchronizerBase::rpc__notify_need_full_snapshot, this)),
					true,
					false);

	rpc_handler_set_network_enabled =
			network_interface->rpc_config(
					std::function<void(bool)>(std::bind(&SceneSynchronizerBase::rpc_set_network_enabled, this, std::placeholders::_1)),
					true,
					false);

	rpc_handler_notify_peer_status =
			network_interface->rpc_config(
					std::function<void(bool)>(std::bind(&SceneSynchronizerBase::rpc_notify_peer_status, this, std::placeholders::_1)),
					true,
					false);

	rpc_handler_deferred_sync_data =
			network_interface->rpc_config(
					std::function<void(const Vector<uint8_t> &)>(std::bind(&SceneSynchronizerBase::rpc_deferred_sync_data, this, std::placeholders::_1)),
					false,
					false);

	clear();
	reset_synchronizer_mode();

	// Make sure to reset all the assigned controllers.
	reset_controllers();

	// Init the peers already connected.
	std::vector<int> peer_ids;
	network_interface->fetch_connected_peers(peer_ids);
	for (int peer_id : peer_ids) {
		on_peer_connected(peer_id);
	}
}

void SceneSynchronizerBase::conclude() {
	network_interface->stop_listening_peer_connection();
	network_interface->clear();

	clear_peers();
	clear();
	uninit_synchronizer();

	// Make sure to reset all the assigned controllers.
	reset_controllers();

	synchronizer_manager = nullptr;

	rpc_handler_state.reset();
	rpc_handler_notify_need_full_snapshot.reset();
	rpc_handler_set_network_enabled.reset();
	rpc_handler_notify_peer_status.reset();
	rpc_handler_deferred_sync_data.reset();
}

void SceneSynchronizerBase::process() {
	PROFILE_NODE

#ifdef DEBUG_ENABLED
	CRASH_COND_MSG(synchronizer == nullptr, "Never execute this function unless this synchronizer is ready.");

	synchronizer_manager->debug_only_validate_nodes();
#endif

	synchronizer->process();
}

void SceneSynchronizerBase::on_app_object_removed(ObjectHandle p_app_object_handle) {
	unregister_app_object(find_object_local_id(p_app_object_handle));
}

void SceneSynchronizerBase::set_max_deferred_nodes_per_update(int p_rate) {
	max_deferred_nodes_per_update = p_rate;
}

int SceneSynchronizerBase::get_max_deferred_nodes_per_update() const {
	return max_deferred_nodes_per_update;
}

void SceneSynchronizerBase::set_server_notify_state_interval(real_t p_interval) {
	server_notify_state_interval = p_interval;
}

real_t SceneSynchronizerBase::get_server_notify_state_interval() const {
	return server_notify_state_interval;
}

void SceneSynchronizerBase::set_comparison_float_tolerance(real_t p_tolerance) {
	comparison_float_tolerance = p_tolerance;
}

real_t SceneSynchronizerBase::get_comparison_float_tolerance() const {
	return comparison_float_tolerance;
}

void SceneSynchronizerBase::set_nodes_relevancy_update_time(real_t p_time) {
	nodes_relevancy_update_time = p_time;
}

real_t SceneSynchronizerBase::get_nodes_relevancy_update_time() const {
	return nodes_relevancy_update_time;
}

bool SceneSynchronizerBase::is_variable_registered(ObjectLocalId p_id, const StringName &p_variable) const {
	const ObjectData *nd = objects_data_storage.get_object_data(p_id);
	if (nd != nullptr) {
		return nd->vars.find(p_variable) >= 0;
	}
	return false;
}

void SceneSynchronizerBase::register_app_object(ObjectHandle p_app_object_handle, ObjectLocalId *out_id) {
	ERR_FAIL_COND(p_app_object_handle == ObjectHandle::NONE);

	ObjectLocalId id = objects_data_storage.find_object_local_id(p_app_object_handle);
	if (out_id) {
		*out_id = id;
	}

	if (id == ObjectLocalId::NONE) {
		ObjectData *od = objects_data_storage.allocate_object_data();
		id = od->get_local_id();
		if (out_id) {
			*out_id = id;
		}

		od->set_net_id(ObjectNetId::NONE);
		od->instance_id = synchronizer_manager->get_object_id(p_app_object_handle);
		od->object_name = synchronizer_manager->get_object_name(p_app_object_handle);
		od->app_object_handle = p_app_object_handle;

		od->set_controller(synchronizer_manager->extract_network_controller(p_app_object_handle));
		if (od->get_controller()) {
			if (unlikely(od->get_controller()->has_scene_synchronizer())) {
				ERR_PRINT("This controller already has a synchronizer. This is a bug!");
			}

			dirty_peers();
		}

		if (generate_id) {
#ifdef DEBUG_ENABLED
			// When generate_id is true, the id must always be undefined.
			CRASH_COND(od->get_net_id() != ObjectNetId::NONE);
#endif
			od->set_net_id(objects_data_storage.generate_net_id());
		}

		if (od->get_controller()) {
			CRASH_COND_MSG(!od->get_controller()->network_interface, "This controller `network_interface` is not set. Please call `setup()` before registering this object as networked.");
			reset_controller(od);
		}

		if (od->has_registered_process_functions()) {
			process_functions__clear();
		}

		if (synchronizer) {
			synchronizer->on_object_data_added(od);
		}

		synchronizer_manager->on_add_object_data(*od);

		synchronizer_manager->setup_synchronizer_for(p_app_object_handle, id);

		SceneSynchronizerDebugger::singleton()->debug_print(network_interface, "New node registered" + (generate_id ? String(" #ID: ") + itos(od->get_net_id().id) : "") + " : " + od->object_name.c_str());

		if (od->get_controller()) {
			od->get_controller()->notify_registered_with_synchronizer(this, *od);
		}
	}

	CRASH_COND(id == ObjectLocalId::NONE);
}

void SceneSynchronizerBase::unregister_app_object(ObjectLocalId p_id) {
	if (p_id == ObjectLocalId::NONE) {
		// Nothing to do.
		return;
	}

	ObjectData *od = objects_data_storage.get_object_data(p_id, false);
	if (!od) {
		// Nothing to do.
		return;
	}

	drop_object_data(*od);
}

void SceneSynchronizerBase::register_variable(ObjectLocalId p_id, const StringName &p_variable) {
	ERR_FAIL_COND(p_id == ObjectLocalId::NONE);
	ERR_FAIL_COND(p_variable == StringName());

	NS::ObjectData *node_data = get_object_data(p_id);
	ERR_FAIL_COND(node_data == nullptr);

	const int index = node_data->vars.find(p_variable);
	if (index == -1) {
		// The variable is not yet registered.
		bool valid = false;
		Variant old_val;
		valid = synchronizer_manager->get_variable(node_data->app_object_handle, String(p_variable).utf8(), old_val);
		if (valid == false) {
			SceneSynchronizerDebugger::singleton()->debug_error(network_interface, "The variable `" + p_variable + "` on the node `" + String(node_data->object_name.c_str()) + "` was not found, make sure the variable exist.");
		}
		const VarId var_id = generate_id ? VarId{ node_data->vars.size() } : VarId::NONE;
		node_data->vars.push_back(
				NS::VarDescriptor(
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
	for (VarId v = { 0 }; v < VarId{ node_data->vars.size() }; v += 1) {
		// This can't happen, because the ID is always consecutive, or UINT32_MAX.
		CRASH_COND(node_data->vars[v.id].id != v && node_data->vars[v.id].id != VarId::NONE);
	}
#endif

	if (synchronizer) {
		synchronizer->on_variable_added(node_data, p_variable);
	}
}

void SceneSynchronizerBase::unregister_variable(ObjectLocalId p_id, const StringName &p_variable) {
	ERR_FAIL_COND(p_id == ObjectLocalId::NONE);
	ERR_FAIL_COND(p_variable == StringName());

	NS::ObjectData *nd = objects_data_storage.get_object_data(p_id);
	ERR_FAIL_COND(nd == nullptr);

	const int index = nd->vars.find(p_variable);
	ERR_FAIL_COND(index == -1);

	const VarId var_id = { uint32_t(index) };

	// Never remove the variable values, because the order of the vars matters.
	nd->vars[index].enabled = false;

	// Remove this var from all the changes listeners.
	for (ChangesListener *cl : nd->vars[var_id.id].changes_listeners) {
		for (ListeningVariable lv : cl->watching_vars) {
			if (lv.node_data == nd && lv.var_id == var_id) {
				// We can't change the var order, so just invalidate this.
				lv.node_data = nullptr;
				lv.var_id = VarId::NONE;
			}
		}
	}

	// So, clear the changes listener list for this var.
	nd->vars[var_id.id].changes_listeners.clear();
}

ObjectNetId SceneSynchronizerBase::get_app_object_net_id(ObjectHandle p_app_object_handle) const {
	const NS::ObjectData *nd = objects_data_storage.get_object_data(objects_data_storage.find_object_local_id(p_app_object_handle));
	if (nd) {
		return nd->get_net_id();
	} else {
		return ObjectNetId::NONE;
	}
}

ObjectHandle SceneSynchronizerBase::get_app_object_from_id(ObjectNetId p_id, bool p_expected) {
	NS::ObjectData *nd = get_object_data(p_id, p_expected);
	if (p_expected) {
		ERR_FAIL_COND_V_MSG(nd == nullptr, ObjectHandle::NONE, "The ID " + itos(p_id.id) + " is not assigned to any node.");
		return nd->app_object_handle;
	} else {
		return nd ? nd->app_object_handle : ObjectHandle::NONE;
	}
}

ObjectHandle SceneSynchronizerBase::get_app_object_from_id_const(ObjectNetId p_id, bool p_expected) const {
	const NS::ObjectData *nd = get_object_data(p_id, p_expected);
	if (p_expected) {
		ERR_FAIL_COND_V_MSG(nd == nullptr, ObjectHandle::NONE, "The ID " + itos(p_id.id) + " is not assigned to any node.");
		return nd->app_object_handle;
	} else {
		return nd ? nd->app_object_handle : ObjectHandle::NONE;
	}
}

const std::vector<ObjectData *> &SceneSynchronizerBase::get_all_object_data() const {
	return objects_data_storage.get_objects_data();
}

VarId SceneSynchronizerBase::get_variable_id(ObjectLocalId p_id, const StringName &p_variable) {
	ERR_FAIL_COND_V(p_variable == StringName(), VarId::NONE);

	NS::ObjectData *nd = get_object_data(p_id);
	ERR_FAIL_COND_V_MSG(nd == nullptr, VarId::NONE, "This node " + String(nd->object_name.c_str()) + "is not registered.");

	const int64_t index = nd->vars.find(p_variable);
	ERR_FAIL_COND_V_MSG(index == -1, VarId::NONE, "This variable " + String(nd->object_name.c_str()) + ":" + p_variable + " is not registered.");

	return VarId{ uint32_t(index) };
}

void SceneSynchronizerBase::set_skip_rewinding(ObjectLocalId p_id, const StringName &p_variable, bool p_skip_rewinding) {
	NS::ObjectData *nd = get_object_data(p_id);
	ERR_FAIL_COND(nd == nullptr);

	const int64_t index = nd->vars.find(p_variable);
	ERR_FAIL_COND(index == -1);

	nd->vars[index].skip_rewinding = p_skip_rewinding;
}

ListenerHandle SceneSynchronizerBase::track_variable_changes(
		ObjectLocalId p_id,
		const StringName &p_variable,
		std::function<void(const std::vector<Variant> &p_old_values)> p_listener_func,
		NetEventFlag p_flags) {
	std::vector<ObjectLocalId> object_ids;
	std::vector<StringName> variables;
	object_ids.push_back(p_id);
	variables.push_back(p_variable);
	return track_variables_changes(object_ids, variables, p_listener_func, p_flags);
}

ListenerHandle SceneSynchronizerBase::track_variables_changes(
		const std::vector<ObjectLocalId> &p_object_ids,
		const std::vector<StringName> &p_variables,
		std::function<void(const std::vector<Variant> &p_old_values)> p_listener_func,
		NetEventFlag p_flags) {
	ERR_FAIL_COND_V_MSG(p_object_ids.size() != p_variables.size(), nulllistenerhandle, "object_ids and variables should have the exact same size.");
	ERR_FAIL_COND_V_MSG(p_object_ids.size() == 0, nulllistenerhandle, "object_ids can't be of size 0");
	ERR_FAIL_COND_V_MSG(p_variables.size() == 0, nulllistenerhandle, "object_ids can't be of size 0");

	bool is_valid = true;

	// TODO allocate into a buffer instead of using `new`?
	ChangesListener *listener = new ChangesListener;
	listener->listener_func = p_listener_func;
	listener->flag = p_flags;

	listener->watching_vars.resize(p_object_ids.size());
	listener->old_values.resize(p_object_ids.size());
	for (int i = 0; i < int(p_object_ids.size()); i++) {
		ObjectLocalId id = p_object_ids[i];
		const StringName variable_name = p_variables[i];

		NS::ObjectData *nd = objects_data_storage.get_object_data(id);
		if (!nd) {
			ERR_PRINT("The passed ObjectHandle `" + itos(id.id) + "` is not pointing to any valid NodeData. Make sure to register the variable first.");
			is_valid = false;
			break;
		}

		const int v = nd->vars.find(variable_name);
		if (v <= -1) {
			ERR_PRINT("The passed variable `" + variable_name + "` doesn't exist under this object `" + String(nd->object_name.c_str()) + "`.");
			is_valid = false;
			break;
		}

		listener->watching_vars[i].node_data = nd;
		listener->watching_vars[i].var_id = { uint32_t(v) };
	}

	if (is_valid) {
		// Now we are sure that everything passed by the user is valid
		// we can connect the other NodeData to this listener.
		for (auto wv : listener->watching_vars) {
			NS::ObjectData *nd = wv.node_data;
			nd->vars[wv.var_id.id].changes_listeners.push_back(listener);
		}

		changes_listeners.push_back(listener);
		return ListenerHandle::to_handle(listener);
	} else {
		delete listener;
		return nulllistenerhandle;
	}
}

void SceneSynchronizerBase::untrack_variable_changes(ListenerHandle p_handle) {
	// Find the listener

	const ChangesListener *unsafe_handle = ListenerHandle::from_handle(p_handle);
	auto it = ns_find(changes_listeners, unsafe_handle);
	if (it == changes_listeners.end()) {
		// Nothing to do.
		return;
	}

	ChangesListener *listener = *it;

	// Before dropping this listener, make sure to clear the NodeData.
	for (auto &wv : listener->watching_vars) {
		if (wv.node_data) {
			if (wv.node_data->vars.size() > wv.var_id.id) {
				auto wv_cl_it = ns_find(wv.node_data->vars[wv.var_id.id].changes_listeners, unsafe_handle);
				if (wv_cl_it != wv.node_data->vars[wv.var_id.id].changes_listeners.end()) {
					wv.node_data->vars[wv.var_id.id].changes_listeners.erase(wv_cl_it);
				}
			}
		}
	}

	changes_listeners.erase(it);

	// Now it's time to clear the pointer.
	delete listener;
}

NS::PHandler SceneSynchronizerBase::register_process(ObjectLocalId p_id, ProcessPhase p_phase, std::function<void(float)> p_func) {
	ERR_FAIL_COND_V(p_id == NS::ObjectLocalId::NONE, NS::NullPHandler);
	ERR_FAIL_COND_V(!p_func, NS::NullPHandler);

	ObjectData *od = get_object_data(p_id);
	ERR_FAIL_COND_V(!od, NS::NullPHandler);

	const NS::PHandler EFH = od->functions[p_phase].bind(p_func);

	process_functions__clear();

	return EFH;
}

void SceneSynchronizerBase::unregister_process(ObjectLocalId p_id, ProcessPhase p_phase, NS::PHandler p_func_handler) {
	ERR_FAIL_COND(p_id == NS::ObjectLocalId::NONE);

	ObjectData *od = get_object_data(p_id);
	if (od) {
		od->functions[p_phase].unbind(p_func_handler);
		process_functions__clear();
	}
}

void SceneSynchronizerBase::setup_deferred_sync(ObjectLocalId p_id, const Callable &p_collect_epoch_func, const Callable &p_apply_epoch_func) {
	ERR_FAIL_COND(p_id == ObjectLocalId::NONE);
	ERR_FAIL_COND(!p_collect_epoch_func.is_valid());
	ERR_FAIL_COND(!p_apply_epoch_func.is_valid());
	NS::ObjectData *od = get_object_data(p_id);
	ERR_FAIL_COND(!od);
	od->collect_epoch_func = p_collect_epoch_func;
	od->apply_epoch_func = p_apply_epoch_func;
	SceneSynchronizerDebugger::singleton()->debug_print(network_interface, "Setup deferred sync functions for: `" + String(od->object_name.c_str()) + "`. Collect epoch, method name: `" + p_collect_epoch_func.get_method() + "`. Apply epoch, method name: `" + p_apply_epoch_func.get_method() + "`.");
}

SyncGroupId SceneSynchronizerBase::sync_group_create() {
	ERR_FAIL_COND_V_MSG(!is_server(), UINT32_MAX, "This function CAN be used only on the server.");
	const SyncGroupId id = static_cast<ServerSynchronizer *>(synchronizer)->sync_group_create();
	synchronizer_manager->on_sync_group_created(id);
	return id;
}

const NS::SyncGroup *SceneSynchronizerBase::sync_group_get(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(!is_server(), nullptr, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get(p_group_id);
}

void SceneSynchronizerBase::sync_group_add_node_by_id(ObjectNetId p_node_id, SyncGroupId p_group_id, bool p_realtime) {
	NS::ObjectData *nd = get_object_data(p_node_id);
	sync_group_add_node(nd, p_group_id, p_realtime);
}

void SceneSynchronizerBase::sync_group_add_node(NS::ObjectData *p_object_data, SyncGroupId p_group_id, bool p_realtime) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_add_node(p_object_data, p_group_id, p_realtime);
}

void SceneSynchronizerBase::sync_group_remove_node_by_id(ObjectNetId p_node_id, SyncGroupId p_group_id) {
	NS::ObjectData *nd = get_object_data(p_node_id);
	sync_group_remove_node(nd, p_group_id);
}

void SceneSynchronizerBase::sync_group_remove_node(NS::ObjectData *p_object_data, SyncGroupId p_group_id) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_remove_node(p_object_data, p_group_id);
}

void SceneSynchronizerBase::sync_group_replace_nodes(SyncGroupId p_group_id, LocalVector<NS::SyncGroup::RealtimeNodeInfo> &&p_new_realtime_nodes, LocalVector<NS::SyncGroup::DeferredNodeInfo> &&p_new_deferred_nodes) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_replace_nodes(p_group_id, std::move(p_new_realtime_nodes), std::move(p_new_deferred_nodes));
}

void SceneSynchronizerBase::sync_group_remove_all_nodes(SyncGroupId p_group_id) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_remove_all_nodes(p_group_id);
}

void SceneSynchronizerBase::sync_group_move_peer_to(int p_peer_id, SyncGroupId p_group_id) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");

	NS::PeerData *pd = MapFunc::at(peer_data, p_peer_id);
	ERR_FAIL_COND_MSG(pd == nullptr, "The PeerData doesn't exist. This looks like a bug. Are you sure the peer_id `" + itos(p_peer_id) + "` exists?");

	if (pd->sync_group_id == p_group_id) {
		// Nothing to do.
		return;
	}

	pd->sync_group_id = p_group_id;

	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_move_peer_to(p_peer_id, p_group_id);
}

SyncGroupId SceneSynchronizerBase::sync_group_get_peer_group(int p_peer_id) const {
	ERR_FAIL_COND_V_MSG(!is_server(), UINT32_MAX, "This function CAN be used only on the server.");

	const NS::PeerData *pd = MapFunc::at(peer_data, p_peer_id);
	ERR_FAIL_COND_V_MSG(pd == nullptr, UINT32_MAX, "The PeerData doesn't exist. This looks like a bug. Are you sure the peer_id `" + itos(p_peer_id) + "` exists?");

	return pd->sync_group_id;
}

const LocalVector<int> *SceneSynchronizerBase::sync_group_get_peers(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(!is_server(), nullptr, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get_peers(p_group_id);
}

void SceneSynchronizerBase::sync_group_set_deferred_update_rate(ObjectLocalId p_node_id, SyncGroupId p_group_id, real_t p_update_rate) {
	NS::ObjectData *od = get_object_data(p_node_id);
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_set_deferred_update_rate(od, p_group_id, p_update_rate);
}

void SceneSynchronizerBase::sync_group_set_deferred_update_rate(ObjectNetId p_node_id, SyncGroupId p_group_id, real_t p_update_rate) {
	NS::ObjectData *od = get_object_data(p_node_id);
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_set_deferred_update_rate(od, p_group_id, p_update_rate);
}

real_t SceneSynchronizerBase::sync_group_get_deferred_update_rate(ObjectLocalId p_id, SyncGroupId p_group_id) const {
	const NS::ObjectData *od = get_object_data(p_id);
	ERR_FAIL_COND_V_MSG(!is_server(), 0.0, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get_deferred_update_rate(od, p_group_id);
}

real_t SceneSynchronizerBase::sync_group_get_deferred_update_rate(ObjectNetId p_id, SyncGroupId p_group_id) const {
	const NS::ObjectData *od = get_object_data(p_id);
	ERR_FAIL_COND_V_MSG(!is_server(), 0.0, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get_deferred_update_rate(od, p_group_id);
}

void SceneSynchronizerBase::sync_group_set_user_data(SyncGroupId p_group_id, uint64_t p_user_data) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_set_user_data(p_group_id, p_user_data);
}

uint64_t SceneSynchronizerBase::sync_group_get_user_data(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(!is_server(), 0, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get_user_data(p_group_id);
}

void SceneSynchronizerBase::start_tracking_scene_changes(Object *p_diff_handle) const {
	ERR_FAIL_COND_MSG(!is_server(), "This function is supposed to be called only on server.");
	SceneDiff *diff = Object::cast_to<SceneDiff>(p_diff_handle);
	ERR_FAIL_COND_MSG(diff == nullptr, "The object is not a SceneDiff class.");

	// TODO add this back?
	//diff->start_tracking_scene_changes(this, objects_data_storage.get_sorted_objects_data());
}

void SceneSynchronizerBase::stop_tracking_scene_changes(Object *p_diff_handle) const {
	ERR_FAIL_COND_MSG(!is_server(), "This function is supposed to be called only on server.");
	SceneDiff *diff = Object::cast_to<SceneDiff>(p_diff_handle);
	ERR_FAIL_COND_MSG(diff == nullptr, "The object is not a SceneDiff class.");

	diff->stop_tracking_scene_changes(this);
}

Variant SceneSynchronizerBase::pop_scene_changes(Object *p_diff_handle) const {
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
	for (ObjectNetId node_id = { 0 }; node_id < ObjectNetId{ diff->diff.size() }; node_id += 1) {
		if (diff->diff[node_id.id].size() == 0) {
			// Nothing to do.
			continue;
		}

		bool node_id_in_ret = false;
		for (VarId var_id = { 0 }; var_id < VarId{ diff->diff[node_id.id].size() }; var_id += 1) {
			if (diff->diff[node_id.id][var_id.id].is_different == false) {
				continue;
			}
			if (node_id_in_ret == false) {
				node_id_in_ret = true;
				// Set the node id.
				ret.push_back(node_id.id);
			}
			ret.push_back(var_id.id);
			ret.push_back(diff->diff[node_id.id][var_id.id].value);
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

void SceneSynchronizerBase::apply_scene_changes(const Variant &p_sync_data) {
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
			[](void *p_user_pointer, NS::ObjectData *p_object_data) {},

			// Parse InputID:
			[](void *p_user_pointer, uint32_t p_input_id) {},

			// Parse controller:
			[](void *p_user_pointer, NS::ObjectData *p_object_data) {},

			// Parse variable:
			[](void *p_user_pointer, NS::ObjectData *p_object_data, VarId p_var_id, const Variant &p_value) {
				SceneSynchronizerBase *scene_sync = static_cast<SceneSynchronizerBase *>(p_user_pointer);

				const Variant current_val = p_object_data->vars[p_var_id.id].var.value;

				if (scene_sync->compare(current_val, p_value) == false) {
					// There is a difference.
					// Set the new value.
					p_object_data->vars[p_var_id.id].var.value = p_value;
					scene_sync->synchronizer_manager->set_variable(
							p_object_data->app_object_handle,
							String(p_object_data->vars[p_var_id.id].var.name).utf8().get_data(),
							p_value);

					// Add an event.
					scene_sync->change_event_add(
							p_object_data,
							p_var_id,
							current_val);
				}
			},

			// Parse node activation:
			[](void *p_user_pointer, NS::ObjectData *p_object_data, bool p_is_active) {
			});

	if (success == false) {
		SceneSynchronizerDebugger::singleton()->debug_error(network_interface, "Scene changes:");
		SceneSynchronizerDebugger::singleton()->debug_error(network_interface, NS::stringify_fast(p_sync_data));
	}

	change_events_flush();
}

bool SceneSynchronizerBase::is_recovered() const {
	return recover_in_progress;
}

bool SceneSynchronizerBase::is_resetted() const {
	return reset_in_progress;
}

bool SceneSynchronizerBase::is_rewinding() const {
	return rewinding_in_progress;
}

bool SceneSynchronizerBase::is_end_sync() const {
	return end_sync;
}

void SceneSynchronizerBase::force_state_notify(SyncGroupId p_sync_group_id) {
	ERR_FAIL_COND(is_server() == false);
	ServerSynchronizer *r = static_cast<ServerSynchronizer *>(synchronizer);
	// + 1.0 is just a ridiculous high number to be sure to avoid float
	// precision error.
	ERR_FAIL_COND_MSG(p_sync_group_id >= r->sync_groups.size(), "The group id `" + itos(p_sync_group_id) + "` doesn't exist.");
	r->sync_groups[p_sync_group_id].state_notifier_timer = get_server_notify_state_interval() + 1.0;
}

void SceneSynchronizerBase::force_state_notify_all() {
	ERR_FAIL_COND(is_server() == false);
	ServerSynchronizer *r = static_cast<ServerSynchronizer *>(synchronizer);

	for (uint32_t i = 0; i < r->sync_groups.size(); ++i) {
		// + 1.0 is just a ridiculous high number to be sure to avoid float
		// precision error.
		r->sync_groups[i].state_notifier_timer = get_server_notify_state_interval() + 1.0;
	}
}

void SceneSynchronizerBase::dirty_peers() {
	peer_dirty = true;
}

void SceneSynchronizerBase::set_enabled(bool p_enable) {
	ERR_FAIL_COND_MSG(synchronizer_type == SYNCHRONIZER_TYPE_SERVER, "The server is always enabled.");
	if (synchronizer_type == SYNCHRONIZER_TYPE_CLIENT) {
		rpc_handler_set_network_enabled.rpc(*network_interface, network_interface->get_server_peer(), p_enable);
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

bool SceneSynchronizerBase::is_enabled() const {
	ERR_FAIL_COND_V_MSG(synchronizer_type == SYNCHRONIZER_TYPE_SERVER, false, "The server is always enabled.");
	if (likely(synchronizer_type == SYNCHRONIZER_TYPE_CLIENT)) {
		return static_cast<ClientSynchronizer *>(synchronizer)->enabled;
	} else if (synchronizer_type == SYNCHRONIZER_TYPE_NONETWORK) {
		return static_cast<NoNetSynchronizer *>(synchronizer)->enabled;
	} else {
		return true;
	}
}

void SceneSynchronizerBase::set_peer_networking_enable(int p_peer, bool p_enable) {
	if (synchronizer_type == SYNCHRONIZER_TYPE_SERVER) {
		ERR_FAIL_COND_MSG(p_peer == 1, "Disable the server is not possible.");

		NS::PeerData *pd = MapFunc::at(peer_data, p_peer);
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
			static_cast<ServerSynchronizer *>(synchronizer)->sync_group_move_peer_to(p_peer, UINT32_MAX);
		}

		dirty_peers();

		// Just notify the peer status.
		rpc_handler_notify_peer_status.rpc(*network_interface, p_peer, p_enable);
	} else {
		ERR_FAIL_COND_MSG(synchronizer_type != SYNCHRONIZER_TYPE_NONETWORK, "At this point no network is expected.");
		static_cast<NoNetSynchronizer *>(synchronizer)->set_enabled(p_enable);
	}
}

bool SceneSynchronizerBase::is_peer_networking_enable(int p_peer) const {
	if (synchronizer_type == SYNCHRONIZER_TYPE_SERVER) {
		if (p_peer == 1) {
			// Server is always enabled.
			return true;
		}

		const NS::PeerData *pd = MapFunc::at(peer_data, p_peer);
		ERR_FAIL_COND_V_MSG(pd == nullptr, false, "The peer: " + itos(p_peer) + " is not know. [bug]");
		return pd->enabled;
	} else {
		ERR_FAIL_COND_V_MSG(synchronizer_type != SYNCHRONIZER_TYPE_NONETWORK, false, "At this point no network is expected.");
		return static_cast<NoNetSynchronizer *>(synchronizer)->is_enabled();
	}
}

void SceneSynchronizerBase::on_peer_connected(int p_peer) {
	peer_data.insert(std::pair(p_peer, NS::PeerData()));

	event_peer_status_updated.broadcast(nullptr, p_peer, true, false);

	dirty_peers();
	if (synchronizer) {
		synchronizer->on_peer_connected(p_peer);
	}
}

void SceneSynchronizerBase::on_peer_disconnected(int p_peer) {
	// Emit a signal notifying this peer is gone.
	NS::PeerData *pd = MapFunc::at(peer_data, p_peer);
	ObjectNetId id = ObjectNetId::NONE;
	NS::ObjectData *node_data = nullptr;
	if (pd) {
		id = pd->controller_id;
		node_data = get_object_data(id);
	}

	event_peer_status_updated.broadcast(node_data, p_peer, false, false);

	peer_data.erase(p_peer);

#ifdef DEBUG_ENABLED
	CRASH_COND_MSG(peer_data.count(p_peer) > 0, "The peer was just removed. This can't be triggered.");
#endif

	if (synchronizer) {
		synchronizer->on_peer_disconnected(p_peer);
	}
}

void SceneSynchronizerBase::init_synchronizer(bool p_was_generating_ids) {
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
		objects_data_storage.reserve_net_ids(objects_data_storage.get_objects_data().size());
		for (uint32_t i = 0; i < objects_data_storage.get_objects_data().size(); i += 1) {
			ObjectData *od = objects_data_storage.get_objects_data()[i];
			if (!od) {
				continue;
			}

			// Handle the node ID.
			if (generate_id) {
				od->set_net_id({ i });
			} else {
				od->set_net_id(ObjectNetId::NONE);
			}

			// Handle the variables ID.
			for (uint32_t v = 0; v < od->vars.size(); v += 1) {
				if (generate_id) {
					od->vars[v].id = { v };
				} else {
					od->vars[v].id = VarId::NONE;
				}
			}
		}
	}

	// Notify the presence all available nodes and its variables to the synchronizer.
	for (auto od : objects_data_storage.get_objects_data()) {
		if (!od) {
			continue;
		}

		synchronizer->on_object_data_added(od);
		for (uint32_t y = 0; y < od->vars.size(); y += 1) {
			synchronizer->on_variable_added(od, od->vars[y].var.name);
		}
	}

	// Notify the presence all available peers
	for (auto &peer_it : peer_data) {
		synchronizer->on_peer_connected(peer_it.first);
	}

	// Reset the controllers.
	reset_controllers();

	process_functions__clear();
	synchronizer_manager->on_init_synchronizer(p_was_generating_ids);
}

void SceneSynchronizerBase::uninit_synchronizer() {
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

void SceneSynchronizerBase::reset_synchronizer_mode() {
	debug_rewindings_enabled = ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/log_debug_rewindings");
	const bool was_generating_ids = generate_id;
	uninit_synchronizer();
	init_synchronizer(was_generating_ids);
}

void SceneSynchronizerBase::clear() {
	// Drop the node_data.
	std::vector<ObjectData *> objects_tmp = objects_data_storage.get_objects_data();
	for (auto od : objects_tmp) {
		if (od) {
			drop_object_data(*od);
		}
	}

	// The above loop should have cleaned this array entirely.
	CRASH_COND(!objects_data_storage.is_empty());

	for (auto cl : changes_listeners) {
		delete cl;
	}
	changes_listeners.clear();

	// Avoid too much useless re-allocations.
	changes_listeners.reserve(100);

	if (synchronizer) {
		synchronizer->clear();
	}

	process_functions__clear();
}

void SceneSynchronizerBase::notify_controller_control_mode_changed(NetworkedControllerBase *controller) {
	if (controller) {
		// TODO improve this mess?
		reset_controller(objects_data_storage.get_object_data(objects_data_storage.find_object_local_id(*controller)));
	}
}

void SceneSynchronizerBase::rpc_receive_state(const Variant &p_snapshot) {
	ERR_FAIL_COND_MSG(is_client() == false, "Only clients are suposed to receive the server snapshot.");
	static_cast<ClientSynchronizer *>(synchronizer)->receive_snapshot(p_snapshot);
}

void SceneSynchronizerBase::rpc__notify_need_full_snapshot() {
	ERR_FAIL_COND_MSG(is_server() == false, "Only the server can receive the request to send a full snapshot.");

	const int sender_peer = network_interface->rpc_get_sender();
	NS::PeerData *pd = MapFunc::at(peer_data, sender_peer);
	ERR_FAIL_COND(pd == nullptr);
	pd->need_full_snapshot = true;
}

void SceneSynchronizerBase::rpc_set_network_enabled(bool p_enabled) {
	ERR_FAIL_COND_MSG(is_server() == false, "The peer status is supposed to be received by the server.");
	set_peer_networking_enable(
			network_interface->rpc_get_sender(),
			p_enabled);
}

void SceneSynchronizerBase::rpc_notify_peer_status(bool p_enabled) {
	ERR_FAIL_COND_MSG(is_client() == false, "The peer status is supposed to be received by the client.");
	static_cast<ClientSynchronizer *>(synchronizer)->set_enabled(p_enabled);
}

void SceneSynchronizerBase::rpc_deferred_sync_data(const Vector<uint8_t> &p_data) {
	ERR_FAIL_COND_MSG(is_client() == false, "Only clients are supposed to receive this function call.");
	ERR_FAIL_COND_MSG(p_data.size() <= 0, "It's not supposed to receive a 0 size data.");

	static_cast<ClientSynchronizer *>(synchronizer)->receive_deferred_sync_data(p_data);
}

void SceneSynchronizerBase::update_peers() {
#ifdef DEBUG_ENABLED
	// This function is only called on server.
	CRASH_COND(synchronizer_type != SYNCHRONIZER_TYPE_SERVER);
#endif

	if (likely(peer_dirty == false)) {
		return;
	}

	peer_dirty = false;

	for (auto &it : peer_data) {
		// Validate the peer.
		if (it.second.controller_id != ObjectNetId::NONE) {
			NS::ObjectData *nd = get_object_data(it.second.controller_id);
			if (nd == nullptr ||
					nd->get_controller() == nullptr ||
					nd->get_controller()->network_interface->get_unit_authority() != it.first) {
				// Invalidate the controller id
				it.second.controller_id = ObjectNetId::NONE;
			}
		} else {
			// The controller_id is not assigned, search it.
			for (uint32_t i = 0; i < objects_data_storage.get_controllers_objects_data().size(); i += 1) {
				const NetworkedControllerBase *nc = objects_data_storage.get_controllers_objects_data()[i]->get_controller();
				if (nc && nc->network_interface->get_unit_authority() == it.first) {
					// Controller found.
					it.second.controller_id = objects_data_storage.get_controllers_objects_data()[i]->get_net_id();
					break;
				}
			}
		}

		NS::ObjectData *nd = get_object_data(it.second.controller_id, false);
		if (nd) {
			nd->realtime_sync_enabled_on_client = it.second.enabled;
			event_peer_status_updated.broadcast(nd, it.first, true, it.second.enabled);
		}
	}
}

void SceneSynchronizerBase::clear_peers() {
	// Copy, so we can safely remove the peers from `peer_data`.
	std::map<int, NS::PeerData> peer_data_tmp = peer_data;
	for (auto &it : peer_data_tmp) {
		on_peer_disconnected(it.first);
	}

	CRASH_COND_MSG(!peer_data.empty(), "The above loop should have cleared this peer_data by calling `_on_peer_disconnected` for all the peers.");
}

void SceneSynchronizerBase::detect_and_signal_changed_variables(int p_flags) {
	// Pull the changes.
	if (event_flag != p_flags) {
		// The flag was not set yet.
		change_events_begin(p_flags);
	}

	for (auto od : objects_data_storage.get_objects_data()) {
		if (od) {
			pull_node_changes(od);
		}
	}
	change_events_flush();
}

void SceneSynchronizerBase::change_events_begin(int p_flag) {
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

void SceneSynchronizerBase::change_event_add(NS::ObjectData *p_object_data, VarId p_var_id, const Variant &p_old) {
	for (int i = 0; i < int(p_object_data->vars[p_var_id.id].changes_listeners.size()); i += 1) {
		ChangesListener *listener = p_object_data->vars[p_var_id.id].changes_listeners[i];
		// This can't be `nullptr` because when the changes listener is dropped
		// all the pointers are cleared.
		CRASH_COND(listener == nullptr);

		if ((listener->flag & event_flag) == 0) {
			// Not listening to this event.
			continue;
		}

		listener->emitted = false;

		int v = 0;
		for (auto wv : listener->watching_vars) {
			if (wv.var_id == p_var_id) {
				wv.old_set = true;
				listener->old_values[v] = p_old;
			}
			v += 1;
		}
	}

	// Notify the synchronizer.
	if (synchronizer) {
		synchronizer->on_variable_changed(
				p_object_data,
				p_var_id,
				p_old,
				event_flag);
	}
}

void SceneSynchronizerBase::change_events_flush() {
	for (uint32_t listener_i = 0; listener_i < changes_listeners.size(); listener_i += 1) {
		ChangesListener &listener = *changes_listeners[listener_i];
		if (listener.emitted) {
			// Nothing to do.
			continue;
		}
		listener.emitted = true;

		for (uint32_t v = 0; v < listener.watching_vars.size(); v += 1) {
			if (!listener.watching_vars[v].old_set) {
				// Old is not set, so set the current valud.
				listener.old_values[v] =
						listener.watching_vars[v].node_data->vars[listener.watching_vars[v].var_id.id].var.value;
			}
			// Reset this to false.
			listener.watching_vars[v].old_set = false;
		}

		listener.listener_func(listener.old_values);
	}

	recover_in_progress = false;
	reset_in_progress = false;
	rewinding_in_progress = false;
	end_sync = false;
}

void SceneSynchronizerBase::drop_object_data(NS::ObjectData &p_object_data) {
	synchronizer_manager->on_drop_object_data(p_object_data);

	if (synchronizer) {
		synchronizer->on_object_data_removed(p_object_data);
	}

	if (p_object_data.get_controller()) {
		// This is a controller, make sure to reset the peers.
		p_object_data.get_controller()->notify_registered_with_synchronizer(nullptr, p_object_data);
		dirty_peers();
	}

	// Remove this `NodeData` from any event listener.
	for (auto cl : changes_listeners) {
		for (auto wv : cl->watching_vars) {
			if (wv.node_data == &p_object_data) {
				// We can't remove this entirely, otherwise we change that the user expects.
				wv.node_data = nullptr;
				wv.var_id = VarId::NONE;
			}
		}
	}

	if (p_object_data.has_registered_process_functions()) {
		process_functions__clear();
	}

	objects_data_storage.deallocate_object_data(p_object_data);
}

void SceneSynchronizerBase::notify_object_data_net_id_changed(ObjectData &p_object_data) {
	if (p_object_data.has_registered_process_functions()) {
		process_functions__clear();
	}
	SceneSynchronizerDebugger::singleton()->debug_print(network_interface, "ObjectNetId: " + itos(p_object_data.get_net_id().id) + " just assigned to: " + String(p_object_data.object_name.c_str()));
}

NetworkedControllerBase *SceneSynchronizerBase::fetch_controller_by_peer(int peer) {
	const NS::PeerData *data = MapFunc::at(peer_data, peer);
	if (data && data->controller_id != ObjectNetId::NONE) {
		NS::ObjectData *nd = get_object_data(data->controller_id);
		if (nd) {
			return nd->get_controller();
		}
	}
	return nullptr;
}

bool SceneSynchronizerBase::compare(const Vector2 &p_first, const Vector2 &p_second) const {
	return compare(p_first, p_second, comparison_float_tolerance);
}

bool SceneSynchronizerBase::compare(const Vector3 &p_first, const Vector3 &p_second) const {
	return compare(p_first, p_second, comparison_float_tolerance);
}

bool SceneSynchronizerBase::compare(const Variant &p_first, const Variant &p_second) const {
	return compare(p_first, p_second, comparison_float_tolerance);
}

bool SceneSynchronizerBase::compare(const Vector2 &p_first, const Vector2 &p_second, real_t p_tolerance) {
	return Math::is_equal_approx(p_first.x, p_second.x, p_tolerance) &&
			Math::is_equal_approx(p_first.y, p_second.y, p_tolerance);
}

bool SceneSynchronizerBase::compare(const Vector3 &p_first, const Vector3 &p_second, real_t p_tolerance) {
	return Math::is_equal_approx(p_first.x, p_second.x, p_tolerance) &&
			Math::is_equal_approx(p_first.y, p_second.y, p_tolerance) &&
			Math::is_equal_approx(p_first.z, p_second.z, p_tolerance);
}

bool SceneSynchronizerBase::compare(const Variant &p_first, const Variant &p_second, real_t p_tolerance) {
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

bool SceneSynchronizerBase::is_server() const {
	return synchronizer_type == SYNCHRONIZER_TYPE_SERVER;
}

bool SceneSynchronizerBase::is_client() const {
	return synchronizer_type == SYNCHRONIZER_TYPE_CLIENT;
}

bool SceneSynchronizerBase::is_no_network() const {
	return synchronizer_type == SYNCHRONIZER_TYPE_NONETWORK;
}

bool SceneSynchronizerBase::is_networked() const {
	return is_client() || is_server();
}

void SceneSynchronizerBase::update_nodes_relevancy() {
	synchronizer_manager->update_nodes_relevancy();

	const bool log_debug_nodes_relevancy_update = ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/log_debug_nodes_relevancy_update");
	if (log_debug_nodes_relevancy_update) {
		static_cast<ServerSynchronizer *>(synchronizer)->sync_group_debug_print();
	}
}

void SceneSynchronizerBase::process_functions__clear() {
	cached_process_functions_valid = false;
}

void SceneSynchronizerBase::process_functions__execute(const double p_delta) {
	if (cached_process_functions_valid == false) {
		// Clear the process_functions.
		for (int process_phase = PROCESSPHASE_EARLY; process_phase < PROCESSPHASE_COUNT; ++process_phase) {
			cached_process_functions[process_phase].clear();
		}

		// Build the cached_process_functions, making sure the node data order is kept.
		for (auto od : objects_data_storage.get_sorted_objects_data()) {
			if (od == nullptr || (is_client() && od->realtime_sync_enabled_on_client == false)) {
				// Nothing to process
				continue;
			}

			// For each valid NodeData.
			for (int process_phase = PROCESSPHASE_EARLY; process_phase < PROCESSPHASE_COUNT; ++process_phase) {
				// Append the contained functions.
				cached_process_functions[process_phase].append(od->functions[process_phase]);
			}
		}

		cached_process_functions_valid = true;
	}

	SceneSynchronizerDebugger::singleton()->debug_print(network_interface, "Process functions START", true);

	for (int process_phase = PROCESSPHASE_EARLY; process_phase < PROCESSPHASE_COUNT; ++process_phase) {
		cached_process_functions[process_phase].broadcast(p_delta);
	}
}

ObjectLocalId SceneSynchronizerBase::find_object_local_id(ObjectHandle p_app_object) const {
	return objects_data_storage.find_object_local_id(p_app_object);
}

ObjectLocalId SceneSynchronizerBase::find_object_local_id(const NetworkedControllerBase &p_controller) const {
	return objects_data_storage.find_object_local_id(p_controller);
}

NS::ObjectData *SceneSynchronizerBase::get_object_data(ObjectLocalId p_id, bool p_expected) {
	return objects_data_storage.get_object_data(p_id, p_expected);
}

const NS::ObjectData *SceneSynchronizerBase::get_object_data(ObjectLocalId p_id, bool p_expected) const {
	return objects_data_storage.get_object_data(p_id, p_expected);
}

ObjectData *SceneSynchronizerBase::get_object_data(ObjectNetId p_id, bool p_expected) {
	return objects_data_storage.get_object_data(p_id, p_expected);
}

const ObjectData *SceneSynchronizerBase::get_object_data(ObjectNetId p_id, bool p_expected) const {
	return objects_data_storage.get_object_data(p_id, p_expected);
}

NetworkedControllerBase *SceneSynchronizerBase::get_controller_for_peer(int p_peer, bool p_expected) {
	const NS::PeerData *pd = MapFunc::at(peer_data, p_peer);
	if (p_expected) {
		ERR_FAIL_COND_V_MSG(pd == nullptr, nullptr, "The peer is unknown `" + itos(p_peer) + "`.");
	}
	NS::ObjectData *nd = get_object_data(pd->controller_id, p_expected);
	if (nd) {
		return nd->get_controller();
	}
	return nullptr;
}

const NetworkedControllerBase *SceneSynchronizerBase::get_controller_for_peer(int p_peer, bool p_expected) const {
	const NS::PeerData *pd = MapFunc::at(peer_data, p_peer);
	if (p_expected) {
		ERR_FAIL_COND_V_MSG(pd == nullptr, nullptr, "The peer is unknown `" + itos(p_peer) + "`.");
	}
	const NS::ObjectData *nd = get_object_data(pd->controller_id, p_expected);
	if (nd) {
		return nd->get_controller();
	}
	return nullptr;
}

NS::PeerData *SceneSynchronizerBase::get_peer_for_controller(const NetworkedControllerBase &p_controller, bool p_expected) {
	for (auto &it : peer_data) {
		if (it.first == p_controller.network_interface->get_unit_authority()) {
			return &(it.second);
		}
	}
	if (p_expected) {
		ERR_PRINT("The controller was not associated to a peer.");
	}
	return nullptr;
}

const NS::PeerData *SceneSynchronizerBase::get_peer_for_controller(const NetworkedControllerBase &p_controller, bool p_expected) const {
	for (auto &it : peer_data) {
		if (it.first == p_controller.network_interface->get_unit_authority()) {
			return &(it.second);
		}
	}
	if (p_expected) {
		ERR_PRINT("The controller was not associated to a peer.");
	}
	return nullptr;
}

ObjectNetId SceneSynchronizerBase::get_biggest_node_id() const {
	return objects_data_storage.get_sorted_objects_data().size() == 0 ? ObjectNetId::NONE : ObjectNetId{ uint32_t(objects_data_storage.get_sorted_objects_data().size() - 1) };
}

void SceneSynchronizerBase::reset_controllers() {
	for (auto od : objects_data_storage.get_controllers_objects_data()) {
		reset_controller(od);
	}
}

void SceneSynchronizerBase::reset_controller(NS::ObjectData *p_controller_nd) {
#ifdef DEBUG_ENABLED
	// This can't happen because the callers make sure the `NodeData` is a
	// controller.
	CRASH_COND(p_controller_nd->get_controller() == nullptr);
#endif

	NetworkedControllerBase *controller = p_controller_nd->get_controller();

	// Reset the controller type.
	if (controller->controller != nullptr) {
		memdelete(controller->controller);
		controller->controller = nullptr;
		controller->controller_type = NetworkedControllerBase::CONTROLLER_TYPE_NULL;
	}

	if (!synchronizer_manager) {
		if (synchronizer) {
			synchronizer->on_controller_reset(p_controller_nd);
		}

		// Nothing to do.
		return;
	}

	if (!network_interface->is_local_peer_networked()) {
		controller->controller_type = NetworkedControllerBase::CONTROLLER_TYPE_NONETWORK;
		controller->controller = memnew(NoNetController(controller));
	} else if (network_interface->is_local_peer_server()) {
		if (controller->get_server_controlled()) {
			controller->controller_type = NetworkedControllerBase::CONTROLLER_TYPE_AUTONOMOUS_SERVER;
			controller->controller = memnew(AutonomousServerController(controller));
		} else {
			controller->controller_type = NetworkedControllerBase::CONTROLLER_TYPE_SERVER;
			controller->controller = memnew(ServerController(controller, controller->get_network_traced_frames()));
		}
	} else if (controller->network_interface->is_local_peer_authority_of_this_unit() && controller->get_server_controlled() == false) {
		controller->controller_type = NetworkedControllerBase::CONTROLLER_TYPE_PLAYER;
		controller->controller = memnew(PlayerController(controller));
	} else {
		controller->controller_type = NetworkedControllerBase::CONTROLLER_TYPE_DOLL;
		controller->controller = memnew(DollController(controller));
	}

	dirty_peers();
	controller->controller->ready();
	controller->notify_controller_reset();

	if (synchronizer) {
		synchronizer->on_controller_reset(p_controller_nd);
	}
}

void SceneSynchronizerBase::pull_node_changes(NS::ObjectData *p_object_data) {
	for (VarId var_id = { 0 }; var_id < VarId{ p_object_data->vars.size() }; var_id += 1) {
		if (p_object_data->vars[var_id.id].enabled == false) {
			continue;
		}

		const Variant old_val = p_object_data->vars[var_id.id].var.value;
		Variant new_val;
		synchronizer_manager->get_variable(
				p_object_data->app_object_handle,
				String(p_object_data->vars[var_id.id].var.name).utf8(),
				new_val);

		if (!compare(old_val, new_val)) {
			p_object_data->vars[var_id.id].var.value = new_val.duplicate(true);
			change_event_add(
					p_object_data,
					var_id,
					old_val);
		}
	}
}

Synchronizer::Synchronizer(SceneSynchronizerBase *p_node) :
		scene_synchronizer(p_node) {
}

NoNetSynchronizer::NoNetSynchronizer(SceneSynchronizerBase *p_node) :
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

ServerSynchronizer::ServerSynchronizer(SceneSynchronizerBase *p_node) :
		Synchronizer(p_node) {
	CRASH_COND(SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID != sync_group_create());
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
	for (auto &peer_it : scene_synchronizer->peer_data) {
		if (unlikely(peer_it.second.controller_id == ObjectNetId::NONE)) {
			continue;
		}

		const NS::ObjectData *nd = scene_synchronizer->get_object_data(peer_it.second.controller_id);
		const uint32_t current_input_id = nd->get_controller()->get_server_controller()->get_current_input_id();
		SceneSynchronizerDebugger::singleton()->write_dump(peer_it.first, current_input_id);
	}
	SceneSynchronizerDebugger::singleton()->start_new_frame();
#endif
}

void ServerSynchronizer::on_peer_connected(int p_peer_id) {
	sync_group_move_peer_to(p_peer_id, SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID);
}

void ServerSynchronizer::on_peer_disconnected(int p_peer_id) {
	for (uint32_t i = 0; i < sync_groups.size(); ++i) {
		sync_groups[i].peers.erase(p_peer_id);
	}
}

void ServerSynchronizer::on_object_data_added(NS::ObjectData *p_object_data) {
#ifdef DEBUG_ENABLED
	// Can't happen on server
	CRASH_COND(scene_synchronizer->is_recovered());
	// On server the ID is always known.
	CRASH_COND(p_object_data->get_net_id() == ObjectNetId::NONE);
#endif

	sync_groups[SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID].add_new_node(p_object_data, true);

	if (p_object_data->get_controller()) {
		// It was added a new NodeData with a controller, make sure to mark
		// its peer as `need_full_snapshot` ASAP.
		NS::PeerData *pd = scene_synchronizer->get_peer_for_controller(*p_object_data->get_controller());
		if (pd) {
			pd->force_notify_snapshot = true;
			pd->need_full_snapshot = true;
		}
	}
}

void ServerSynchronizer::on_object_data_removed(NS::ObjectData &p_object_data) {
	// Make sure to remove this `NodeData` from any sync group.
	for (uint32_t i = 0; i < sync_groups.size(); ++i) {
		sync_groups[i].remove_node(&p_object_data);
	}
}

void ServerSynchronizer::on_variable_added(NS::ObjectData *p_object_data, const StringName &p_var_name) {
#ifdef DEBUG_ENABLED
	// Can't happen on server
	CRASH_COND(scene_synchronizer->is_recovered());
	// On server the ID is always known.
	CRASH_COND(p_object_data->get_net_id() == ObjectNetId::NONE);
#endif

	for (uint32_t g = 0; g < sync_groups.size(); ++g) {
		sync_groups[g].notify_new_variable(p_object_data, p_var_name);
	}
}

void ServerSynchronizer::on_variable_changed(NS::ObjectData *p_object_data, VarId p_var_id, const Variant &p_old_value, int p_flag) {
#ifdef DEBUG_ENABLED
	// Can't happen on server
	CRASH_COND(scene_synchronizer->is_recovered());
	// On server the ID is always known.
	CRASH_COND(p_object_data->get_net_id() == ObjectNetId::NONE);
#endif

	for (uint32_t g = 0; g < sync_groups.size(); ++g) {
		sync_groups[g].notify_variable_changed(p_object_data, p_object_data->vars[p_var_id.id].var.name);
	}
}

SyncGroupId ServerSynchronizer::sync_group_create() {
	const SyncGroupId id = sync_groups.size();
	sync_groups.resize(id + 1);
	return id;
}

const NS::SyncGroup *ServerSynchronizer::sync_group_get(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(p_group_id >= sync_groups.size(), nullptr, "The group id `" + itos(p_group_id) + "` doesn't exist.");
	return &sync_groups[p_group_id];
}

void ServerSynchronizer::sync_group_add_node(NS::ObjectData *p_object_data, SyncGroupId p_group_id, bool p_realtime) {
	ERR_FAIL_COND(p_object_data == nullptr);
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id].add_new_node(p_object_data, p_realtime);
}

void ServerSynchronizer::sync_group_remove_node(NS::ObjectData *p_object_data, SyncGroupId p_group_id) {
	ERR_FAIL_COND(p_object_data == nullptr);
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id].remove_node(p_object_data);
}

void ServerSynchronizer::sync_group_replace_nodes(SyncGroupId p_group_id, LocalVector<NS::SyncGroup::RealtimeNodeInfo> &&p_new_realtime_nodes, LocalVector<NS::SyncGroup::DeferredNodeInfo> &&p_new_deferred_nodes) {
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id].replace_nodes(std::move(p_new_realtime_nodes), std::move(p_new_deferred_nodes));
}

void ServerSynchronizer::sync_group_remove_all_nodes(SyncGroupId p_group_id) {
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id].remove_all_nodes();
}

void ServerSynchronizer::sync_group_move_peer_to(int p_peer_id, SyncGroupId p_group_id) {
	// remove the peer from any sync_group.
	for (uint32_t i = 0; i < sync_groups.size(); ++i) {
		sync_groups[i].peers.erase(p_peer_id);
	}

	if (p_group_id == UINT32_MAX) {
		// This peer is not listening to anything.
		return;
	}

	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	sync_groups[p_group_id].peers.push_back(p_peer_id);

	// Also mark the peer as need full snapshot, as it's into a new group now.
	NS::PeerData *pd = MapFunc::at(scene_synchronizer->peer_data, p_peer_id);
	ERR_FAIL_COND(pd == nullptr);
	pd->force_notify_snapshot = true;
	pd->need_full_snapshot = true;

	// Make sure the controller is added into this group.
	NS::ObjectData *nd = scene_synchronizer->get_object_data(pd->controller_id, false);
	if (nd) {
		sync_group_add_node(nd, p_group_id, true);
	}
}

const LocalVector<int> *ServerSynchronizer::sync_group_get_peers(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(p_group_id >= sync_groups.size(), nullptr, "The group id `" + itos(p_group_id) + "` doesn't exist.");
	return &sync_groups[p_group_id].peers;
}

void ServerSynchronizer::sync_group_set_deferred_update_rate(NS::ObjectData *p_object_data, SyncGroupId p_group_id, real_t p_update_rate) {
	ERR_FAIL_COND(p_object_data == nullptr);
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id].set_deferred_update_rate(p_object_data, p_update_rate);
}

real_t ServerSynchronizer::sync_group_get_deferred_update_rate(const NS::ObjectData *p_object_data, SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V(p_object_data == nullptr, 0.0);
	ERR_FAIL_COND_V_MSG(p_group_id >= sync_groups.size(), 0.0, "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_V_MSG(p_group_id == SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID, 0.0, "You can't change this SyncGroup in any way. Create a new one.");
	return sync_groups[p_group_id].get_deferred_update_rate(p_object_data);
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
		NS::SyncGroup &group = sync_groups[g];

		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "| [Group " + itos(g) + "#]");
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|    Listening peers");
		for (int peer : group.peers) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|      |- " + itos(peer));
		}

		const LocalVector<NS::SyncGroup::RealtimeNodeInfo> &realtime_node_info = group.get_realtime_sync_nodes();
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|");
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|    [Realtime nodes]");
		for (auto info : realtime_node_info) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|      |- " + String(info.nd->object_name.c_str()));
		}

		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|");

		const LocalVector<NS::SyncGroup::DeferredNodeInfo> &deferred_node_info = group.get_deferred_sync_nodes();
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|    [Deferred nodes (UR: Update Rate)]");
		for (auto info : deferred_node_info) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|      |- [UR: " + rtos(info.update_rate) + "] " + info.nd->object_name.c_str());
		}
	}
	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|-----------------------");
	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "");
}

void ServerSynchronizer::process_snapshot_notificator(real_t p_delta) {
	if (scene_synchronizer->peer_data.empty()) {
		// No one is listening.
		return;
	}

	for (int g = 0; g < int(sync_groups.size()); ++g) {
		NS::SyncGroup &group = sync_groups[g];

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
			NS::PeerData *peer = MapFunc::at(scene_synchronizer->peer_data, peer_id);
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

			NS::ObjectData *nd = scene_synchronizer->get_object_data(peer->controller_id, false);

			if (nd) {
				CRASH_COND_MSG(nd->get_controller() == nullptr, "The NodeData fetched is not a controller: `" + String(nd->object_name.c_str()) + "`, this is not supposed to happen.");

				// Add the controller input id at the beginning of the snapshot.
				snap.push_back(true);
				NetworkedControllerBase *controller = nd->get_controller();
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

			scene_synchronizer->rpc_handler_state.rpc(
					scene_synchronizer->get_network_interface(),
					peer_id,
					snap);

			if (nd) {
				NetworkedControllerBase *controller = nd->get_controller();
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
		const NS::SyncGroup &p_group) const {
	const LocalVector<NS::SyncGroup::RealtimeNodeInfo> &relevant_node_data = p_group.get_realtime_sync_nodes();

	Vector<Variant> snapshot_data;

	// First insert the list of ALL enabled nodes, if changed.
	if (p_group.is_realtime_node_list_changed() || p_force_full_snapshot) {
		snapshot_data.push_back(true);
		// Here we create a bit array: The bit position is significant as it
		// refers to a specific ID, the bit is set to 1 if the Node is relevant
		// to this group.
		BitArray bit_array;
		bit_array.resize_in_bits(scene_synchronizer->objects_data_storage.get_sorted_objects_data().size());
		bit_array.zero();
		for (uint32_t i = 0; i < relevant_node_data.size(); i += 1) {
			const NS::ObjectData *nd = relevant_node_data[i].nd;
			CRASH_COND(nd->get_net_id() == ObjectNetId::NONE);
			bit_array.store_bits(nd->get_net_id().id, 1, 1);
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
						NS::SyncGroup::Change(),
						snapshot_data);
			}
		}
	}

	const SnapshotGenerationMode mode = p_force_full_snapshot ? SNAPSHOT_GENERATION_MODE_FORCE_FULL : SNAPSHOT_GENERATION_MODE_NORMAL;

	// Then, generate the snapshot for the relevant nodes.
	for (uint32_t i = 0; i < relevant_node_data.size(); i += 1) {
		const NS::ObjectData *node_data = relevant_node_data[i].nd;

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
		const NS::ObjectData *p_object_data,
		SnapshotGenerationMode p_mode,
		const NS::SyncGroup::Change &p_change,
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

	if (p_object_data->app_object_handle == ObjectHandle::NONE) {
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
		_snap_node_data.write[0] = p_object_data->get_net_id().id;
		_snap_node_data.write[1] = p_object_data->object_name.c_str();
		snap_node_data = _snap_node_data;
	} else {
		// This node is already known on clients, just set the node ID.
		snap_node_data = p_object_data->get_net_id().id;
	}

	if ((node_has_changes && skip_snapshot_variables == false) || force_snapshot_node_path || unknown) {
		r_snapshot_data.push_back(snap_node_data);
	} else {
		// It has no changes, skip this node.
		return;
	}

	if (force_snapshot_variables || (node_has_changes && skip_snapshot_variables == false)) {
		// Insert the node variables.
		for (uint32_t i = 0; i < p_object_data->vars.size(); i += 1) {
			const NS::VarDescriptor &var = p_object_data->vars[i];
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
				_var_info.write[0] = var.id.id;
				_var_info.write[1] = var.var.name;
				var_info = _var_info;
			} else {
				var_info = var.id.id;
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
		NS::SyncGroup &group = sync_groups[g];

		if (group.peers.size() == 0) {
			// No one is interested to this group.
			continue;
		}

		LocalVector<NS::SyncGroup::DeferredNodeInfo> &node_info = group.get_deferred_sync_nodes();
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

			if (node_info[i].nd->get_net_id().id > UINT16_MAX) {
				SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "[FATAL] The `process_deferred_sync` found a node with ID `" + itos(node_info[i].nd->get_net_id().id) + "::" + node_info[i].nd->object_name.c_str() + "` that exceedes the max ID this function can network at the moment. Please report this, we will consider improving this function.");
				send = false;
			}

			if (node_info[i].nd->collect_epoch_func.is_null()) {
				SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The `process_deferred_sync` found a node `" + itos(node_info[i].nd->get_net_id().id) + "::" + node_info[i].nd->object_name.c_str() + "` with an invalid function `collect_epoch_func`. Please use `setup_deferred_sync` to correctly initialize this node for deferred sync.");
				send = false;
			}

			if (send) {
				node_info[i]._update_priority = 0.0;

				// Read the state and write into the tmp_buffer:
				tmp_buffer->begin_write(0);

				Callable::CallError e;
				node_info[i].nd->collect_epoch_func.callp(&fake_array_vars, 1, r, e);

				if (e.error != Callable::CallError::CALL_OK) {
					SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The `process_deferred_sync` was not able to execute the function `" + node_info[i].nd->collect_epoch_func.get_method() + "` for the node `" + itos(node_info[i].nd->get_net_id().id) + "::" + node_info[i].nd->object_name.c_str() + "`.");
					continue;
				}

				if (tmp_buffer->total_size() > UINT16_MAX) {
					SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The `process_deferred_sync` failed because the method `" + node_info[i].nd->collect_epoch_func.get_method() + "` for the node `" + itos(node_info[i].nd->get_net_id().id) + "::" + node_info[i].nd->object_name.c_str() + "` collected more than " + itos(UINT16_MAX) + " bits. Please optimize your netcode to send less data.");
					continue;
				}

				++update_node_count;

				if (node_info[i].nd->get_net_id().id > UINT8_MAX) {
					global_buffer.add_bool(true);
					global_buffer.add_uint(node_info[i].nd->get_net_id().id, DataBuffer::COMPRESSION_LEVEL_2);
				} else {
					global_buffer.add_bool(false);
					global_buffer.add_uint(node_info[i].nd->get_net_id().id, DataBuffer::COMPRESSION_LEVEL_3);
				}

				// Collapse the two DataBuffer.
				global_buffer.add_uint(uint32_t(tmp_buffer->total_size()), DataBuffer::COMPRESSION_LEVEL_2);
				global_buffer.add_bits(tmp_buffer->get_buffer().get_bytes().ptr(), tmp_buffer->total_size());

			} else {
				node_info[i]._update_priority += node_info[i].update_rate;
			}
		}

		if (update_node_count > 0) {
			global_buffer.dry();
			for (int i = 0; i < int(group.peers.size()); ++i) {
				scene_synchronizer->rpc_handler_deferred_sync_data.rpc(
						scene_synchronizer->get_network_interface(),
						group.peers[i],
						global_buffer.get_buffer().get_bytes());
			}
		}
	}

	memdelete(tmp_buffer);
}

ClientSynchronizer::ClientSynchronizer(SceneSynchronizerBase *p_node) :
		Synchronizer(p_node) {
	clear();

	notify_server_full_snapshot_is_needed();
}

void ClientSynchronizer::clear() {
	player_controller_node_data = nullptr;
	objects_names.clear();
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

	const double physics_ticks_per_second = Engine::get_singleton()->get_physics_ticks_per_second();
	const double delta = 1.0 / physics_ticks_per_second;

#ifdef DEBUG_ENABLED
	if (unlikely(Engine::get_singleton()->get_frames_per_second() < physics_ticks_per_second)) {
		const bool silent = !ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/debugger/log_debug_fps_warnings");
		SceneSynchronizerDebugger::singleton()->debug_warning(&scene_synchronizer->get_network_interface(), "Current FPS is " + itos(Engine::get_singleton()->get_frames_per_second()) + ", but the minimum required FPS is " + itos(physics_ticks_per_second) + ", the client is unable to generate enough inputs for the server.", silent);
	}
#endif

	process_simulation(delta, physics_ticks_per_second);

	process_received_server_state(delta);

	// Now trigger the END_SYNC event.
	signal_end_sync_changed_variables_events();

	process_received_deferred_sync_data(delta);

#if DEBUG_ENABLED
	if (player_controller_node_data) {
		NetworkedControllerBase *controller = player_controller_node_data->get_controller();
		PlayerController *player_controller = controller->get_player_controller();
		const int client_peer = scene_synchronizer->network_interface->fetch_local_peer_id();
		SceneSynchronizerDebugger::singleton()->write_dump(client_peer, player_controller->get_current_input_id());
		SceneSynchronizerDebugger::singleton()->start_new_frame();
	}
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

void ClientSynchronizer::on_object_data_added(NS::ObjectData *p_object_data) {
}

void ClientSynchronizer::on_object_data_removed(NS::ObjectData &p_object_data) {
	if (player_controller_node_data == &p_object_data) {
		player_controller_node_data = nullptr;
		server_snapshots.clear();
		client_snapshots.clear();
	}

	if (p_object_data.get_net_id().id < uint32_t(last_received_snapshot.node_vars.size())) {
		last_received_snapshot.node_vars.ptrw()[p_object_data.get_net_id().id].clear();
	}

	remove_node_from_deferred_sync(&p_object_data);
}

void ClientSynchronizer::on_variable_changed(NS::ObjectData *p_object_data, VarId p_var_id, const Variant &p_old_value, int p_flag) {
	if (p_flag & NetEventFlag::SYNC) {
		sync_end_events.insert(
				EndSyncEvent{
						p_object_data,
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
					e->get().node_data->vars[e->get().var_id.id].var.value,
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

void ClientSynchronizer::on_controller_reset(NS::ObjectData *p_object_data) {
#ifdef DEBUG_ENABLED
	CRASH_COND(p_object_data->get_controller() == nullptr);
#endif

	if (player_controller_node_data == p_object_data) {
		// Reset the node_data.
		player_controller_node_data = nullptr;
		server_snapshots.clear();
		client_snapshots.clear();
	}

	if (p_object_data->get_controller()->is_player_controller()) {
		if (player_controller_node_data != nullptr) {
			SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "Only one player controller is supported, at the moment. Make sure this is the case.");
		} else {
			// Set this player controller as active.
			player_controller_node_data = p_object_data;
			server_snapshots.clear();
			client_snapshots.clear();
		}
	}
}

void ClientSynchronizer::store_snapshot() {
	NetworkedControllerBase *controller = player_controller_node_data->get_controller();

#ifdef DEBUG_ENABLED
	if (unlikely(client_snapshots.size() > 0 && controller->get_current_input_id() <= client_snapshots.back().input_id)) {
		CRASH_NOW_MSG("[FATAL] During snapshot creation, for controller " + String(player_controller_node_data->object_name.c_str()) + ", was found an ID for an older snapshots. New input ID: " + itos(controller->get_current_input_id()) + " Last saved snapshot input ID: " + itos(client_snapshots.back().input_id) + ".");
	}
#endif

	client_snapshots.push_back(NS::Snapshot());

	NS::Snapshot &snap = client_snapshots.back();
	snap.input_id = controller->get_current_input_id();

	update_client_snapshot(snap);
}

void ClientSynchronizer::store_controllers_snapshot(
		const NS::Snapshot &p_snapshot,
		std::deque<NS::Snapshot> &r_snapshot_storage) {
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

void ClientSynchronizer::process_simulation(real_t p_delta, real_t p_physics_ticks_per_second) {
	if (unlikely(player_controller_node_data == nullptr || enabled == false)) {
		// No player controller so can't process the simulation.
		// TODO Remove this constraint?

		// Make sure to fetch changed variable anyway.
		scene_synchronizer->detect_and_signal_changed_variables(NetEventFlag::CHANGE);
		return;
	}

	NetworkedControllerBase *controller = player_controller_node_data->get_controller();
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
	int sub_ticks = player_controller->calculates_sub_ticks(p_delta, p_physics_ticks_per_second);

	if (sub_ticks == 0) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "No sub ticks: this is not bu a bug; it's the lag compensation algorithm.", true);
	}

	while (sub_ticks > 0) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "ClientSynchronizer::process::sub_process " + itos(sub_ticks), true);
		SceneSynchronizerDebugger::singleton()->scene_sync_process_start(scene_synchronizer);

		// Process the scene.
		scene_synchronizer->process_functions__execute(p_delta);

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
}

void ClientSynchronizer::process_received_server_state(real_t p_delta) {
	// The client is responsible to recover only its local controller, while all
	// the other controllers_node_data (dolls) have their state interpolated. There is
	// no need to check the correctness of the doll state nor the needs to
	// rewind those.
	//
	// The scene, (global nodes), are always in sync with the reference frame
	// of the client.

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

	if (!player_controller_node_data) {
		// There is no player controller, we can't apply any snapshot which
		// `input_id` is not UINT32_MAX.
		return;
	}

	NetworkedControllerBase *controller = player_controller_node_data->get_controller();
	PlayerController *player_controller = controller->get_player_controller();

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
	NS::Snapshot no_rewind_recover;

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
		NS::Snapshot &r_no_rewind_recover) {
	LocalVector<String> differences_info;

#ifdef DEBUG_ENABLED
	LocalVector<ObjectNetId> different_node_data;
	const bool is_equal = NS::Snapshot::compare(
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
		const Vector<NS::NameAndVar> const_empty_vector;

		// Emit the de-sync detected signal.
		for (
				int i = 0;
				i < int(different_node_data.size());
				i += 1) {
			const ObjectNetId net_node_id = different_node_data[i];
			NS::ObjectData *rew_node_data = scene_synchronizer->get_object_data(net_node_id);

			const Vector<NS::NameAndVar> &server_node_vars = ObjectNetId{ uint32_t(server_snapshots.front().node_vars.size()) } <= net_node_id ? const_empty_vector : server_snapshots.front().node_vars[net_node_id.id];
			const Vector<NS::NameAndVar> &client_node_vars = ObjectNetId{ uint32_t(client_snapshots.front().node_vars.size()) } <= net_node_id ? const_empty_vector : client_snapshots.front().node_vars[net_node_id.id];

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

			scene_synchronizer->event_desync_detected.broadcast(p_input_id, rew_node_data->app_object_handle, variable_names, client_values, server_values);
		}
	}
#else
	const bool is_equal = NS::Snapshot::compare(
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

	const NS::Snapshot &server_snapshot = server_snapshots.front();
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
		NS::ObjectData *p_local_controller_node,
		NetworkedControllerBase *p_local_controller,
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

void ClientSynchronizer::__pcr__sync__no_rewind(const NS::Snapshot &p_no_rewind_recover) {
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
		void (*p_node_parse)(void *p_user_pointer, NS::ObjectData *p_object_data),
		void (*p_input_id_parse)(void *p_user_pointer, uint32_t p_input_id),
		void (*p_controller_parse)(void *p_user_pointer, NS::ObjectData *p_object_data),
		void (*p_variable_parse)(void *p_user_pointer, NS::ObjectData *p_object_data, VarId p_var_id, const Variant &p_value),
		void (*p_node_activation_parse)(void *p_user_pointer, NS::ObjectData *p_object_data, bool p_is_active)) {
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

	NS::ObjectData *synchronizer_node_data = nullptr;
	VarId var_id = VarId::NONE;

	for (; snap_data_index < raw_snapshot.size(); snap_data_index += 1) {
		const Variant v = raw_snapshot_ptr[snap_data_index];
		if (synchronizer_node_data == nullptr) {
			// Node is null so we expect `v` has the node info.

			bool skip_this_node = false;
			ObjectHandle app_object_handle = ObjectHandle::NONE;
			ObjectNetId net_node_id = ObjectNetId::NONE;
			std::string object_name;

			if (v.is_array()) {
				// Node info are in verbose form, extract it.

				const Vector<Variant> node_data = v;
				ERR_FAIL_COND_V(node_data.size() != 2, false);
				ERR_FAIL_COND_V_MSG(node_data[0].get_type() != Variant::INT, false, "This snapshot is corrupted.");
				ERR_FAIL_COND_V_MSG(node_data[1].get_type() != Variant::STRING, false, "This snapshot is corrupted.");

				net_node_id.id = node_data[0];
				object_name = String(node_data[1]).utf8();

				// Associate the ID with the path.
				objects_names.insert(std::pair(net_node_id, object_name));

			} else if (v.get_type() == Variant::INT) {
				// Node info are in short form.
				net_node_id.id = v;
				NS::ObjectData *nd = scene_synchronizer->get_object_data(net_node_id);
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
					const std::string *object_name_ptr = NS::MapFunc::at(objects_names, net_node_id);

					if (object_name_ptr == nullptr) {
						// Was not possible lookup the node_path.
						SceneSynchronizerDebugger::singleton()->debug_warning(&scene_synchronizer->get_network_interface(), "The node with ID `" + itos(net_node_id.id) + "` is not know by this peer, this is not supposed to happen.");
						notify_server_full_snapshot_is_needed();
						skip_this_node = true;
						goto node_lookup_check;
					} else {
						object_name = *object_name_ptr;
					}
				}

				app_object_handle = scene_synchronizer->synchronizer_manager->fetch_app_object(object_name);
				if (app_object_handle == ObjectHandle::NONE) {
					// The node doesn't exists.
					SceneSynchronizerDebugger::singleton()->debug_warning(&scene_synchronizer->get_network_interface(), "The node " + String(object_name.c_str()) + " still doesn't exist.");
					skip_this_node = true;
					goto node_lookup_check;
				}

				// Register this node, so to make sure the client is tracking it.
				ObjectLocalId reg_obj_id;
				scene_synchronizer->register_app_object(app_object_handle, &reg_obj_id);
				if (reg_obj_id != ObjectLocalId::NONE) {
					ObjectData *nd = scene_synchronizer->get_object_data(reg_obj_id);
					// Set the node ID.
					// TODO this should not be done here.
					nd->set_net_id(net_node_id);
					synchronizer_node_data = nd;
				} else {
					SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "[BUG] This node " + String(object_name.c_str()) + " was not know on this client. Though, was not possible to register it.");
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
					SceneSynchronizerDebugger::singleton()->debug_warning(&scene_synchronizer->get_network_interface(), "This NetNodeId " + itos(net_node_id.id) + " doesn't exist on this client.");
				}
				continue;
			}

		node_lookup_out:

#ifdef DEBUG_ENABLED
			// At this point the ID is never UINT32_MAX thanks to the above
			// mechanism.
			CRASH_COND(synchronizer_node_data->get_net_id() == ObjectNetId::NONE);
#endif

			p_node_parse(p_user_pointer, synchronizer_node_data);

			if (synchronizer_node_data->get_controller()) {
				p_controller_parse(p_user_pointer, synchronizer_node_data);
			}

		} else if (var_id == VarId::NONE) {
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

				var_id.id = var_data[0];
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
										NS::VarDescriptor(
												var_id,
												variable_name,
												Variant(),
												skip_rewinding,
												enabled));
						SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The variable " + variable_name + " for the node " + synchronizer_node_data->object_name.c_str() + " was not known on this client. This should never happen, make sure to register the same nodes on the client and server.");
					}

					if (index != var_id.id) {
						if (synchronizer_node_data[var_id.id].get_net_id() != ObjectNetId::NONE) {
							// It's not expected because if index is different to
							// var_id, var_id should have a not yet initialized
							// variable.
							SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "This snapshot is corrupted. The var_id, at this point, must have a not yet init variable.");
							notify_server_full_snapshot_is_needed();
							return false;
						}

						// Make sure the variable is at the right index.
						SWAP(synchronizer_node_data->vars[index], synchronizer_node_data->vars[var_id.id]);
					}
				}

				// Make sure the ID is properly assigned.
				synchronizer_node_data->vars[var_id.id].id = var_id;

			} else if (v.get_type() == Variant::INT) {
				// The variable is stored in the compact form.

				var_id.id = v;

				if (var_id >= VarId{ uint32_t(synchronizer_node_data->vars.size()) } ||
						synchronizer_node_data->vars[var_id.id].id == VarId::NONE) {
					SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The var with ID `" + itos(var_id.id) + "` is not know by this peer, this is not supposed to happen.");

					notify_server_full_snapshot_is_needed();

					// Skip the next data since it's the value of this variable.
					snap_data_index += 1;
					var_id = VarId::NONE;
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
			var_id = VarId::NONE;
		}
	}

	// Fetch the active node list, and execute the callback to notify if the
	// node is active or not.
	{
		const uint8_t *active_node_list_byte_array_ptr = active_node_list_byte_array.ptr();
		ObjectNetId net_id = { 0 };
		for (int j = 0; j < active_node_list_byte_array.size(); ++j) {
			const uint8_t bit = active_node_list_byte_array_ptr[j];
			for (int offset = 0; offset < 8; ++offset) {
				if (net_id >= ObjectNetId{ uint32_t(scene_synchronizer->objects_data_storage.get_sorted_objects_data().size()) }) {
					// This check is needed because we are iterating the full 8 bits
					// into the byte: we don't have a count where to stop.
					break;
				}
				const int bit_mask = 1 << offset;
				const bool is_active = (bit & bit_mask) > 0;
				NS::ObjectData *nd = scene_synchronizer->get_object_data(net_id, false);
				if (nd) {
					p_node_activation_parse(p_user_pointer, nd, is_active);
				} else {
					if (is_active) {
						// This node data doesn't exist but it should be activated.
						SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The node_data `" + itos(net_id.id) + "` was not found on this client but the server marked this as realtime_sync_active node. This is likely a bug, pleasae report.");
						notify_server_full_snapshot_is_needed();
					}
				}
				net_id.id += 1;
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
		ObjectNetId node_id = ObjectNetId::NONE;
		if (future_epoch_buffer.read_bool()) {
			remaining_size = future_epoch_buffer.size() - future_epoch_buffer.get_bit_offset();
			if (remaining_size < future_epoch_buffer.get_uint_size(DataBuffer::COMPRESSION_LEVEL_2)) {
				// buffer entirely consumed, nothing else to do.
				break;
			}

			node_id.id = future_epoch_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_2);
		} else {
			if (remaining_size < future_epoch_buffer.get_uint_size(DataBuffer::COMPRESSION_LEVEL_3)) {
				// buffer entirely consumed, nothing else to do.
				break;
			}
			node_id.id = future_epoch_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_3);
		}

		remaining_size = future_epoch_buffer.size() - future_epoch_buffer.get_bit_offset();
		if (remaining_size < future_epoch_buffer.get_uint_size(DataBuffer::COMPRESSION_LEVEL_2)) {
			// buffer entirely consumed, nothing else to do.
			break;
		}
		const int buffer_bit_count = future_epoch_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_2);

		remaining_size = future_epoch_buffer.size() - future_epoch_buffer.get_bit_offset();
		if (remaining_size < buffer_bit_count) {
			SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "[FATAL] The function `receive_deferred_sync_data` failed applying the epoch because the received buffer is malformed. The node with ID `" + itos(node_id.id) + "` reported that the sub buffer size is `" + itos(buffer_bit_count) + "` but the main-buffer doesn't have so many bits.");
			break;
		}

		const int current_offset = future_epoch_buffer.get_bit_offset();
		const int expected_bit_offset_after_apply = current_offset + buffer_bit_count;

		NS::ObjectData *nd = scene_synchronizer->get_object_data(node_id, false);
		if (nd == nullptr) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The function `receive_deferred_sync_data` is skipping the node with ID `" + itos(node_id.id) + "` as it was not found locally.");
			future_epoch_buffer.seek(expected_bit_offset_after_apply);
			continue;
		}

		Vector<uint8_t> future_buffer_data;
		future_buffer_data.resize(Math::ceil(float(buffer_bit_count) / 8.0));
		future_epoch_buffer.read_bits(future_buffer_data.ptrw(), buffer_bit_count);
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

		NS::ObjectData *nd = stream.nd;
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

void ClientSynchronizer::remove_node_from_deferred_sync(NS::ObjectData *p_object_data) {
	const int64_t index = deferred_sync_array.find(p_object_data);
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

	NS::Snapshot received_snapshot = last_received_snapshot;
	received_snapshot.input_id = UINT32_MAX;

	struct ParseData {
		NS::Snapshot &snapshot;
		NS::ObjectData *player_controller_node_data;
		SceneSynchronizerBase *scene_synchronizer;
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
			[](void *p_user_pointer, NS::ObjectData *p_object_data) {
				ParseData *pd = static_cast<ParseData *>(p_user_pointer);

#ifdef DEBUG_ENABLED
				// This function should never receive undefined IDs.
				CRASH_COND(p_object_data->get_net_id() == ObjectNetId::NONE);
#endif

				// Make sure this node is part of the server node too.
				if (uint32_t(pd->snapshot.node_vars.size()) <= p_object_data->get_net_id().id) {
					pd->snapshot.node_vars.resize(p_object_data->get_net_id().id + 1);
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
			[](void *p_user_pointer, NS::ObjectData *p_object_data) {},

			// Parse variable:
			[](void *p_user_pointer, NS::ObjectData *p_object_data, VarId p_var_id, const Variant &p_value) {
				ParseData *pd = static_cast<ParseData *>(p_user_pointer);

				if (p_object_data->vars.size() != uint32_t(pd->snapshot.node_vars[p_object_data->get_net_id().id].size())) {
					// The parser may have added a variable, so make sure to resize the vars array.
					pd->snapshot.node_vars.write[p_object_data->get_net_id().id].resize(p_object_data->vars.size());
				}

				pd->snapshot.node_vars.write[p_object_data->get_net_id().id].write[p_var_id.id].name = p_object_data->vars[p_var_id.id].var.name;
				pd->snapshot.node_vars.write[p_object_data->get_net_id().id].write[p_var_id.id].value = p_value.duplicate(true);
			},

			// Parse node activation:
			[](void *p_user_pointer, NS::ObjectData *p_object_data, bool p_is_active) {
				ParseData *pd = static_cast<ParseData *>(p_user_pointer);
				if (p_object_data->realtime_sync_enabled_on_client != p_is_active) {
					p_object_data->realtime_sync_enabled_on_client = p_is_active;

					// Make sure the process_function cache is cleared.
					pd->scene_synchronizer->process_functions__clear();
				}

				// Make sure this node is not into the deferred sync list.
				if (p_is_active) {
					pd->client_synchronizer->remove_node_from_deferred_sync(p_object_data);
				}
			});

	if (success == false) {
		SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "Snapshot parsing failed.");
		SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "Snapshot:", !scene_synchronizer->debug_rewindings_enabled);
		SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), NS::stringify_fast(p_snapshot), !scene_synchronizer->debug_rewindings_enabled);
		return false;
	}

	if (unlikely(received_snapshot.input_id == UINT32_MAX && player_controller_node_data != nullptr)) {
		// We espect that the player_controller is updated by this new snapshot,
		// so make sure it's done so.
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "[INFO] the player controller (" + String(player_controller_node_data->object_name.c_str()) + ") was not part of the received snapshot, this happens when the server destroys the peer controller.");
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "NS::Snapshot:", !scene_synchronizer->debug_rewindings_enabled);
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), NS::stringify_fast(p_snapshot), !scene_synchronizer->debug_rewindings_enabled);
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
	scene_synchronizer->rpc_handler_notify_need_full_snapshot.rpc(
			scene_synchronizer->get_network_interface(),
			scene_synchronizer->network_interface->get_server_peer());
}

void ClientSynchronizer::update_client_snapshot(NS::Snapshot &p_snapshot) {
	p_snapshot.custom_data.clear();
	scene_synchronizer->synchronizer_manager->snapshot_add_custom_data(nullptr, p_snapshot.custom_data);

	// Make sure we have room for all the NodeData.
	p_snapshot.node_vars.resize(scene_synchronizer->objects_data_storage.get_sorted_objects_data().size());

	// Fetch the data.
	for (ObjectNetId net_node_id = { 0 }; net_node_id < ObjectNetId{ uint32_t(scene_synchronizer->objects_data_storage.get_sorted_objects_data().size()) }; net_node_id += 1) {
		NS::ObjectData *nd = scene_synchronizer->objects_data_storage.get_object_data(net_node_id);
		if (nd == nullptr || nd->realtime_sync_enabled_on_client == false) {
			continue;
		}

		// Make sure this ID is valid.
		ERR_FAIL_COND_MSG(nd->get_net_id() == ObjectNetId::NONE, "[BUG] It's not expected that the client has an uninitialized NetNodeId into the `organized_node_data` ");

#ifdef DEBUG_ENABLED
		CRASH_COND_MSG(nd->get_net_id().id >= uint32_t(p_snapshot.node_vars.size()), "This array was resized above, this can't be triggered.");
#endif

		Vector<NS::NameAndVar> *snap_node_vars = p_snapshot.node_vars.ptrw() + nd->get_net_id().id;
		snap_node_vars->resize(nd->vars.size());

		NS::NameAndVar *snap_node_vars_ptr = snap_node_vars->ptrw();
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
		const NS::Snapshot &p_snapshot,
		int p_flag,
		LocalVector<String> *r_applied_data_info,
		bool p_skip_custom_data) {
	const Vector<NS::NameAndVar> *nodes_vars = p_snapshot.node_vars.ptr();

	scene_synchronizer->change_events_begin(p_flag);

	for (ObjectNetId net_node_id = { 0 }; net_node_id < ObjectNetId{ uint32_t(p_snapshot.node_vars.size()) }; net_node_id += 1) {
		NS::ObjectData *nd = scene_synchronizer->get_object_data(net_node_id);

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

		const Vector<NS::NameAndVar> &vars = nodes_vars[net_node_id.id];
		const NS::NameAndVar *vars_ptr = vars.ptr();

		if (r_applied_data_info) {
			r_applied_data_info->push_back("Applied snapshot data on the node: " + String(nd->object_name.c_str()));
		}

		// NOTE: The vars may not contain ALL the variables: it depends on how
		//       the snapshot was captured.
		for (VarId v = { 0 }; v < VarId{ uint32_t(vars.size()) }; v += 1) {
			if (vars_ptr[v.id].name == StringName()) {
				// This variable was not set, skip it.
				continue;
			}

			const Variant current_val = nd->vars[v.id].var.value;
			nd->vars[v.id].var.value = vars_ptr[v.id].value.duplicate(true);

			if (!scene_synchronizer->compare(current_val, vars_ptr[v.id].value)) {
				scene_synchronizer->synchronizer_manager->set_variable(
						nd->app_object_handle,
						String(vars_ptr[v.id].name).utf8(),
						vars_ptr[v.id].value);
				scene_synchronizer->change_event_add(
						nd,
						v,
						current_val);

				if (r_applied_data_info) {
					r_applied_data_info->push_back(" |- Variable: " + vars_ptr[v.id].name + " New value: " + NS::stringify_fast(vars_ptr[v.id].value));
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
