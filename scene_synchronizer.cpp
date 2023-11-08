
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
#include "modules/network_synchronizer/core/var_data.h"
#include "modules/network_synchronizer/data_buffer.h"
#include "modules/network_synchronizer/net_utilities.h"
#include "modules/network_synchronizer/networked_controller.h"
#include "modules/network_synchronizer/scene_synchronizer.h"
#include "modules/network_synchronizer/snapshot.h"
#include "scene_synchronizer_debugger.h"
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

NS_NAMESPACE_BEGIN

const SyncGroupId SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID = 0;
void (*SceneSynchronizerBase::var_data_encode_func)(DataBuffer &r_buffer, const NS::VarData &p_val) = nullptr;
void (*SceneSynchronizerBase::var_data_decode_func)(NS::VarData &r_val, DataBuffer &p_buffer) = nullptr;
bool (*SceneSynchronizerBase::var_data_compare_func)(const VarData &p_A, const VarData &p_B) = nullptr;
std::string (*SceneSynchronizerBase::var_data_stringify_func)(const VarData &p_var_data) = nullptr;

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

void SceneSynchronizerBase::register_var_data_functions(
		void (*p_var_data_encode_func)(DataBuffer &r_buffer, const NS::VarData &p_val),
		void (*p_var_data_decode_func)(NS::VarData &r_val, DataBuffer &p_buffer),
		bool (*p_var_data_compare_func)(const VarData &p_A, const VarData &p_B),
		std::string (*p_var_data_stringify_func)(const VarData &p_var_data)) {
	var_data_encode_func = p_var_data_encode_func;
	var_data_decode_func = p_var_data_decode_func;
	var_data_compare_func = p_var_data_compare_func;
	var_data_stringify_func = p_var_data_stringify_func;
}

void SceneSynchronizerBase::setup(SynchronizerManager &p_synchronizer_interface) {
	synchronizer_manager = &p_synchronizer_interface;
	network_interface->start_listening_peer_connection(
			[this](int p_peer) { on_peer_connected(p_peer); },
			[this](int p_peer) { on_peer_disconnected(p_peer); });

	rpc_handler_state =
			network_interface->rpc_config(
					std::function<void(DataBuffer &)>(std::bind(&SceneSynchronizerBase::rpc_receive_state, this, std::placeholders::_1)),
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

	rpc_handler_trickled_sync_data =
			network_interface->rpc_config(
					std::function<void(const Vector<uint8_t> &)>(std::bind(&SceneSynchronizerBase::rpc_trickled_sync_data, this, std::placeholders::_1)),
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
	rpc_handler_trickled_sync_data.reset();
}

void SceneSynchronizerBase::process() {
	PROFILE_NODE

#ifdef DEBUG_ENABLED
	CRASH_COND_MSG(synchronizer == nullptr, "Never execute this function unless this synchronizer is ready.");

	synchronizer_manager->debug_only_validate_objects();
#endif

	synchronizer->process();
}

void SceneSynchronizerBase::on_app_object_removed(ObjectHandle p_app_object_handle) {
	unregister_app_object(find_object_local_id(p_app_object_handle));
}

void SceneSynchronizerBase::var_data_encode(DataBuffer &r_buffer, const NS::VarData &p_val) {
	var_data_encode_func(r_buffer, p_val);
}

void SceneSynchronizerBase::var_data_decode(NS::VarData &r_val, DataBuffer &p_buffer) {
	var_data_decode_func(r_val, p_buffer);
}

bool SceneSynchronizerBase::var_data_compare(const VarData &p_A, const VarData &p_B) {
	return var_data_compare_func(p_A, p_B);
}

std::string SceneSynchronizerBase::var_data_stringify(const VarData &p_var_data) {
	return var_data_stringify_func(p_var_data);
}

void SceneSynchronizerBase::set_max_trickled_objects_per_update(int p_rate) {
	max_trickled_objects_per_update = p_rate;
}

int SceneSynchronizerBase::get_max_trickled_objects_per_update() const {
	return max_trickled_objects_per_update;
}

void SceneSynchronizerBase::set_server_notify_state_interval(real_t p_interval) {
	server_notify_state_interval = p_interval;
}

real_t SceneSynchronizerBase::get_server_notify_state_interval() const {
	return server_notify_state_interval;
}

void SceneSynchronizerBase::set_objects_relevancy_update_time(real_t p_time) {
	objects_relevancy_update_time = p_time;
}

real_t SceneSynchronizerBase::get_objects_relevancy_update_time() const {
	return objects_relevancy_update_time;
}

bool SceneSynchronizerBase::is_variable_registered(ObjectLocalId p_id, const StringName &p_variable) const {
	const ObjectData *od = objects_data_storage.get_object_data(p_id);
	if (od != nullptr) {
		return od->find_variable_id(std::string(String(p_variable).utf8())) != VarId::NONE;
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

void SceneSynchronizerBase::register_variable(ObjectLocalId p_id, const std::string &p_variable) {
	ERR_FAIL_COND(p_id == ObjectLocalId::NONE);
	ERR_FAIL_COND(p_variable.empty());

	NS::ObjectData *object_data = get_object_data(p_id);
	ERR_FAIL_COND(object_data == nullptr);

	VarId var_id = object_data->find_variable_id(p_variable);
	if (var_id == VarId::NONE) {
		// The variable is not yet registered.
		bool valid = false;
		VarData old_val;
		valid = synchronizer_manager->get_variable(object_data->app_object_handle, p_variable.data(), old_val);
		if (valid == false) {
			SceneSynchronizerDebugger::singleton()->debug_error(network_interface, "The variable `" + String(p_variable.c_str()) + "` on the node `" + String(object_data->object_name.c_str()) + "` was not found, make sure the variable exist.");
		}
		var_id = VarId{ uint32_t(object_data->vars.size()) };
		object_data->vars.push_back(
				NS::VarDescriptor(
						var_id,
						p_variable,
						std::move(old_val),
						false,
						true));
	} else {
		// Make sure the var is active.
		object_data->vars[var_id.id].enabled = true;
	}

#ifdef DEBUG_ENABLED
	for (VarId v = { 0 }; v < VarId{ uint32_t(object_data->vars.size()) }; v += 1) {
		// This can't happen, because the IDs are always consecutive, or NONE.
		CRASH_COND(object_data->vars[v.id].id != v);
	}
#endif

	if (synchronizer) {
		synchronizer->on_variable_added(object_data, p_variable);
	}
}

void SceneSynchronizerBase::unregister_variable(ObjectLocalId p_id, const std::string &p_variable) {
	ERR_FAIL_COND(p_id == ObjectLocalId::NONE);
	ERR_FAIL_COND(p_variable.empty());

	NS::ObjectData *od = objects_data_storage.get_object_data(p_id);
	ERR_FAIL_COND(od == nullptr);

	const VarId var_id = od->find_variable_id(p_variable);
	ERR_FAIL_COND(var_id == VarId::NONE);

	// Never remove the variable values, because the order of the vars matters.
	od->vars[var_id.id].enabled = false;

	// Remove this var from all the changes listeners.
	for (ChangesListener *cl : od->vars[var_id.id].changes_listeners) {
		for (ListeningVariable lv : cl->watching_vars) {
			if (lv.node_data == od && lv.var_id == var_id) {
				// We can't change the var order, so just invalidate this.
				lv.node_data = nullptr;
				lv.var_id = VarId::NONE;
			}
		}
	}

	// So, clear the changes listener list for this var.
	od->vars[var_id.id].changes_listeners.clear();
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

	NS::ObjectData *od = get_object_data(p_id);
	ERR_FAIL_COND_V_MSG(od == nullptr, VarId::NONE, "This node " + String(od->object_name.c_str()) + "is not registered.");

	return od->find_variable_id(std::string(String(p_variable).utf8()));
}

void SceneSynchronizerBase::set_skip_rewinding(ObjectLocalId p_id, const StringName &p_variable, bool p_skip_rewinding) {
	NS::ObjectData *od = get_object_data(p_id);
	ERR_FAIL_COND(od == nullptr);

	const VarId id = od->find_variable_id(std::string(String(p_variable).utf8()));
	ERR_FAIL_COND(id == VarId::NONE);

	od->vars[id.id].skip_rewinding = p_skip_rewinding;
}

ListenerHandle SceneSynchronizerBase::track_variable_changes(
		ObjectLocalId p_id,
		const StringName &p_variable,
		std::function<void(const std::vector<VarData> &p_old_values)> p_listener_func,
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
		std::function<void(const std::vector<VarData> &p_old_values)> p_listener_func,
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

		NS::ObjectData *od = objects_data_storage.get_object_data(id);
		if (!od) {
			ERR_PRINT("The passed ObjectHandle `" + itos(id.id) + "` is not pointing to any valid NodeData. Make sure to register the variable first.");
			is_valid = false;
			break;
		}

		const VarId vid = od->find_variable_id(std::string(String(variable_name).utf8()));
		if (vid == VarId::NONE) {
			ERR_PRINT("The passed variable `" + variable_name + "` doesn't exist under this object `" + String(od->object_name.c_str()) + "`.");
			is_valid = false;
			break;
		}

		listener->watching_vars[i].node_data = od;
		listener->watching_vars[i].var_id = vid;
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
	auto it = VecFunc::find(changes_listeners, unsafe_handle);
	if (it == changes_listeners.end()) {
		// Nothing to do.
		return;
	}

	ChangesListener *listener = *it;

	// Before dropping this listener, make sure to clear the NodeData.
	for (auto &wv : listener->watching_vars) {
		if (wv.node_data) {
			if (wv.node_data->vars.size() > wv.var_id.id) {
				auto wv_cl_it = VecFunc::find(wv.node_data->vars[wv.var_id.id].changes_listeners, unsafe_handle);
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

void SceneSynchronizerBase::set_trickled_sync(
		ObjectLocalId p_id,
		std::function<void(DataBuffer & /*out_buffer*/, float /*update_rate*/)> p_func_trickled_collect,
		std::function<void(float /*delta*/, float /*interpolation_alpha*/, DataBuffer & /*past_buffer*/, DataBuffer & /*future_buffer*/)> p_func_trickled_apply) {
	ERR_FAIL_COND(p_id == ObjectLocalId::NONE);

	NS::ObjectData *od = get_object_data(p_id);
	ERR_FAIL_COND(!od);

	od->func_trickled_collect = p_func_trickled_collect;
	od->func_trickled_apply = p_func_trickled_apply;
	SceneSynchronizerDebugger::singleton()->debug_print(network_interface, "Setup trickled sync functions for: `" + String(od->object_name.c_str()) + "`.");
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

void SceneSynchronizerBase::sync_group_add_object_by_id(ObjectNetId p_node_id, SyncGroupId p_group_id, bool p_realtime) {
	NS::ObjectData *nd = get_object_data(p_node_id);
	sync_group_add_object(nd, p_group_id, p_realtime);
}

void SceneSynchronizerBase::sync_group_add_object(NS::ObjectData *p_object_data, SyncGroupId p_group_id, bool p_realtime) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_add_object(p_object_data, p_group_id, p_realtime);
}

void SceneSynchronizerBase::sync_group_remove_object_by_id(ObjectNetId p_node_id, SyncGroupId p_group_id) {
	NS::ObjectData *nd = get_object_data(p_node_id);
	sync_group_remove_object(nd, p_group_id);
}

void SceneSynchronizerBase::sync_group_remove_object(NS::ObjectData *p_object_data, SyncGroupId p_group_id) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_remove_object(p_object_data, p_group_id);
}

void SceneSynchronizerBase::sync_group_replace_objects(SyncGroupId p_group_id, LocalVector<NS::SyncGroup::SimulatedObjectInfo> &&p_new_realtime_nodes, LocalVector<NS::SyncGroup::TrickledObjectInfo> &&p_new_trickled_nodes) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_replace_object(p_group_id, std::move(p_new_realtime_nodes), std::move(p_new_trickled_nodes));
}

void SceneSynchronizerBase::sync_group_remove_all_objects(SyncGroupId p_group_id) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_remove_all_objects(p_group_id);
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

const std::vector<int> *SceneSynchronizerBase::sync_group_get_listening_peers(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(!is_server(), nullptr, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get_listening_peers(p_group_id);
}

void SceneSynchronizerBase::sync_group_set_trickled_update_rate(ObjectLocalId p_node_id, SyncGroupId p_group_id, real_t p_update_rate) {
	NS::ObjectData *od = get_object_data(p_node_id);
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_set_trickled_update_rate(od, p_group_id, p_update_rate);
}

void SceneSynchronizerBase::sync_group_set_trickled_update_rate(ObjectNetId p_node_id, SyncGroupId p_group_id, real_t p_update_rate) {
	NS::ObjectData *od = get_object_data(p_node_id);
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_set_trickled_update_rate(od, p_group_id, p_update_rate);
}

real_t SceneSynchronizerBase::sync_group_get_trickled_update_rate(ObjectLocalId p_id, SyncGroupId p_group_id) const {
	const NS::ObjectData *od = get_object_data(p_id);
	ERR_FAIL_COND_V_MSG(!is_server(), 0.0, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get_trickled_update_rate(od, p_group_id);
}

real_t SceneSynchronizerBase::sync_group_get_trickled_update_rate(ObjectNetId p_id, SyncGroupId p_group_id) const {
	const NS::ObjectData *od = get_object_data(p_id);
	ERR_FAIL_COND_V_MSG(!is_server(), 0.0, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get_trickled_update_rate(od, p_group_id);
}

void SceneSynchronizerBase::sync_group_set_user_data(SyncGroupId p_group_id, uint64_t p_user_data) {
	ERR_FAIL_COND_MSG(!is_server(), "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_set_user_data(p_group_id, p_user_data);
}

uint64_t SceneSynchronizerBase::sync_group_get_user_data(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(!is_server(), 0, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get_user_data(p_group_id);
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

void SceneSynchronizerBase::rpc_receive_state(DataBuffer &p_snapshot) {
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

void SceneSynchronizerBase::rpc_trickled_sync_data(const Vector<uint8_t> &p_data) {
	ERR_FAIL_COND_MSG(is_client() == false, "Only clients are supposed to receive this function call.");
	ERR_FAIL_COND_MSG(p_data.size() <= 0, "It's not supposed to receive a 0 size data.");

	static_cast<ClientSynchronizer *>(synchronizer)->receive_trickled_sync_data(p_data);
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
			pull_object_changes(od);
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

void SceneSynchronizerBase::change_event_add(NS::ObjectData *p_object_data, VarId p_var_id, const VarData &p_old) {
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
				listener->old_values[v].copy(p_old);
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
				listener.old_values[v].copy(
						listener.watching_vars[v].node_data->vars[listener.watching_vars[v].var_id.id].var.value);
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

const std::vector<ObjectNetId> *SceneSynchronizerBase::client_get_simulated_objects() const {
	ERR_FAIL_COND_V_MSG(!is_client(), nullptr, "This function CAN be used only on the client.");
	return &(static_cast<ClientSynchronizer *>(synchronizer)->simulated_objects);
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

void SceneSynchronizerBase::update_objects_relevancy() {
	synchronizer_manager->update_objects_relevancy();

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

ObjectNetId SceneSynchronizerBase::get_biggest_object_id() const {
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

void SceneSynchronizerBase::pull_object_changes(NS::ObjectData *p_object_data) {
	for (VarId var_id = { 0 }; var_id < VarId{ uint32_t(p_object_data->vars.size()) }; var_id += 1) {
		if (p_object_data->vars[var_id.id].enabled == false) {
			continue;
		}

		const VarData &old_val = p_object_data->vars[var_id.id].var.value;
		VarData new_val;
		synchronizer_manager->get_variable(
				p_object_data->app_object_handle,
				p_object_data->vars[var_id.id].var.name.c_str(),
				new_val);

		if (!SceneSynchronizerBase::var_data_compare(old_val, new_val)) {
			change_event_add(
					p_object_data,
					var_id,
					old_val);
			p_object_data->vars[var_id.id].var.value = std::move(new_val);
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
	objects_relevancy_update_timer = 0.0;
	// Release the internal memory.
	sync_groups.clear();
}

void ServerSynchronizer::process() {
	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "ServerSynchronizer::process", true);

	scene_synchronizer->update_peers();

	const double physics_ticks_per_second = Engine::get_singleton()->get_physics_ticks_per_second();
	const double delta = 1.0 / physics_ticks_per_second;

	if (objects_relevancy_update_timer >= scene_synchronizer->objects_relevancy_update_time) {
		scene_synchronizer->update_objects_relevancy();
		objects_relevancy_update_timer = 0.0;
	}
	objects_relevancy_update_timer += delta;

	SceneSynchronizerDebugger::singleton()->scene_sync_process_start(scene_synchronizer);

	epoch += 1;

	// Process the scene
	scene_synchronizer->process_functions__execute(delta);

	scene_synchronizer->detect_and_signal_changed_variables(NetEventFlag::CHANGE);

	process_snapshot_notificator(delta);
	process_trickled_sync(delta);

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
		sync_groups[i].remove_listening_peer(p_peer_id);
	}
}

void ServerSynchronizer::on_object_data_added(NS::ObjectData *p_object_data) {
#ifdef DEBUG_ENABLED
	// Can't happen on server
	CRASH_COND(scene_synchronizer->is_recovered());
	// On server the ID is always known.
	CRASH_COND(p_object_data->get_net_id() == ObjectNetId::NONE);
#endif

	sync_groups[SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID].add_new_sync_object(p_object_data, true);

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
		sync_groups[i].remove_sync_object(p_object_data);
	}
}

void ServerSynchronizer::on_variable_added(NS::ObjectData *p_object_data, const std::string &p_var_name) {
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

void ServerSynchronizer::on_variable_changed(NS::ObjectData *p_object_data, VarId p_var_id, const VarData &p_old_value, int p_flag) {
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

void ServerSynchronizer::sync_group_add_object(NS::ObjectData *p_object_data, SyncGroupId p_group_id, bool p_realtime) {
	ERR_FAIL_COND(p_object_data == nullptr);
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id].add_new_sync_object(p_object_data, p_realtime);
}

void ServerSynchronizer::sync_group_remove_object(NS::ObjectData *p_object_data, SyncGroupId p_group_id) {
	ERR_FAIL_COND(p_object_data == nullptr);
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id].remove_sync_object(*p_object_data);
}

void ServerSynchronizer::sync_group_replace_object(SyncGroupId p_group_id, LocalVector<NS::SyncGroup::SimulatedObjectInfo> &&p_new_realtime_nodes, LocalVector<NS::SyncGroup::TrickledObjectInfo> &&p_new_trickled_nodes) {
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id].replace_objects(std::move(p_new_realtime_nodes), std::move(p_new_trickled_nodes));
}

void ServerSynchronizer::sync_group_remove_all_objects(SyncGroupId p_group_id) {
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id].remove_all_nodes();
}

void ServerSynchronizer::sync_group_move_peer_to(int p_peer_id, SyncGroupId p_group_id) {
	// remove the peer from any sync_group.
	for (uint32_t i = 0; i < sync_groups.size(); ++i) {
		sync_groups[i].remove_listening_peer(p_peer_id);
	}

	if (p_group_id == UINT32_MAX) {
		// This peer is not listening to anything.
		return;
	}

	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	sync_groups[p_group_id].add_listening_peer(p_peer_id);

	// Also mark the peer as need full snapshot, as it's into a new group now.
	NS::PeerData *pd = MapFunc::at(scene_synchronizer->peer_data, p_peer_id);
	ERR_FAIL_COND(pd == nullptr);
	pd->force_notify_snapshot = true;
	pd->need_full_snapshot = true;

	// Make sure the controller is added into this group.
	NS::ObjectData *nd = scene_synchronizer->get_object_data(pd->controller_id, false);
	if (nd) {
		sync_group_add_object(nd, p_group_id, true);
	}
}

const std::vector<int> *ServerSynchronizer::sync_group_get_listening_peers(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(p_group_id >= sync_groups.size(), nullptr, "The group id `" + itos(p_group_id) + "` doesn't exist.");
	return &sync_groups[p_group_id].get_listening_peers();
}

void ServerSynchronizer::sync_group_set_trickled_update_rate(NS::ObjectData *p_object_data, SyncGroupId p_group_id, real_t p_update_rate) {
	ERR_FAIL_COND(p_object_data == nullptr);
	ERR_FAIL_COND_MSG(p_group_id >= sync_groups.size(), "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id].set_trickled_update_rate(p_object_data, p_update_rate);
}

real_t ServerSynchronizer::sync_group_get_trickled_update_rate(const NS::ObjectData *p_object_data, SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V(p_object_data == nullptr, 0.0);
	ERR_FAIL_COND_V_MSG(p_group_id >= sync_groups.size(), 0.0, "The group id `" + itos(p_group_id) + "` doesn't exist.");
	ERR_FAIL_COND_V_MSG(p_group_id == SceneSynchronizerBase::GLOBAL_SYNC_GROUP_ID, 0.0, "You can't change this SyncGroup in any way. Create a new one.");
	return sync_groups[p_group_id].get_trickled_update_rate(p_object_data);
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
		for (int peer : group.get_listening_peers()) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|      |- " + itos(peer));
		}

		const LocalVector<NS::SyncGroup::SimulatedObjectInfo> &realtime_node_info = group.get_simulated_sync_objects();
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|");
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|    [Realtime nodes]");
		for (auto info : realtime_node_info) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|      |- " + String(info.od->object_name.c_str()));
		}

		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|");

		const LocalVector<NS::SyncGroup::TrickledObjectInfo> &trickled_node_info = group.get_trickled_sync_objects();
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|    [Trickled nodes (UR: Update Rate)]");
		for (auto info : trickled_node_info) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|      |- [UR: " + rtos(info.update_rate) + "] " + info.od->object_name.c_str());
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

		if (group.get_listening_peers().empty()) {
			// No one is interested to this group.
			continue;
		}

		// Notify the state if needed
		group.state_notifier_timer += p_delta;
		const bool notify_state = group.state_notifier_timer >= scene_synchronizer->get_server_notify_state_interval();

		if (notify_state) {
			group.state_notifier_timer = 0.0;
		}

		const int MD_SIZE = DataBuffer::get_bit_taken(DataBuffer::DATA_TYPE_UINT, DataBuffer::COMPRESSION_LEVEL_1);

		bool full_snapshot_need_init = true;
		DataBuffer full_snapshot;
		full_snapshot.begin_write(MD_SIZE);

		bool delta_snapshot_need_init = true;
		DataBuffer delta_snapshot;
		delta_snapshot.begin_write(MD_SIZE);

		for (int peer_id : group.get_listening_peers()) {
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

			NS::ObjectData *controller_od = scene_synchronizer->get_object_data(peer->controller_id, false);

			// Fetch the peer input_id for this snapshot
			std::uint32_t input_id = std::numeric_limits<std::uint32_t>::max();
			if (controller_od) {
				CRASH_COND_MSG(controller_od->get_controller() == nullptr, "The NodeData fetched is not a controller: `" + String(controller_od->object_name.c_str()) + "`, this is not supposed to happen.");
				NetworkedControllerBase *controller = controller_od->get_controller();
				input_id = controller->get_current_input_id();
			}

			DataBuffer *snap;
			if (peer->need_full_snapshot) {
				peer->need_full_snapshot = false;
				if (full_snapshot_need_init) {
					full_snapshot_need_init = false;
					full_snapshot.seek(MD_SIZE);
					generate_snapshot(true, group, full_snapshot);
				}

				snap = &full_snapshot;

			} else {
				if (delta_snapshot_need_init) {
					delta_snapshot_need_init = false;
					delta_snapshot.seek(MD_SIZE);
					generate_snapshot(false, group, delta_snapshot);
				}

				snap = &delta_snapshot;
			}

			snap->seek(0);
			snap->add(input_id);

			scene_synchronizer->rpc_handler_state.rpc(
					scene_synchronizer->get_network_interface(),
					peer_id,
					*snap);

			if (controller_od) {
				NetworkedControllerBase *controller = controller_od->get_controller();
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

void ServerSynchronizer::generate_snapshot(
		bool p_force_full_snapshot,
		const NS::SyncGroup &p_group,
		DataBuffer &r_snapshot_db) const {
	const LocalVector<NS::SyncGroup::SimulatedObjectInfo> &relevant_node_data = p_group.get_simulated_sync_objects();

	// First insert the list of ALL simulated ObjectData, if changed.
	if (p_group.is_realtime_node_list_changed() || p_force_full_snapshot) {
		r_snapshot_db.add(true);

		for (uint32_t i = 0; i < relevant_node_data.size(); i += 1) {
			const NS::ObjectData *od = relevant_node_data[i].od;
			CRASH_COND(od->get_net_id() == ObjectNetId::NONE);
			CRASH_COND(od->get_net_id().id > std::numeric_limits<uint16_t>::max());
			r_snapshot_db.add(od->get_net_id().id);
		}

		// Add `uint16_max to signal its end.
		r_snapshot_db.add(ObjectNetId::NONE.id);
	} else {
		r_snapshot_db.add(false);
	}

	// Calling this function to allow customize the snapshot per group.
	NS::VarData vd;
	if (scene_synchronizer->synchronizer_manager->snapshot_get_custom_data(&p_group, vd)) {
		r_snapshot_db.add(true);
		SceneSynchronizerBase::var_data_encode(r_snapshot_db, vd);
	} else {
		r_snapshot_db.add(false);
	}

	if (p_group.is_trickled_node_list_changed() || p_force_full_snapshot) {
		for (int i = 0; i < int(p_group.get_trickled_sync_objects().size()); ++i) {
			if (p_group.get_trickled_sync_objects()[i]._unknown || p_force_full_snapshot) {
				generate_snapshot_object_data(
						p_group.get_trickled_sync_objects()[i].od,
						SNAPSHOT_GENERATION_MODE_FORCE_NODE_PATH_ONLY,
						NS::SyncGroup::Change(),
						r_snapshot_db);
			}
		}
	}

	const SnapshotGenerationMode mode = p_force_full_snapshot ? SNAPSHOT_GENERATION_MODE_FORCE_FULL : SNAPSHOT_GENERATION_MODE_NORMAL;

	// Then, generate the snapshot for the relevant nodes.
	for (uint32_t i = 0; i < relevant_node_data.size(); i += 1) {
		const NS::ObjectData *node_data = relevant_node_data[i].od;

		if (node_data != nullptr) {
			generate_snapshot_object_data(
					node_data,
					mode,
					relevant_node_data[i].change,
					r_snapshot_db);
		}
	}

	// Mark the end.
	r_snapshot_db.add(ObjectNetId::NONE.id);
}

void ServerSynchronizer::generate_snapshot_object_data(
		const NS::ObjectData *p_object_data,
		SnapshotGenerationMode p_mode,
		const NS::SyncGroup::Change &p_change,
		DataBuffer &r_snapshot_db) const {
	if (p_object_data->app_object_handle == ObjectHandle::NONE) {
		return;
	}

	const bool force_using_node_path = p_mode == SNAPSHOT_GENERATION_MODE_FORCE_FULL || p_mode == SNAPSHOT_GENERATION_MODE_FORCE_NODE_PATH_ONLY;
	const bool force_snapshot_variables = p_mode == SNAPSHOT_GENERATION_MODE_FORCE_FULL;
	const bool skip_snapshot_variables = p_mode == SNAPSHOT_GENERATION_MODE_FORCE_NODE_PATH_ONLY;

	const bool unknown = p_change.unknown;
	const bool node_has_changes = p_change.vars.is_empty() == false;

	// Insert OBJECT DATA NetId.
	r_snapshot_db.add(p_object_data->get_net_id().id);

	if (force_using_node_path || unknown) {
		// This object is unknown.
		r_snapshot_db.add(true); // Has the object name?
		r_snapshot_db.add(p_object_data->object_name);
	} else {
		// This node is already known on clients, just set the node ID.
		r_snapshot_db.add(false); // Has the object name?
	}

	const bool allow_vars =
			force_snapshot_variables ||
			(node_has_changes && !skip_snapshot_variables) ||
			unknown;

	// This is necessary to allow the client decode the snapshot even if it
	// doesn't know this object.
	std::uint8_t vars_count = p_object_data->vars.size();
	r_snapshot_db.add(vars_count);

	// This is assuming the client and the server have the same vars registered
	// with the same order.
	for (uint32_t i = 0; i < p_object_data->vars.size(); i += 1) {
		const NS::VarDescriptor &var = p_object_data->vars[i];

		bool var_has_value = allow_vars;

		if (var.enabled == false) {
			var_has_value = false;
		}

		if (!force_snapshot_variables && !p_change.vars.has(var.var.name)) {
			// This is a delta snapshot and this variable is the same as before.
			// Skip this value
			var_has_value = false;
		}

		r_snapshot_db.add(var_has_value);
		if (var_has_value) {
			SceneSynchronizerBase::var_data_encode(r_snapshot_db, var.var.value);
		}
	}
}

void ServerSynchronizer::process_trickled_sync(real_t p_delta) {
	DataBuffer *tmp_buffer = memnew(DataBuffer);

	Variant r;

	for (auto &group : sync_groups) {
		if (group.get_listening_peers().empty()) {
			// No one is interested to this group.
			continue;
		}

		LocalVector<NS::SyncGroup::TrickledObjectInfo> &objects_info = group.get_trickled_sync_objects();
		if (objects_info.size() == 0) {
			// Nothing to sync.
			continue;
		}

		int update_node_count = 0;

		group.sort_trickled_node_by_update_priority();

		DataBuffer global_buffer;
		global_buffer.begin_write(0);
		global_buffer.add_uint(epoch, DataBuffer::COMPRESSION_LEVEL_1);

		for (auto &object_info : objects_info) {
			bool send = true;
			if (object_info._update_priority < 1.0 || update_node_count >= scene_synchronizer->max_trickled_objects_per_update) {
				send = false;
			}

			if (send) {
				// TODO use `DEBUG_ENABLED` here?
				if (object_info.od->get_net_id().id > UINT16_MAX) {
					SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "[FATAL] The `process_trickled_sync` found a node with ID `" + itos(object_info.od->get_net_id().id) + "::" + object_info.od->object_name.c_str() + "` that exceedes the max ID this function can network at the moment. Please report this, we will consider improving this function.");
					continue;
				}

				// TODO use `DEBUG_ENABLED` here?
				if (!object_info.od->func_trickled_collect) {
					SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The `process_trickled_sync` found a node `" + itos(object_info.od->get_net_id().id) + "::" + object_info.od->object_name.c_str() + "` with an invalid function `func_trickled_collect`. Please use `setup_deferred_sync` to correctly initialize this node for deferred sync.");
					continue;
				}

				object_info._update_priority = 0.0;

				// Read the state and write into the tmp_buffer:
				tmp_buffer->begin_write(0);

				object_info.od->func_trickled_collect(*tmp_buffer, object_info.update_rate);
				if (tmp_buffer->total_size() > UINT16_MAX) {
					SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The `process_trickled_sync` failed because the method `trickled_collect` for the node `" + itos(object_info.od->get_net_id().id) + "::" + object_info.od->object_name.c_str() + "` collected more than " + itos(UINT16_MAX) + " bits. Please optimize your netcode to send less data.");
					continue;
				}

				++update_node_count;

				if (object_info.od->get_net_id().id > UINT8_MAX) {
					global_buffer.add_bool(true);
					global_buffer.add_uint(object_info.od->get_net_id().id, DataBuffer::COMPRESSION_LEVEL_2);
				} else {
					global_buffer.add_bool(false);
					global_buffer.add_uint(object_info.od->get_net_id().id, DataBuffer::COMPRESSION_LEVEL_3);
				}

				// Collapse the two DataBuffer.
				global_buffer.add_uint(uint32_t(tmp_buffer->total_size()), DataBuffer::COMPRESSION_LEVEL_2);
				global_buffer.add_bits(tmp_buffer->get_buffer().get_bytes().ptr(), tmp_buffer->total_size());

			} else {
				object_info._update_priority += object_info.update_rate;
			}
		}

		if (update_node_count > 0) {
			global_buffer.dry();
			for (int peer : group.get_listening_peers()) {
				scene_synchronizer->rpc_handler_trickled_sync_data.rpc(
						scene_synchronizer->get_network_interface(),
						peer,
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
	player_controller_object_data = nullptr;
	objects_names.clear();
	last_received_snapshot.input_id = UINT32_MAX;
	last_received_snapshot.object_vars.clear();
	client_snapshots.clear();
	server_snapshots.clear();
	last_checked_input = 0;
	enabled = true;
	need_full_snapshot_notified = false;
}

void ClientSynchronizer::process() {
	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "ClientSynchronizer::process", true);

	const float physics_ticks_per_second = Engine::get_singleton()->get_physics_ticks_per_second();
	const float delta = 1.0 / physics_ticks_per_second;

#ifdef DEBUG_ENABLED
	if (unlikely(Engine::get_singleton()->get_frames_per_second() < physics_ticks_per_second)) {
		const bool silent = !ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/debugger/log_debug_fps_warnings");
		SceneSynchronizerDebugger::singleton()->debug_warning(&scene_synchronizer->get_network_interface(), "Current FPS is " + itos(Engine::get_singleton()->get_frames_per_second()) + ", but the minimum required FPS is " + itos(physics_ticks_per_second) + ", the client is unable to generate enough inputs for the server.", silent);
	}
#endif

	process_server_sync(delta);
	process_simulation(delta, physics_ticks_per_second);
	process_trickled_sync(delta);

#if DEBUG_ENABLED
	if (player_controller_object_data) {
		NetworkedControllerBase *controller = player_controller_object_data->get_controller();
		PlayerController *player_controller = controller->get_player_controller();
		const int client_peer = scene_synchronizer->network_interface->fetch_local_peer_id();
		SceneSynchronizerDebugger::singleton()->write_dump(client_peer, player_controller->get_current_input_id());
		SceneSynchronizerDebugger::singleton()->start_new_frame();
	}
#endif
}

void ClientSynchronizer::receive_snapshot(DataBuffer &p_snapshot) {
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
	if (player_controller_object_data == &p_object_data) {
		player_controller_object_data = nullptr;
		server_snapshots.clear();
		client_snapshots.clear();
	}

	if (p_object_data.get_net_id().id < uint32_t(last_received_snapshot.object_vars.size())) {
		last_received_snapshot.object_vars[p_object_data.get_net_id().id].clear();
	}

	remove_object_from_trickled_sync(&p_object_data);
}

void ClientSynchronizer::on_variable_changed(NS::ObjectData *p_object_data, VarId p_var_id, const VarData &p_old_value, int p_flag) {
	if (p_flag & NetEventFlag::SYNC) {
		const EndSyncEvent ese(
				p_object_data,
				p_var_id,
				p_old_value);
		auto see_it = VecFunc::find(sync_end_events, ese);
		if (see_it == sync_end_events.end()) {
			sync_end_events.push_back(ese);
		} else {
			see_it->old_value.copy(p_old_value);
		}
	}
}

void ClientSynchronizer::signal_end_sync_changed_variables_events() {
	scene_synchronizer->change_events_begin(NetEventFlag::END_SYNC);
	for (auto &e : sync_end_events) {
		// Check if the values between the variables before the sync and the
		// current one are different.
		if (SceneSynchronizerBase::var_data_compare(
					e.object_data->vars[e.var_id.id].var.value,
					e.old_value) == false) {
			// Are different so we need to emit the `END_SYNC`.
			scene_synchronizer->change_event_add(
					e.object_data,
					e.var_id,
					e.old_value);
		}
	}
	sync_end_events.clear();

	scene_synchronizer->change_events_flush();
}

void ClientSynchronizer::on_controller_reset(NS::ObjectData *p_object_data) {
#ifdef DEBUG_ENABLED
	CRASH_COND(p_object_data->get_controller() == nullptr);
#endif

	if (player_controller_object_data == p_object_data) {
		// Reset the node_data.
		player_controller_object_data = nullptr;
		server_snapshots.clear();
		client_snapshots.clear();
	}

	if (p_object_data->get_controller()->is_player_controller()) {
		if (player_controller_object_data != nullptr) {
			SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "Only one player controller is supported, at the moment. Make sure this is the case.");
		} else {
			// Set this player controller as active.
			player_controller_object_data = p_object_data;
			server_snapshots.clear();
			client_snapshots.clear();
		}
	}
}

void ClientSynchronizer::store_snapshot() {
	NetworkedControllerBase *controller = player_controller_object_data->get_controller();

#ifdef DEBUG_ENABLED
	if (unlikely(client_snapshots.size() > 0 && controller->get_current_input_id() <= client_snapshots.back().input_id)) {
		CRASH_NOW_MSG("[FATAL] During snapshot creation, for controller " + String(player_controller_object_data->object_name.c_str()) + ", was found an ID for an older snapshots. New input ID: " + itos(controller->get_current_input_id()) + " Last saved snapshot input ID: " + itos(client_snapshots.back().input_id) + ".");
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

	if (p_snapshot.input_id == UINT32_MAX) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The Client received the server snapshot WITHOUT `input_id`.", true);
		// The controller node is not registered so just assume this snapshot is the most up-to-date.
		r_snapshot_storage.clear();
		r_snapshot_storage.push_back(Snapshot::make_copy(p_snapshot));

	} else {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The Client received the server snapshot: " + itos(p_snapshot.input_id), true);

		// Store the snapshot sorted by controller input ID.
		if (r_snapshot_storage.empty() == false) {
			// Make sure the snapshots are stored in order.
			const uint32_t last_stored_input_id = r_snapshot_storage.back().input_id;
			if (p_snapshot.input_id == last_stored_input_id) {
				// Update the snapshot.
				r_snapshot_storage.back().copy(p_snapshot);
			} else {
				ERR_FAIL_COND_MSG(p_snapshot.input_id < last_stored_input_id, "This snapshot (with ID: " + itos(p_snapshot.input_id) + ") is not expected because the last stored id is: " + itos(last_stored_input_id));
				r_snapshot_storage.push_back(Snapshot::make_copy(p_snapshot));
			}
		} else {
			r_snapshot_storage.push_back(Snapshot::make_copy(p_snapshot));
		}
	}
}

void ClientSynchronizer::process_server_sync(float p_delta) {
	process_received_server_state(p_delta);

	// Now trigger the END_SYNC event.
	signal_end_sync_changed_variables_events();
}

void ClientSynchronizer::process_received_server_state(real_t p_delta) {
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

	if (!player_controller_object_data) {
		// There is no player controller, we can't apply any snapshot which
		// `input_id` is not UINT32_MAX.
		return;
	}

	NetworkedControllerBase *controller = player_controller_object_data->get_controller();
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
				player_controller_object_data,
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
		std::vector<std::string> variable_names;
		std::vector<VarData> server_values;
		std::vector<VarData> client_values;

		// Emit the de-sync detected signal.
		for (
				int i = 0;
				i < int(different_node_data.size());
				i += 1) {
			const ObjectNetId net_node_id = different_node_data[i];
			NS::ObjectData *rew_node_data = scene_synchronizer->get_object_data(net_node_id);

			const std::vector<NS::NameAndVar> *server_node_vars = ObjectNetId{ uint32_t(server_snapshots.front().object_vars.size()) } <= net_node_id ? nullptr : &(server_snapshots.front().object_vars[net_node_id.id]);
			const std::vector<NS::NameAndVar> *client_node_vars = ObjectNetId{ uint32_t(client_snapshots.front().object_vars.size()) } <= net_node_id ? nullptr : &(client_snapshots.front().object_vars[net_node_id.id]);

			const std::size_t count = MAX(server_node_vars ? server_node_vars->size() : 0, client_node_vars ? client_node_vars->size() : 0);

			variable_names.resize(count);
			server_values.resize(count);
			client_values.resize(count);

			for (std::size_t g = 0; g < count; ++g) {
				if (server_node_vars && g < server_node_vars->size()) {
					variable_names[g] = (*server_node_vars)[g].name;
					server_values[g].copy((*server_node_vars)[g].value);
				} else {
					server_values[g] = VarData();
				}

				if (client_node_vars && g < client_node_vars->size()) {
					variable_names[g] = (*client_node_vars)[g].name;
					client_values[g].copy((*client_node_vars)[g].value);
				} else {
					client_values[g] = VarData();
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

	std::vector<std::string> applied_data_info;

	const NS::Snapshot &server_snapshot = server_snapshots.front();
	apply_snapshot(
			server_snapshot,
			NetEventFlag::SYNC_RECOVER | NetEventFlag::SYNC_RESET,
			scene_synchronizer->debug_rewindings_enabled ? &applied_data_info : nullptr);

	if (applied_data_info.size() > 0) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "Full reset:");
		for (int i = 0; i < int(applied_data_info.size()); i++) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|- " + String(applied_data_info[i].c_str()));
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
	std::vector<std::string> applied_data_info;

	apply_snapshot(
			p_no_rewind_recover,
			NetEventFlag::SYNC_RECOVER,
			scene_synchronizer->debug_rewindings_enabled ? &applied_data_info : nullptr,
			// ALWAYS skips custom data because partial snapshots don't contain custom_data.
			true);

	if (applied_data_info.size() > 0) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "Partial reset:");
		for (int i = 0; i < int(applied_data_info.size()); i++) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|- " + String(applied_data_info[i].c_str()));
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
	std::vector<std::string> applied_data_info;

	apply_snapshot(
			server_snapshots.front(),
			NetEventFlag::SYNC_RECOVER,
			&applied_data_info);

	server_snapshots.pop_front();

	if (applied_data_info.size() > 0) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "Paused controller recover:");
		for (int i = 0; i < int(applied_data_info.size()); i++) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "|- " + String(applied_data_info[i].c_str()));
		}
	}
}

void ClientSynchronizer::process_simulation(real_t p_delta, real_t p_physics_ticks_per_second) {
	if (unlikely(player_controller_object_data == nullptr || enabled == false)) {
		// No player controller so can't process the simulation.
		// TODO Remove this constraint?

		// Make sure to fetch changed variable anyway.
		scene_synchronizer->detect_and_signal_changed_variables(NetEventFlag::CHANGE);
		return;
	}

	NetworkedControllerBase *controller = player_controller_object_data->get_controller();
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

bool ClientSynchronizer::parse_sync_data(
		DataBuffer &p_snapshot,
		void *p_user_pointer,
		void (*p_custom_data_parse)(void *p_user_pointer, VarData &&p_custom_data),
		void (*p_node_parse)(void *p_user_pointer, NS::ObjectData *p_object_data),
		void (*p_input_id_parse)(void *p_user_pointer, uint32_t p_input_id),
		void (*p_controller_parse)(void *p_user_pointer, NS::ObjectData *p_object_data),
		void (*p_variable_parse)(void *p_user_pointer, NS::ObjectData *p_object_data, VarId p_var_id, VarData &&p_value),
		void (*p_simulated_objects_parse)(void *p_user_pointer, std::vector<ObjectNetId> &&p_simulated_objects)) {
	// The snapshot is a DataBuffer that contains the scene informations.
	// NOTE: Check generate_snapshot to see the DataBuffer format.

	p_snapshot.begin_read();
	if (p_snapshot.size() <= 0) {
		// Nothing to do.
		return true;
	}

	{
		// Fetch the `InputID`.
		std::uint32_t input_id;
		p_snapshot.read(input_id);
		ERR_FAIL_COND_V_MSG(p_snapshot.is_buffer_failed(), false, "This snapshot is corrupted as the `InputID` expected is not set.");
		p_input_id_parse(p_user_pointer, input_id);

		// Fetch `active_node_list_byte_array`.
		bool has_active_list_array;
		p_snapshot.read(has_active_list_array);
		ERR_FAIL_COND_V_MSG(p_snapshot.is_buffer_failed(), false, "This snapshot is corrupted as the `has_active_list_array` boolean expected is not set.");
		if (has_active_list_array) {
			std::vector<ObjectNetId> simulated_objects;
			simulated_objects.reserve(scene_synchronizer->get_all_object_data().size());

			// Fetch the array.
			while (true) {
				ObjectNetId id;
				p_snapshot.read(id.id);
				ERR_FAIL_COND_V_MSG(p_snapshot.is_buffer_failed(), false, "This snapshot is corrupted as fetching `ObjectNetId` failed.");

				if (id == ObjectNetId::NONE) {
					// The end.
					break;
				}
				simulated_objects.push_back(id);
			}

			p_simulated_objects_parse(p_user_pointer, std::move(simulated_objects));
		}
	}

	{
		bool has_custom_data = false;
		p_snapshot.read(has_custom_data);
		if (has_custom_data) {
			VarData vd;
			SceneSynchronizerBase::var_data_decode(vd, p_snapshot);
			p_custom_data_parse(p_user_pointer, std::move(vd));
		}
	}

	while (true) {
		// First extract the object data
		NS::ObjectData *synchronizer_object_data = nullptr;
		{
			ObjectNetId net_id = ObjectNetId::NONE;
			p_snapshot.read(net_id.id);
			ERR_FAIL_COND_V_MSG(p_snapshot.is_buffer_failed(), false, "This snapshot is corrupted. The NetId was expected at this point.");

			if (net_id == ObjectNetId::NONE) {
				// All the Objects fetched.
				break;
			}

			bool has_object_name = false;
			p_snapshot.read(has_object_name);
			ERR_FAIL_COND_V_MSG(p_snapshot.is_buffer_failed(), false, "This snapshot is corrupted. The `has_object_name` was expected at this point.");

			std::string object_name;
			if (has_object_name) {
				// Extract the object name
				p_snapshot.read(object_name);
				ERR_FAIL_COND_V_MSG(p_snapshot.is_buffer_failed(), false, "This snapshot is corrupted. The `object_name` was expected at this point.");

				// Associate the ID with the path.
				objects_names.insert(std::pair(net_id, object_name));
			}

			// Fetch the ObjectData.
			synchronizer_object_data = scene_synchronizer->get_object_data(net_id, false);
			if (!synchronizer_object_data) {
				// ObjectData not found, fetch it using the object name.

				if (object_name.empty()) {
					// The object_name was not specified by this snapshot, so fetch it
					const std::string *object_name_ptr = NS::MapFunc::at(objects_names, net_id);

					if (object_name_ptr == nullptr) {
						// The name for this `NodeId` doesn't exists yet.
						SceneSynchronizerDebugger::singleton()->debug_warning(&scene_synchronizer->get_network_interface(), "The object with ID `" + itos(net_id.id) + "` is not know by this peer yet.");
						notify_server_full_snapshot_is_needed();
					} else {
						object_name = *object_name_ptr;
					}
				}

				// Now fetch the object handle
				const ObjectHandle app_object_handle =
						scene_synchronizer->synchronizer_manager->fetch_app_object(object_name);

				if (app_object_handle == ObjectHandle::NONE) {
					// The node doesn't exists.
					SceneSynchronizerDebugger::singleton()->debug_warning(&scene_synchronizer->get_network_interface(), "The object " + String(object_name.c_str()) + " still doesn't exist.");
				} else {
					// Register this object, so to make sure the client is tracking it.
					ObjectLocalId reg_obj_id;
					scene_synchronizer->register_app_object(app_object_handle, &reg_obj_id);
					if (reg_obj_id != ObjectLocalId::NONE) {
						synchronizer_object_data = scene_synchronizer->get_object_data(reg_obj_id);
						// Set the NetId.
						synchronizer_object_data->set_net_id(net_id);
					} else {
						SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "[BUG] This object " + String(object_name.c_str()) + " was known on this client. Though, was not possible to register it as sync object.");
					}
				}
			}
		}

		const bool skip_object = synchronizer_object_data == nullptr;

		if (!skip_object) {
#ifdef DEBUG_ENABLED
			// At this point the ID is never UINT32_MAX thanks to the above
			// mechanism.
			CRASH_COND(synchronizer_object_data->get_net_id() == ObjectNetId::NONE);
#endif

			p_node_parse(p_user_pointer, synchronizer_object_data);

			if (synchronizer_object_data->get_controller()) {
				p_controller_parse(p_user_pointer, synchronizer_object_data);
			}
		}

		// Now it's time to fetch the variables.
		std::uint8_t vars_count;
		p_snapshot.read(vars_count);
		ERR_FAIL_COND_V_MSG(p_snapshot.is_buffer_failed(), false, String() + "This snapshot is corrupted. The `vars_count` was expected here.");

		if (skip_object) {
			// Skip all the variables for this object.
			for (std::uint8_t rvid = 0; rvid < vars_count; rvid++) {
				bool var_has_value = false;
				p_snapshot.read(var_has_value);
				if (var_has_value) {
					p_snapshot.read_variant();
				}
			}
		} else {
			for (auto &var_desc : synchronizer_object_data->vars) {
				bool var_has_value = false;
				p_snapshot.read(var_has_value);
				ERR_FAIL_COND_V_MSG(p_snapshot.is_buffer_failed(), false, String() + "This snapshot is corrupted. The `var_has_value` was expected at this point. Object: `" + synchronizer_object_data->object_name.c_str() + "` Var: `" + var_desc.var.name.c_str() + "`");

				if (var_has_value) {
					VarData value;
					SceneSynchronizerBase::var_data_decode(value, p_snapshot);
					ERR_FAIL_COND_V_MSG(p_snapshot.is_buffer_failed(), false, String() + "This snapshot is corrupted. The `variable value` was expected at this point. Object: `" + synchronizer_object_data->object_name.c_str() + "` Var: `" + var_desc.var.name.c_str() + "`");

					// Variable fetched, now parse this variable.
					p_variable_parse(
							p_user_pointer,
							synchronizer_object_data,
							var_desc.id,
							std::move(value));
				}
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

void ClientSynchronizer::receive_trickled_sync_data(const Vector<uint8_t> &p_data) {
	DataBuffer future_epoch_buffer(p_data);
	future_epoch_buffer.begin_read();

	int remaining_size = future_epoch_buffer.size() - future_epoch_buffer.get_bit_offset();
	if (remaining_size < DataBuffer::get_bit_taken(DataBuffer::DATA_TYPE_UINT, DataBuffer::COMPRESSION_LEVEL_1)) {
		SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "[FATAL] The function `receive_trickled_sync_data` received malformed data.");
		// Nothing to fetch.
		return;
	}

	const uint32_t epoch = future_epoch_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_1);

	DataBuffer *db = memnew(DataBuffer);

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
			SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "[FATAL] The function `receive_trickled_sync_data` failed applying the epoch because the received buffer is malformed. The node with ID `" + itos(node_id.id) + "` reported that the sub buffer size is `" + itos(buffer_bit_count) + "` but the main-buffer doesn't have so many bits.");
			break;
		}

		const int current_offset = future_epoch_buffer.get_bit_offset();
		const int expected_bit_offset_after_apply = current_offset + buffer_bit_count;

		NS::ObjectData *od = scene_synchronizer->get_object_data(node_id, false);
		if (od == nullptr) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The function `receive_trickled_sync_data` is skipping the node with ID `" + itos(node_id.id) + "` as it was not found locally.");
			future_epoch_buffer.seek(expected_bit_offset_after_apply);
			continue;
		}

		Vector<uint8_t> future_buffer_data;
		future_buffer_data.resize(Math::ceil(float(buffer_bit_count) / 8.0));
		future_epoch_buffer.read_bits(future_buffer_data.ptrw(), buffer_bit_count);
		CRASH_COND_MSG(future_epoch_buffer.get_bit_offset() != expected_bit_offset_after_apply, "At this point the buffer is expected to be exactly at this bit.");

		int64_t index = trickled_sync_array.find(od);
		if (index == -1) {
			index = trickled_sync_array.size();
			trickled_sync_array.push_back(TrickledSyncInterpolationData(od));
		}
		TrickledSyncInterpolationData &stream = trickled_sync_array[index];
#ifdef DEBUG_ENABLED
		CRASH_COND(stream.od != od);
#endif
		stream.future_epoch_buffer.copy(future_buffer_data);

		stream.past_epoch_buffer.begin_write(0);

		// 2. Now collect the past epoch buffer by reading the current values.
		db->begin_write(0);

		if (!stream.od->func_trickled_collect) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The function `receive_trickled_sync_data` is skipping the node `" + String(stream.od->object_name.c_str()) + "` as the function `trickled_collect` failed executing.");
			future_epoch_buffer.seek(expected_bit_offset_after_apply);
			continue;
		}

		if (stream.past_epoch != UINT32_MAX) {
			stream.od->func_trickled_collect(*db, 1.0);
			stream.past_epoch_buffer.copy(*db);
		} else {
			// Streaming not started.
			stream.past_epoch_buffer.copy(stream.future_epoch_buffer);
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

void ClientSynchronizer::process_trickled_sync(real_t p_delta) {
	DataBuffer *db1 = memnew(DataBuffer);
	DataBuffer *db2 = memnew(DataBuffer);

	for (int i = 0; i < int(trickled_sync_array.size()); ++i) {
		TrickledSyncInterpolationData &stream = trickled_sync_array[i];
		if (stream.alpha > 1.2) {
			// The stream is not yet started.
			// OR
			// The stream for this node is stopped as the data received is old.
			continue;
		}

		NS::ObjectData *od = stream.od;
		if (od == nullptr) {
			SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The function `process_received_trickled_sync_data` found a null NodeData into the `trickled_sync_array`; this is not supposed to happen.");
			continue;
		}

#ifdef DEBUG_ENABLED
		if (!od->func_trickled_apply) {
			SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "The function `process_received_trickled_sync_data` skip the node `" + String(od->object_name.c_str()) + "` has an invalid apply epoch function named `trickled_apply`. Remotely you used the function `setup_trickled_sync` properly, while locally you didn't. Fix it.");
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

		od->func_trickled_apply(p_delta, stream.alpha, *db1, *db2);
	}

	memdelete(db1);
	memdelete(db2);
}

void ClientSynchronizer::remove_object_from_trickled_sync(NS::ObjectData *p_object_data) {
	const int64_t index = trickled_sync_array.find(p_object_data);
	if (index >= 0) {
		trickled_sync_array.remove_at_unordered(index);
	}
}

bool ClientSynchronizer::parse_snapshot(DataBuffer &p_snapshot) {
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

	NS::Snapshot received_snapshot;
	received_snapshot.copy(last_received_snapshot);
	received_snapshot.input_id = UINT32_MAX;

	struct ParseData {
		NS::Snapshot &snapshot;
		NS::ObjectData *player_controller_node_data;
		SceneSynchronizerBase *scene_synchronizer;
		ClientSynchronizer *client_synchronizer;
	};

	ParseData parse_data{
		received_snapshot,
		player_controller_object_data,
		scene_synchronizer,
		this
	};

	const bool success = parse_sync_data(
			p_snapshot,
			&parse_data,

			// Custom data:
			[](void *p_user_pointer, VarData &&p_custom_data) {
				ParseData *pd = static_cast<ParseData *>(p_user_pointer);
				pd->snapshot.has_custom_data = true;
				pd->snapshot.custom_data = std::move(p_custom_data);
			},

			// Parse node:
			[](void *p_user_pointer, NS::ObjectData *p_object_data) {
				ParseData *pd = static_cast<ParseData *>(p_user_pointer);

#ifdef DEBUG_ENABLED
				// This function should never receive undefined IDs.
				CRASH_COND(p_object_data->get_net_id() == ObjectNetId::NONE);
#endif

				// Make sure this node is part of the server node too.
				if (uint32_t(pd->snapshot.object_vars.size()) <= p_object_data->get_net_id().id) {
					pd->snapshot.object_vars.resize(p_object_data->get_net_id().id + 1);
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
			[](void *p_user_pointer, NS::ObjectData *p_object_data, VarId p_var_id, VarData &&p_value) {
				ParseData *pd = static_cast<ParseData *>(p_user_pointer);

				if (p_object_data->vars.size() != uint32_t(pd->snapshot.object_vars[p_object_data->get_net_id().id].size())) {
					// The parser may have added a variable, so make sure to resize the vars array.
					pd->snapshot.object_vars[p_object_data->get_net_id().id].resize(p_object_data->vars.size());
				}

				pd->snapshot.object_vars[p_object_data->get_net_id().id][p_var_id.id].name = p_object_data->vars[p_var_id.id].var.name;
				pd->snapshot.object_vars[p_object_data->get_net_id().id][p_var_id.id].value = std::move(p_value);
			},

			// Parse node activation:
			[](void *p_user_pointer, std::vector<ObjectNetId> &&p_simulated_objects) {
				ParseData *pd = static_cast<ParseData *>(p_user_pointer);
				pd->snapshot.simulated_objects = std::move(p_simulated_objects);
			});

	if (success == false) {
		SceneSynchronizerDebugger::singleton()->debug_error(&scene_synchronizer->get_network_interface(), "Snapshot parsing failed.");
		return false;
	}

	if (unlikely(received_snapshot.input_id == UINT32_MAX && player_controller_object_data != nullptr)) {
		// We espect that the player_controller is updated by this new snapshot,
		// so make sure it's done so.
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "[INFO] the player controller (" + String(player_controller_object_data->object_name.c_str()) + ") was not part of the received snapshot, this happens when the server destroys the peer controller.");
	}

	last_received_snapshot = std::move(received_snapshot);

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

void ClientSynchronizer::update_client_snapshot(NS::Snapshot &r_snapshot) {
	scene_synchronizer->synchronizer_manager->snapshot_get_custom_data(nullptr, r_snapshot.custom_data);

	// Make sure we have room for all the NodeData.
	r_snapshot.object_vars.resize(scene_synchronizer->objects_data_storage.get_sorted_objects_data().size());

	// Fetch the data.
	for (ObjectNetId net_node_id = { 0 }; net_node_id < ObjectNetId{ uint32_t(scene_synchronizer->objects_data_storage.get_sorted_objects_data().size()) }; net_node_id += 1) {
		NS::ObjectData *nd = scene_synchronizer->objects_data_storage.get_object_data(net_node_id);
		if (nd == nullptr || nd->realtime_sync_enabled_on_client == false) {
			continue;
		}

		// Make sure this ID is valid.
		ERR_FAIL_COND_MSG(nd->get_net_id() == ObjectNetId::NONE, "[BUG] It's not expected that the client has an uninitialized NetNodeId into the `organized_node_data` ");

#ifdef DEBUG_ENABLED
		CRASH_COND_MSG(nd->get_net_id().id >= uint32_t(r_snapshot.object_vars.size()), "This array was resized above, this can't be triggered.");
#endif

		std::vector<NS::NameAndVar> *snap_node_vars = r_snapshot.object_vars.data() + nd->get_net_id().id;
		snap_node_vars->resize(nd->vars.size());

		NS::NameAndVar *snap_node_vars_ptr = snap_node_vars->data();
		for (uint32_t v = 0; v < nd->vars.size(); v += 1) {
			if (nd->vars[v].enabled) {
				snap_node_vars_ptr[v].name = nd->vars[v].var.name;
				snap_node_vars_ptr[v].value.copy(nd->vars[v].var.value);
			} else {
				snap_node_vars_ptr[v].name = std::string();
				snap_node_vars_ptr[v].value = VarData();
			}
		}
	}
}

void ClientSynchronizer::update_simulated_objects_list(const std::vector<ObjectNetId> &p_simulated_objects) {
	// Reset the simulated object first.
	for (auto od : scene_synchronizer->get_all_object_data()) {
		const bool is_simulating = NS::VecFunc::has(p_simulated_objects, od->get_net_id());
		if (od->realtime_sync_enabled_on_client != is_simulating) {
			od->realtime_sync_enabled_on_client = is_simulating;

			// Make sure the process_function cache is cleared.
			scene_synchronizer->process_functions__clear();

			// Make sure this node is NOT into the trickled sync list.
			if (is_simulating) {
				remove_object_from_trickled_sync(od);
			}
		}
	}
	simulated_objects = p_simulated_objects;
}

void ClientSynchronizer::apply_snapshot(
		const NS::Snapshot &p_snapshot,
		int p_flag,
		std::vector<std::string> *r_applied_data_info,
		bool p_skip_custom_data) {
	const std::vector<NS::NameAndVar> *objects_vars = p_snapshot.object_vars.data();

	scene_synchronizer->change_events_begin(p_flag);

	update_simulated_objects_list(p_snapshot.simulated_objects);

	for (ObjectNetId net_node_id = { 0 }; net_node_id < ObjectNetId{ uint32_t(p_snapshot.object_vars.size()) }; net_node_id += 1) {
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

		const std::vector<NS::NameAndVar> &vars = objects_vars[net_node_id.id];
		const NS::NameAndVar *vars_ptr = vars.data();

		if (r_applied_data_info) {
			r_applied_data_info->push_back("Applied snapshot data on the node: " + std::string(nd->object_name.c_str()));
		}

		// NOTE: The vars may not contain ALL the variables: it depends on how
		//       the snapshot was captured.
		for (VarId v = { 0 }; v < VarId{ uint32_t(vars.size()) }; v += 1) {
			if (vars_ptr[v.id].name.empty()) {
				// This variable was not set, skip it.
				continue;
			}

			VarData current_val;
			const bool get_var_success = scene_synchronizer->synchronizer_manager->get_variable(nd->app_object_handle, nd->vars[v.id].var.name.c_str(), current_val);

			if (!get_var_success || !SceneSynchronizerBase::var_data_compare(current_val, vars_ptr[v.id].value)) {
				nd->vars[v.id].var.value.copy(vars_ptr[v.id].value);

				scene_synchronizer->synchronizer_manager->set_variable(
						nd->app_object_handle,
						vars_ptr[v.id].name.c_str(),
						vars_ptr[v.id].value);
				scene_synchronizer->change_event_add(
						nd,
						v,
						current_val);

				if (r_applied_data_info) {
					r_applied_data_info->push_back(std::string() + " |- Variable: " + vars_ptr[v.id].name + " New value: " + SceneSynchronizerBase::var_data_stringify(vars_ptr[v.id].value));
				}
			}
		}
	}

	if (p_snapshot.has_custom_data && !p_skip_custom_data) {
		scene_synchronizer->synchronizer_manager->snapshot_set_custom_data(p_snapshot.custom_data);
	}

	scene_synchronizer->change_events_flush();
}

NS_NAMESPACE_END
