
#include "scene_synchronizer.h"

#include "core/error/error_macros.h"

#include "core/core.h"
#include "core/ensure.h"
#include "core/object_data.h"
#include "core/var_data.h"
#include "data_buffer.h"
#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/core/ensure.h"
#include "modules/network_synchronizer/scene_synchronizer.h"
#include "modules/network_synchronizer/scene_synchronizer_debugger.h"
#include "net_utilities.h"
#include "networked_controller.h"
#include "scene_synchronizer.h"
#include "scene_synchronizer_debugger.h"
#include "snapshot.h"
#include <chrono>
#include <limits>
#include <string>

NS_NAMESPACE_BEGIN

void (*SceneSynchronizerBase::var_data_encode_func)(DataBuffer &r_buffer, const NS::VarData &p_val) = nullptr;
void (*SceneSynchronizerBase::var_data_decode_func)(NS::VarData &r_val, DataBuffer &p_buffer) = nullptr;
bool (*SceneSynchronizerBase::var_data_compare_func)(const VarData &p_A, const VarData &p_B) = nullptr;
std::string (*SceneSynchronizerBase::var_data_stringify_func)(const VarData &p_var_data, bool p_verbose) = nullptr;
void (*SceneSynchronizerBase::print_line_func)(const std::string &p_str) = nullptr;
void (*SceneSynchronizerBase::print_code_message_func)(const char *p_function, const char *p_file, int p_line, const std::string &p_error, const std::string &p_message, NS::PrintMessageType p_type) = nullptr;
void (*SceneSynchronizerBase::print_flush_stdout_func)() = nullptr;

SceneSynchronizerBase::SceneSynchronizerBase(NetworkInterface *p_network_interface, bool p_pedantic_checks) :
#ifdef DEBUG_ENABLED
		pedantic_checks(p_pedantic_checks),
#endif
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

void SceneSynchronizerBase::install_synchronizer(
		void (*p_var_data_encode_func)(DataBuffer &r_buffer, const NS::VarData &p_val),
		void (*p_var_data_decode_func)(NS::VarData &r_val, DataBuffer &p_buffer),
		bool (*p_var_data_compare_func)(const VarData &p_A, const VarData &p_B),
		std::string (*p_var_data_stringify_func)(const VarData &p_var_data, bool p_verbose),
		void (*p_print_line_func)(const std::string &p_str),
		void (*p_print_code_message_func)(const char *p_function, const char *p_file, int p_line, const std::string &p_error, const std::string &p_message, NS::PrintMessageType p_type),
		void (*p_print_flush_stdout_func)()) {
	var_data_encode_func = p_var_data_encode_func;
	var_data_decode_func = p_var_data_decode_func;
	var_data_compare_func = p_var_data_compare_func;
	var_data_stringify_func = p_var_data_stringify_func;

	print_line_func = p_print_line_func;
	print_code_message_func = p_print_code_message_func;
	print_flush_stdout_func = p_print_flush_stdout_func;
}

void SceneSynchronizerBase::setup(SynchronizerManager &p_synchronizer_interface) {
	synchronizer_manager = &p_synchronizer_interface;
	network_interface->start_listening_peer_connection(
			[this](int p_peer) { on_peer_connected(p_peer); },
			[this](int p_peer) { on_peer_disconnected(p_peer); });

	rpc_handler_latency =
			network_interface->rpc_config(
					std::function<void()>(std::bind(&SceneSynchronizerBase::rpc_latency, this)),
					true,
					false);

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

	rpc_handle_notify_fps_acceleration =
			network_interface->rpc_config(
					std::function<void(const Vector<uint8_t> &)>(std::bind(&SceneSynchronizerBase::rpc_notify_fps_acceleration, this, std::placeholders::_1)),
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
	rpc_handle_notify_fps_acceleration.reset();
}

void SceneSynchronizerBase::process(double p_delta) {
	NS_PROFILE

#ifdef DEBUG_ENABLED
	CRASH_COND_MSG(synchronizer == nullptr, "Never execute this function unless this synchronizer is ready.");

	synchronizer_manager->debug_only_validate_objects();
#endif

	synchronizer->process(p_delta);
}

void SceneSynchronizerBase::on_app_object_removed(ObjectHandle p_app_object_handle) {
	unregister_app_object(find_object_local_id(p_app_object_handle));
}

void SceneSynchronizerBase::var_data_encode(DataBuffer &r_buffer, const NS::VarData &p_val) {
	NS_PROFILE
	var_data_encode_func(r_buffer, p_val);
}

void SceneSynchronizerBase::var_data_decode(NS::VarData &r_val, DataBuffer &p_buffer) {
	NS_PROFILE
	var_data_decode_func(r_val, p_buffer);
}

bool SceneSynchronizerBase::var_data_compare(const VarData &p_A, const VarData &p_B) {
	NS_PROFILE
	return var_data_compare_func(p_A, p_B);
}

std::string SceneSynchronizerBase::var_data_stringify(const VarData &p_var_data, bool p_verbose) {
	NS_PROFILE
	return var_data_stringify_func(p_var_data, p_verbose);
}

void SceneSynchronizerBase::__print_line(const std::string &p_str) {
	print_line_func(p_str);
}

void SceneSynchronizerBase::print_code_message(const char *p_function, const char *p_file, int p_line, const std::string &p_error, const std::string &p_message, NS::PrintMessageType p_type) {
	const std::string log_level_str = NS::get_log_level_txt(p_type);
	std::string msg = log_level_str + " The condition " + p_error + " evaluated to false: " + p_message + "\n";
	msg += std::string() + "At: " + p_file + "::" + p_file + "::" + std::to_string(p_line);
	SceneSynchronizerDebugger::singleton()->__add_message(msg, "SceneSync");
	print_code_message_func(p_function, p_file, p_line, p_error, p_message, p_type);
}

void SceneSynchronizerBase::print_flush_stdout() {
	print_flush_stdout_func();
}

void SceneSynchronizerBase::set_frames_per_seconds(int p_fps) {
	frames_per_seconds = std::max(p_fps, 1);
	fixed_frame_delta = 1.0 / frames_per_seconds;
}

int SceneSynchronizerBase::get_frames_per_seconds() const {
	return frames_per_seconds;
}

double SceneSynchronizerBase::get_fixed_frame_delta() const {
	return fixed_frame_delta;
}

void SceneSynchronizerBase::set_max_sub_process_per_frame(std::uint8_t p_max_sub_process_per_frame) {
	max_sub_process_per_frame = p_max_sub_process_per_frame;
}

std::uint8_t SceneSynchronizerBase::get_max_sub_process_per_frame() const {
	return max_sub_process_per_frame;
}

void SceneSynchronizerBase::set_tick_acceleration(double p_acceleration) {
	tick_acceleration = std::max(p_acceleration, 0.01);
}

double SceneSynchronizerBase::get_tick_acceleration() const {
	return tick_acceleration;
}

void SceneSynchronizerBase::set_tick_speedup_notification_delay(float p_delay_seconds) {
	tick_speedup_notification_delay = p_delay_seconds;
}

float SceneSynchronizerBase::get_tick_speedup_notification_delay() const {
	return tick_speedup_notification_delay;
}

void SceneSynchronizerBase::set_max_trickled_objects_per_update(int p_rate) {
	max_trickled_objects_per_update = p_rate;
}

int SceneSynchronizerBase::get_max_trickled_objects_per_update() const {
	return max_trickled_objects_per_update;
}

void SceneSynchronizerBase::set_max_trickled_interpolation_alpha(float p_int_alpha) {
	max_trickled_interpolation_alpha = std::max(p_int_alpha, 1.0f);
}

float SceneSynchronizerBase::get_max_trickled_interpolation_alpha() const {
	return max_trickled_interpolation_alpha;
}

void SceneSynchronizerBase::set_frame_confirmation_timespan(real_t p_interval) {
	frame_confirmation_timespan = p_interval;
}

float SceneSynchronizerBase::get_frame_confirmation_timespan() const {
	return frame_confirmation_timespan;
}

void SceneSynchronizerBase::set_max_predicted_intervals(float p_max_predicted_intevals) {
	max_predicted_intervals = std::max(p_max_predicted_intevals, 1.5f);
}

float SceneSynchronizerBase::get_max_predicted_intervals() const {
	return max_predicted_intervals;
}

void SceneSynchronizerBase::set_objects_relevancy_update_time(real_t p_time) {
	objects_relevancy_update_time = p_time;
}

real_t SceneSynchronizerBase::get_objects_relevancy_update_time() const {
	return objects_relevancy_update_time;
}

void SceneSynchronizerBase::set_latency_update_rate(float p_rate_seconds) {
	latency_update_rate = p_rate_seconds;
}

float SceneSynchronizerBase::get_latency_update_rate() const {
	return latency_update_rate;
}

bool SceneSynchronizerBase::is_variable_registered(ObjectLocalId p_id, const std::string &p_variable) const {
	const ObjectData *od = objects_data_storage.get_object_data(p_id);
	if (od != nullptr) {
		return od->find_variable_id(p_variable) != VarId::NONE;
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
			if make_unlikely (od->get_controller()->has_scene_synchronizer()) {
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

		SceneSynchronizerDebugger::singleton()->print(INFO, "New node registered" + (generate_id ? " #ID: " + std::to_string(od->get_net_id().id) : "") + " : " + od->object_name, network_interface->get_owner_name());

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

void SceneSynchronizerBase::register_app_object_as_controlled_by_peer(ObjectLocalId p_id, int p_peer) {
	ERR_FAIL_COND(p_id == ObjectLocalId::NONE);

	NS::ObjectData *object_data = get_object_data(p_id);
	ERR_FAIL_COND(object_data == nullptr);

	object_data->set_controlled_by_peer(p_peer);
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
			SceneSynchronizerDebugger::singleton()->print(ERROR, "The variable `" + p_variable + "` on the node `" + object_data->object_name + "` was not found, make sure the variable exist.", network_interface->get_owner_name());
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

const std::vector<ObjectData *> &SceneSynchronizerBase::get_sorted_objects_data() const {
	return objects_data_storage.get_sorted_objects_data();
}

const std::vector<ObjectData *> &SceneSynchronizerBase::get_all_object_data() const {
	return objects_data_storage.get_objects_data();
}

const std::vector<ObjectData *> *SceneSynchronizerBase::get_peer_controlled_objects_data(int p_peer) const {
	return objects_data_storage.get_peer_controlled_objects_data(p_peer);
}

VarId SceneSynchronizerBase::get_variable_id(ObjectLocalId p_id, const std::string &p_variable) {
	ENSURE_V(p_variable != "", VarId::NONE);

	NS::ObjectData *od = get_object_data(p_id);
	ENSURE_V_MSG(od, VarId::NONE, "This node " + od->object_name + "is not registered.");

	return od->find_variable_id(p_variable);
}

void SceneSynchronizerBase::set_skip_rewinding(ObjectLocalId p_id, const std::string &p_variable, bool p_skip_rewinding) {
	NS::ObjectData *od = get_object_data(p_id);
	ERR_FAIL_COND(od == nullptr);

	const VarId id = od->find_variable_id(p_variable);
	ERR_FAIL_COND(id == VarId::NONE);

	od->vars[id.id].skip_rewinding = p_skip_rewinding;
}

ListenerHandle SceneSynchronizerBase::track_variable_changes(
		ObjectLocalId p_id,
		const std::string &p_variable,
		std::function<void(const std::vector<VarData> &p_old_values)> p_listener_func,
		NetEventFlag p_flags) {
	std::vector<ObjectLocalId> object_ids;
	std::vector<std::string> variables;
	object_ids.push_back(p_id);
	variables.push_back(p_variable);
	return track_variables_changes(object_ids, variables, p_listener_func, p_flags);
}

ListenerHandle SceneSynchronizerBase::track_variables_changes(
		const std::vector<ObjectLocalId> &p_object_ids,
		const std::vector<std::string> &p_variables,
		std::function<void(const std::vector<VarData> &p_old_values)> p_listener_func,
		NetEventFlag p_flags) {
	ENSURE_V_MSG(p_object_ids.size() == p_variables.size(), nulllistenerhandle, "object_ids and variables should have the exact same size.");
	ENSURE_V_MSG(p_object_ids.size() != 0, nulllistenerhandle, "object_ids can't be of size 0");
	ENSURE_V_MSG(p_variables.size() != 0, nulllistenerhandle, "object_ids can't be of size 0");

	bool is_valid = true;

	// TODO allocate into a buffer instead of using `new`?
	ChangesListener *listener = new ChangesListener;
	listener->listener_func = p_listener_func;
	listener->flag = p_flags;

	listener->watching_vars.resize(p_object_ids.size());
	listener->old_values.resize(p_object_ids.size());
	for (int i = 0; i < int(p_object_ids.size()); i++) {
		ObjectLocalId id = p_object_ids[i];
		const std::string &variable_name = p_variables[i];

		NS::ObjectData *od = objects_data_storage.get_object_data(id);
		if (!od) {
			ERR_PRINT("The passed ObjectHandle `" + itos(id.id) + "` is not pointing to any valid NodeData. Make sure to register the variable first.");
			is_valid = false;
			break;
		}

		const VarId vid = od->find_variable_id(variable_name);
		if (vid == VarId::NONE) {
			SceneSynchronizerDebugger::singleton()->print(ERROR, "The passed variable `" + variable_name + "` doesn't exist under this object `" + od->object_name + "`.");
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

	// Before droplatency this listener, make sure to clear the NodeData.
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
	SceneSynchronizerDebugger::singleton()->print(INFO, "Setup trickled sync functions for: `" + od->object_name + "`.", network_interface->get_owner_name());
}

int SceneSynchronizerBase::get_peer_latency(int p_peer) const {
	const PeerData *pd = MapFunc::get_or_null(peer_data, p_peer);
	if (pd) {
		return pd->get_latency();
	} else {
		return -1;
	}
}

SyncGroupId SceneSynchronizerBase::sync_group_create() {
	ENSURE_V_MSG(is_server(), SyncGroupId::NONE, "This function CAN be used only on the server.");
	const SyncGroupId id = static_cast<ServerSynchronizer *>(synchronizer)->sync_group_create();
	synchronizer_manager->on_sync_group_created(id);
	return id;
}

const NS::SyncGroup *SceneSynchronizerBase::sync_group_get(SyncGroupId p_group_id) const {
	ENSURE_V_MSG(is_server(), nullptr, "This function CAN be used only on the server.");
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
	ENSURE_MSG(is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_remove_object(p_object_data, p_group_id);
}

void SceneSynchronizerBase::sync_group_replace_objects(SyncGroupId p_group_id, LocalVector<NS::SyncGroup::SimulatedObjectInfo> &&p_new_realtime_nodes, LocalVector<NS::SyncGroup::TrickledObjectInfo> &&p_new_trickled_nodes) {
	ENSURE_MSG(is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_replace_object(p_group_id, std::move(p_new_realtime_nodes), std::move(p_new_trickled_nodes));
}

void SceneSynchronizerBase::sync_group_remove_all_objects(SyncGroupId p_group_id) {
	ENSURE_MSG(is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_remove_all_objects(p_group_id);
}

void SceneSynchronizerBase::sync_group_move_peer_to(int p_peer_id, SyncGroupId p_group_id) {
	ENSURE_MSG(is_server(), "This function CAN be used only on the server.");

	PeerData *pd = MapFunc::get_or_null(peer_data, p_peer_id);
	ENSURE(pd);
	if (pd->authority_data.sync_group_id == p_group_id) {
		// Nothing to do.
		return;
	}

	pd->authority_data.sync_group_id = p_group_id;

	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_move_peer_to(p_peer_id, p_group_id);
}

SyncGroupId SceneSynchronizerBase::sync_group_get_peer_group(int p_peer_id) const {
	ENSURE_V_MSG(is_server(), SyncGroupId::NONE, "This function CAN be used only on the server.");

	// Update the sync group id
	const NS::PeerData *pd = MapFunc::get_or_null(peer_data, p_peer_id);
	if (pd) {
		return pd->authority_data.sync_group_id;
	}

	return SyncGroupId::NONE;
}

const std::vector<int> *SceneSynchronizerBase::sync_group_get_listening_peers(SyncGroupId p_group_id) const {
	ENSURE_V_MSG(is_server(), nullptr, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get_listening_peers(p_group_id);
}

void SceneSynchronizerBase::sync_group_set_trickled_update_rate(ObjectLocalId p_node_id, SyncGroupId p_group_id, real_t p_update_rate) {
	NS::ObjectData *od = get_object_data(p_node_id);
	ENSURE_MSG(is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_set_trickled_update_rate(od, p_group_id, p_update_rate);
}

void SceneSynchronizerBase::sync_group_set_trickled_update_rate(ObjectNetId p_node_id, SyncGroupId p_group_id, real_t p_update_rate) {
	NS::ObjectData *od = get_object_data(p_node_id);
	ENSURE_MSG(is_server(), "This function CAN be used only on the server.");
	static_cast<ServerSynchronizer *>(synchronizer)->sync_group_set_trickled_update_rate(od, p_group_id, p_update_rate);
}

real_t SceneSynchronizerBase::sync_group_get_trickled_update_rate(ObjectLocalId p_id, SyncGroupId p_group_id) const {
	const NS::ObjectData *od = get_object_data(p_id);
	ENSURE_V_MSG(is_server(), 0.0, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get_trickled_update_rate(od, p_group_id);
}

real_t SceneSynchronizerBase::sync_group_get_trickled_update_rate(ObjectNetId p_id, SyncGroupId p_group_id) const {
	const NS::ObjectData *od = get_object_data(p_id);
	ENSURE_V_MSG(is_server(), 0.0, "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_get_trickled_update_rate(od, p_group_id);
}

void SceneSynchronizerBase::sync_group_set_user_data(SyncGroupId p_group_id, uint64_t p_user_data) {
	ENSURE_MSG(is_server(), "This function CAN be used only on the server.");
	return static_cast<ServerSynchronizer *>(synchronizer)->sync_group_set_user_data(p_group_id, p_user_data);
}

uint64_t SceneSynchronizerBase::sync_group_get_user_data(SyncGroupId p_group_id) const {
	ENSURE_V_MSG(is_server(), 0, "This function CAN be used only on the server.");
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

std::size_t SceneSynchronizerBase::get_client_max_frames_storage_size() const {
	const float netsync_frame_per_seconds = get_frames_per_seconds();
	return std::ceil(get_frame_confirmation_timespan() * get_max_predicted_intervals() * netsync_frame_per_seconds);
}

void SceneSynchronizerBase::force_state_notify(SyncGroupId p_sync_group_id) {
	ERR_FAIL_COND(is_server() == false);
	ServerSynchronizer *r = static_cast<ServerSynchronizer *>(synchronizer);
	// + 1.0 is just a ridiculous high number to be sure to avoid float
	// precision error.
	ERR_FAIL_COND_MSG(p_sync_group_id.id >= r->sync_groups.size(), "The group id `" + itos(p_sync_group_id.id) + "` doesn't exist.");
	r->sync_groups[p_sync_group_id.id].state_notifier_timer = get_frame_confirmation_timespan() + 1.0;
}

void SceneSynchronizerBase::force_state_notify_all() {
	ERR_FAIL_COND(is_server() == false);
	ServerSynchronizer *r = static_cast<ServerSynchronizer *>(synchronizer);

	for (uint32_t i = 0; i < r->sync_groups.size(); ++i) {
		// + 1.0 is just a ridiculous high number to be sure to avoid float
		// precision error.
		r->sync_groups[i].state_notifier_timer = get_frame_confirmation_timespan() + 1.0;
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
	if make_likely (synchronizer_type == SYNCHRONIZER_TYPE_CLIENT) {
		return static_cast<ClientSynchronizer *>(synchronizer)->enabled;
	} else if (synchronizer_type == SYNCHRONIZER_TYPE_NONETWORK) {
		return static_cast<NoNetSynchronizer *>(synchronizer)->enabled;
	} else {
		return true;
	}
}

void SceneSynchronizerBase::set_peer_networking_enable(int p_peer, bool p_enable) {
	if (synchronizer_type == SYNCHRONIZER_TYPE_SERVER) {
		ENSURE_MSG(p_peer != 1, "Disable the server is not possible.");

		static_cast<ServerSynchronizer *>(synchronizer)->set_peer_networking_enable(p_peer, p_enable);

		dirty_peers();

		// Just notify the peer status.
		rpc_handler_notify_peer_status.rpc(*network_interface, p_peer, p_enable);
	} else {
		ERR_FAIL_COND_MSG(synchronizer_type != SYNCHRONIZER_TYPE_NONETWORK, "At this point no network is expected.");
		static_cast<NoNetSynchronizer *>(synchronizer)->set_enabled(p_enable);
	}
}

bool SceneSynchronizerBase::is_peer_networking_enabled(int p_peer) const {
	if (synchronizer_type == SYNCHRONIZER_TYPE_SERVER) {
		if (p_peer == 1) {
			// Server is always enabled.
			return true;
		}

		const PeerData *pd = MapFunc::get_or_null(peer_data, p_peer);
		if (pd) {
			return pd->authority_data.enabled;
		} else {
			return false;
		}

	} else {
		ENSURE_V_MSG(synchronizer_type == SYNCHRONIZER_TYPE_NONETWORK, false, "At this point no network is expected.");
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
	NS::PeerData *pd = MapFunc::get_or_null(peer_data, p_peer);
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

void SceneSynchronizerBase::rpc_latency() {
	if (is_client()) {
		// This is a client, latency the server back.
		rpc_handler_latency.rpc(get_network_interface(), get_network_interface().get_server_peer());
	} else if (is_server()) {
		const int sender_peer = get_network_interface().rpc_get_sender();
		static_cast<ServerSynchronizer *>(synchronizer)->notify_latency_received(sender_peer);
	} else {
		ERR_FAIL_MSG("[FATAL] The rpc latency function was executed on a peer that is not a client nor a server. This is a bug.");
	}
}

void SceneSynchronizerBase::rpc_receive_state(DataBuffer &p_snapshot) {
	ENSURE_MSG(is_client(), "Only clients are suposed to receive the server snapshot.");
	static_cast<ClientSynchronizer *>(synchronizer)->receive_snapshot(p_snapshot);
}

void SceneSynchronizerBase::rpc__notify_need_full_snapshot() {
	ENSURE_MSG(is_server(), "Only the server can receive the request to send a full snapshot.");

	const int peer = network_interface->rpc_get_sender();
	static_cast<ServerSynchronizer *>(synchronizer)->notify_need_full_snapshot(peer);
}

void SceneSynchronizerBase::rpc_set_network_enabled(bool p_enabled) {
	ENSURE_MSG(is_server(), "The peer status is supposed to be received by the server.");
	set_peer_networking_enable(
			network_interface->rpc_get_sender(),
			p_enabled);
}

void SceneSynchronizerBase::rpc_notify_peer_status(bool p_enabled) {
	ENSURE_MSG(is_client(), "The peer status is supposed to be received by the client.");
	static_cast<ClientSynchronizer *>(synchronizer)->set_enabled(p_enabled);
}

void SceneSynchronizerBase::rpc_trickled_sync_data(const Vector<uint8_t> &p_data) {
	ENSURE_MSG(is_client(), "Only clients are supposed to receive this function call.");
	ENSURE_MSG(p_data.size() > 0, "It's not supposed to receive a 0 size data.");

	static_cast<ClientSynchronizer *>(synchronizer)->receive_trickled_sync_data(p_data);
}

void SceneSynchronizerBase::rpc_notify_fps_acceleration(const Vector<uint8_t> &p_data) {
	ENSURE(is_client());
	ENSURE(p_data.size() == 1);

	int8_t additional_frames_to_produce;
	memcpy(
			&additional_frames_to_produce,
			&p_data[0],
			sizeof(int8_t));

	ClientSynchronizer *client_sync = static_cast<ClientSynchronizer *>(synchronizer);

	// Slowdown the acceleration when near the target.
	client_sync->acceleration_fps_speed = std::clamp(double(additional_frames_to_produce) / get_tick_acceleration(), -1.0, 1.0) * get_tick_acceleration();
	const double acceleration_fps_speed_ABS = std::abs(client_sync->acceleration_fps_speed);

	if (acceleration_fps_speed_ABS >= CMP_EPSILON2) {
		const double acceleration_time = double(std::abs(additional_frames_to_produce)) / acceleration_fps_speed_ABS;
		client_sync->acceleration_fps_timer = acceleration_time;
	} else {
		client_sync->acceleration_fps_timer = 0.0;
	}

#ifdef DEBUG_ENABLED
	const bool debug = ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/debug_server_speedup");
	if (debug) {
		SceneSynchronizerDebugger::singleton()->print(
				INFO,
				std::string() +
						"Client received speedup." +
						" Frames to produce: `" + std::to_string(additional_frames_to_produce) + "`" +
						" Acceleration fps: `" + std::to_string(client_sync->acceleration_fps_speed) + "`" +
						" Acceleration time: `" + std::to_string(client_sync->acceleration_fps_timer) + "`",
				get_network_interface().get_owner_name(),
				true);
	}
#endif
}

void SceneSynchronizerBase::update_peers() {
#ifdef DEBUG_ENABLED
	// This function is only called on server.
	ASSERT_COND(synchronizer_type == SYNCHRONIZER_TYPE_SERVER);
#endif

	const ServerSynchronizer *server_sync = static_cast<ServerSynchronizer *>(synchronizer);

	if make_likely (peer_dirty == false) {
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
			const bool enabled = is_peer_networking_enabled(it.first);
			nd->realtime_sync_enabled_on_client = enabled;
			event_peer_status_updated.broadcast(nd, it.first, true, enabled);
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
	const std::vector<ObjectData *> &active_objects = synchronizer->get_active_objects();

#ifdef NS_PROFILING_ENABLED
	const std::string info = "objects count: " + std::to_string(active_objects.size());
	NS_PROFILE_WITH_INFO(info);
#endif

	// Pull the changes.
	if (event_flag != p_flags) {
		// The flag was not set yet.
		change_events_begin(p_flags);
	}

	for (auto od : active_objects) {
		if (od) {
			pull_object_changes(od);
		}
	}
	change_events_flush();
}

void SceneSynchronizerBase::change_events_begin(int p_flag) {
	NS_PROFILE

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
	NS_PROFILE

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
	NS_PROFILE

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
		p_object_data.get_controller()->unregister_with_synchronizer(this);
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
	SceneSynchronizerDebugger::singleton()->print(INFO, "ObjectNetId: " + p_object_data.get_net_id() + " just assigned to: " + p_object_data.object_name, network_interface->get_owner_name());
}

NetworkedControllerBase *SceneSynchronizerBase::fetch_controller_by_peer(int peer) {
	const NS::PeerData *data = MapFunc::get_or_null(peer_data, peer);
	if (data && data->controller_id != ObjectNetId::NONE) {
		NS::ObjectData *nd = get_object_data(data->controller_id);
		if (nd) {
			return nd->get_controller();
		}
	}
	return nullptr;
}

FrameIndex SceneSynchronizerBase::client_get_last_checked_frame_index() const {
	ENSURE_V_MSG(is_client(), FrameIndex::NONE, "This function can be called only on client scene synchronizer.");
	return static_cast<ClientSynchronizer *>(synchronizer)->last_checked_input;
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

void SceneSynchronizerBase::process_functions__execute() {
	const std::string info = "delta: " + std::to_string(get_fixed_frame_delta());
	NS_PROFILE_WITH_INFO(info);

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
		const std::string info = "process phase: " + std::to_string(process_phase);
		NS_PROFILE_WITH_INFO(info);
		cached_process_functions[process_phase].broadcast(get_fixed_frame_delta());
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
	const NS::PeerData *pd = MapFunc::get_or_null(peer_data, p_peer);
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
	const NS::PeerData *pd = MapFunc::get_or_null(peer_data, p_peer);
	if (p_expected) {
		ERR_FAIL_COND_V_MSG(pd == nullptr, nullptr, "The peer is unknown `" + itos(p_peer) + "`.");
	}
	const NS::ObjectData *nd = get_object_data(pd->controller_id, p_expected);
	if (nd) {
		return nd->get_controller();
	}
	return nullptr;
}

const std::map<int, NS::PeerData> &SceneSynchronizerBase::get_peers() const {
	return peer_data;
}

std::map<int, NS::PeerData> &SceneSynchronizerBase::get_peers() {
	return peer_data;
}

NS::PeerData *SceneSynchronizerBase::get_peer_data_for_controller(const NetworkedControllerBase &p_controller, bool p_expected) {
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

const NS::PeerData *SceneSynchronizerBase::get_peer_data_for_controller(const NetworkedControllerBase &p_controller, bool p_expected) const {
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
	NS_PROFILE

	for (VarId var_id = { 0 }; var_id < VarId{ uint32_t(p_object_data->vars.size()) }; var_id += 1) {
		if (p_object_data->vars[var_id.id].enabled == false) {
			continue;
		}

		const VarData &old_val = p_object_data->vars[var_id.id].var.value;
		VarData new_val;
		{
			NS_PROFILE_NAMED("get_variable")
			synchronizer_manager->get_variable(
					p_object_data->app_object_handle,
					p_object_data->vars[var_id.id].var.name.c_str(),
					new_val);
		}

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
	time_bank = 0.0;
	enabled = true;
	frame_count = 0;
}

void NoNetSynchronizer::process(double p_delta) {
	if make_unlikely (enabled == false) {
		return;
	}

	const int sub_process_count = fetch_sub_processes_count(p_delta);
	for (int i = 0; i < sub_process_count; i++) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "NoNetSynchronizer::process", true);

		const uint32_t frame_index = frame_count;
		frame_count += 1;

		SceneSynchronizerDebugger::singleton()->scene_sync_process_start(scene_synchronizer);

		// Process the scene.
		scene_synchronizer->process_functions__execute();
		scene_synchronizer->detect_and_signal_changed_variables(NetEventFlag::CHANGE);

		SceneSynchronizerDebugger::singleton()->scene_sync_process_end(scene_synchronizer);
		SceneSynchronizerDebugger::singleton()->write_dump(0, frame_index);
		SceneSynchronizerDebugger::singleton()->start_new_frame();
	}
}

void NoNetSynchronizer::on_object_data_added(NS::ObjectData *p_object_data) {
	NS::VecFunc::insert_unique(active_objects, p_object_data);
}

void NoNetSynchronizer::on_object_data_removed(NS::ObjectData &p_object_data) {
	NS::VecFunc::remove_unordered(active_objects, &p_object_data);
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

int NoNetSynchronizer::fetch_sub_processes_count(double p_delta) {
	time_bank += p_delta;
	const double sub_frames = std::floor(time_bank * static_cast<double>(scene_synchronizer->get_frames_per_seconds()));
	time_bank -= sub_frames / static_cast<double>(scene_synchronizer->get_frames_per_seconds());
	// Clamp the maximum possible frames that we can process on a single frame.
	// This is a guard to make sure we do not process way too many frames on a single frame.
	return std::min(static_cast<int>(scene_synchronizer->get_max_sub_process_per_frame()), static_cast<int>(sub_frames));
}

ServerSynchronizer::ServerSynchronizer(SceneSynchronizerBase *p_node) :
		Synchronizer(p_node) {
	CRASH_COND(NS::SyncGroupId::GLOBAL != sync_group_create());
}

void ServerSynchronizer::clear() {
	time_bank = 0.0;
	objects_relevancy_update_timer = 0.0;
	// Release the internal memory.
	sync_groups.clear();
}

void ServerSynchronizer::process(double p_delta) {
	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "ServerSynchronizer::process", true);

	scene_synchronizer->update_peers();

	if (objects_relevancy_update_timer >= scene_synchronizer->objects_relevancy_update_time) {
		scene_synchronizer->update_objects_relevancy();
		objects_relevancy_update_timer = 0.0;
	} else {
		objects_relevancy_update_timer += p_delta;
	}

	SceneSynchronizerDebugger::singleton()->scene_sync_process_start(scene_synchronizer);

	const int sub_process_count = fetch_sub_processes_count(p_delta);
	for (int i = 0; i < sub_process_count; i++) {
		epoch += 1;

		// Process the scene
		scene_synchronizer->process_functions__execute();
		scene_synchronizer->detect_and_signal_changed_variables(NetEventFlag::CHANGE);

		process_snapshot_notificator();
	}

	process_trickled_sync(p_delta);
	process_latency_update();
	process_adjust_clients_controller_tick_rate(p_delta);

	SceneSynchronizerDebugger::singleton()->scene_sync_process_end(scene_synchronizer);

#if DEBUG_ENABLED
	// Write the debug dump for each peer.
	for (auto &peer_it : scene_synchronizer->peer_data) {
		if make_unlikely (peer_it.second.controller_id == ObjectNetId::NONE) {
			continue;
		}

		const NS::ObjectData *nd = scene_synchronizer->get_object_data(peer_it.second.controller_id);
		const FrameIndex current_input_id = nd->get_controller()->get_server_controller()->get_current_frame_index();
		SceneSynchronizerDebugger::singleton()->write_dump(peer_it.first, current_input_id.id);
	}
	SceneSynchronizerDebugger::singleton()->start_new_frame();
#endif
}

void ServerSynchronizer::on_peer_connected(int p_peer_id) {
	MapFunc::assign(peers_data, p_peer_id, PeerServerData());
	sync_group_move_peer_to(p_peer_id, SyncGroupId::GLOBAL);
}

void ServerSynchronizer::on_peer_disconnected(int p_peer_id) {
	peers_data.erase(p_peer_id);
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

	NS::VecFunc::insert_unique(active_objects, p_object_data);

	sync_groups[SyncGroupId::GLOBAL.id].add_new_sync_object(p_object_data, true);

	if (p_object_data->get_controller()) {
		// The added `ObjectData` is a controller, so mark the peer as needing
		// a full snapshot.
		const int peer = p_object_data->get_controller()->get_network_interface().get_unit_authority();
		std::map<int, PeerServerData>::iterator it = MapFunc::insert_if_new(peers_data, peer, PeerServerData());
		it->second.force_notify_snapshot = true;
		it->second.need_full_snapshot = true;
	}
}

void ServerSynchronizer::on_object_data_removed(NS::ObjectData &p_object_data) {
	NS::VecFunc::remove_unordered(active_objects, &p_object_data);

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

void ServerSynchronizer::notify_need_full_snapshot(int p_peer) {
	NS::PeerServerData *psd = MapFunc::get_or_null(peers_data, p_peer);
	ENSURE(psd);
	psd->need_full_snapshot = true;
}

SyncGroupId ServerSynchronizer::sync_group_create() {
	SyncGroupId id;
	id.id = sync_groups.size();
	sync_groups.resize(id.id + 1);
	return id;
}

const NS::SyncGroup *ServerSynchronizer::sync_group_get(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(p_group_id.id >= sync_groups.size(), nullptr, "The group id `" + itos(p_group_id.id) + "` doesn't exist.");
	return &sync_groups[p_group_id.id];
}

void ServerSynchronizer::sync_group_add_object(NS::ObjectData *p_object_data, SyncGroupId p_group_id, bool p_realtime) {
	ERR_FAIL_COND(p_object_data == nullptr);
	ERR_FAIL_COND_MSG(p_group_id.id >= sync_groups.size(), "The group id `" + itos(p_group_id.id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SyncGroupId::GLOBAL, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id.id].add_new_sync_object(p_object_data, p_realtime);
}

void ServerSynchronizer::sync_group_remove_object(NS::ObjectData *p_object_data, SyncGroupId p_group_id) {
	ERR_FAIL_COND(p_object_data == nullptr);
	ERR_FAIL_COND_MSG(p_group_id.id >= sync_groups.size(), "The group id `" + itos(p_group_id.id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SyncGroupId::GLOBAL, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id.id].remove_sync_object(*p_object_data);
}

void ServerSynchronizer::sync_group_replace_object(SyncGroupId p_group_id, LocalVector<NS::SyncGroup::SimulatedObjectInfo> &&p_new_realtime_nodes, LocalVector<NS::SyncGroup::TrickledObjectInfo> &&p_new_trickled_nodes) {
	ERR_FAIL_COND_MSG(p_group_id.id >= sync_groups.size(), "The group id `" + itos(p_group_id.id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SyncGroupId::GLOBAL, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id.id].replace_objects(std::move(p_new_realtime_nodes), std::move(p_new_trickled_nodes));
}

void ServerSynchronizer::sync_group_remove_all_objects(SyncGroupId p_group_id) {
	ERR_FAIL_COND_MSG(p_group_id.id >= sync_groups.size(), "The group id `" + itos(p_group_id.id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SyncGroupId::GLOBAL, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id.id].remove_all_nodes();
}

void ServerSynchronizer::sync_group_move_peer_to(int p_peer_id, SyncGroupId p_group_id) {
	// Update the sync group id
	sync_group_update(p_peer_id);
}

void ServerSynchronizer::sync_group_update(int p_peer_id) {
	NS::PeerData *pd = MapFunc::get_or_null(scene_synchronizer->peer_data, p_peer_id);
	ASSERT_COND_MSG(pd, "The caller MUST make sure the peer server data exists before calling this function.");

	auto psd_it = MapFunc::insert_if_new(peers_data, p_peer_id, PeerServerData());

	// remove the peer from any sync_group.
	for (uint32_t i = 0; i < sync_groups.size(); ++i) {
		sync_groups[i].remove_listening_peer(p_peer_id);
	}

	if (pd->authority_data.sync_group_id == SyncGroupId::NONE || !pd->authority_data.enabled) {
		// This peer is not listening to anything.
		return;
	}

	ENSURE_MSG(pd->authority_data.sync_group_id.id < sync_groups.size(), "The group id `" + pd->authority_data.sync_group_id + "` doesn't exist.");
	sync_groups[pd->authority_data.sync_group_id.id].add_listening_peer(p_peer_id);

	// Also mark the peer as need full snapshot, as it's into a new group now.
	psd_it->second.force_notify_snapshot = true;
	psd_it->second.need_full_snapshot = true;

	// Make sure the controller is added into this group.
	NS::ObjectData *nd = scene_synchronizer->get_object_data(pd->controller_id, false);
	if (nd) {
		sync_group_add_object(nd, pd->authority_data.sync_group_id, true);
	}
}

const std::vector<int> *ServerSynchronizer::sync_group_get_listening_peers(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(p_group_id.id >= sync_groups.size(), nullptr, "The group id `" + itos(p_group_id.id) + "` doesn't exist.");
	return &sync_groups[p_group_id.id].get_listening_peers();
}

void ServerSynchronizer::set_peer_networking_enable(int p_peer, bool p_enable) {
	PeerData *pd = MapFunc::get_or_null(scene_synchronizer->peer_data, p_peer);
	ENSURE(pd);

	if (pd->authority_data.enabled == p_enable) {
		// Nothing to do.
		return;
	}

	pd->authority_data.enabled = p_enable;

	sync_group_update(p_peer);
}

void ServerSynchronizer::sync_group_set_trickled_update_rate(NS::ObjectData *p_object_data, SyncGroupId p_group_id, real_t p_update_rate) {
	ERR_FAIL_COND(p_object_data == nullptr);
	ERR_FAIL_COND_MSG(p_group_id.id >= sync_groups.size(), "The group id `" + itos(p_group_id.id) + "` doesn't exist.");
	ERR_FAIL_COND_MSG(p_group_id == SyncGroupId::GLOBAL, "You can't change this SyncGroup in any way. Create a new one.");
	sync_groups[p_group_id.id].set_trickled_update_rate(p_object_data, p_update_rate);
}

real_t ServerSynchronizer::sync_group_get_trickled_update_rate(const NS::ObjectData *p_object_data, SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V(p_object_data == nullptr, 0.0);
	ERR_FAIL_COND_V_MSG(p_group_id.id >= sync_groups.size(), 0.0, "The group id `" + itos(p_group_id.id) + "` doesn't exist.");
	ERR_FAIL_COND_V_MSG(p_group_id == SyncGroupId::GLOBAL, 0.0, "You can't change this SyncGroup in any way. Create a new one.");
	return sync_groups[p_group_id.id].get_trickled_update_rate(p_object_data);
}

void ServerSynchronizer::sync_group_set_user_data(SyncGroupId p_group_id, uint64_t p_user_data) {
	ERR_FAIL_COND_MSG(p_group_id.id >= sync_groups.size(), "The group id `" + itos(p_group_id.id) + "` doesn't exist.");
	sync_groups[p_group_id.id].user_data = p_user_data;
}

uint64_t ServerSynchronizer::sync_group_get_user_data(SyncGroupId p_group_id) const {
	ERR_FAIL_COND_V_MSG(p_group_id.id >= sync_groups.size(), 0, "The group id `" + itos(p_group_id.id) + "` doesn't exist.");
	return sync_groups[p_group_id.id].user_data;
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

// This function MUST be processed with a fixed delta time.
void ServerSynchronizer::process_snapshot_notificator() {
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
		group.state_notifier_timer += scene_synchronizer->get_fixed_frame_delta();
		const bool notify_state = group.state_notifier_timer >= scene_synchronizer->get_frame_confirmation_timespan();

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
			NS::PeerData *peer = MapFunc::get_or_null(scene_synchronizer->peer_data, peer_id);
			if (peer == nullptr) {
				SceneSynchronizerDebugger::singleton()->print(ERROR, "The `process_snapshot_notificator` failed to lookup the peer_id `" + std::to_string(peer_id) + "`. Was it removed but never cleared from sync_groups. Report this error, as this is a bug.");
				continue;
			}
			auto pd_it = MapFunc::insert_if_new(peers_data, peer_id, PeerServerData());

			if (pd_it->second.force_notify_snapshot == false && notify_state == false) {
				// Nothing to sync.
				continue;
			}

			pd_it->second.force_notify_snapshot = false;

			NS::ObjectData *controller_od = scene_synchronizer->get_object_data(peer->controller_id, false);

			// Fetch the peer input_id for this snapshot
			FrameIndex input_id = FrameIndex::NONE;
			if (controller_od) {
				ASSERT_COND_MSG(controller_od->get_controller(), "The NodeData fetched is not a controller: `" + controller_od->object_name + "`, this is not supposed to happen.");
				NetworkedControllerBase *controller = controller_od->get_controller();
				input_id = controller->get_current_frame_index();
			}

			DataBuffer *snap;
			if (pd_it->second.need_full_snapshot) {
				pd_it->second.need_full_snapshot = false;
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
			snap->add(input_id.id);

			scene_synchronizer->rpc_handler_state.rpc(
					scene_synchronizer->get_network_interface(),
					peer_id,
					*snap);
			scene_synchronizer->event_sent_snapshot.broadcast(input_id, peer_id);

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

	// Network the peers latency.
	for (int peer : p_group.get_peers_with_newly_calculated_latency()) {
		const PeerData *pd = NS::MapFunc::get_or_null(scene_synchronizer->peer_data, peer);
		if (pd) {
			r_snapshot_db.add(true);
			r_snapshot_db.add(peer);
			const std::uint8_t compressed_latency = pd->get_compressed_latency();
			r_snapshot_db.add(compressed_latency);
		}
	}
	r_snapshot_db.add(false);

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

#ifdef DEBUG_ENABLED
		if (scene_synchronizer->pedantic_checks) {
			// Make sure the value read from `var.var.value` equals to the one set on the scene.
			VarData current_val;
			scene_synchronizer->get_synchronizer_manager().get_variable(
					p_object_data->app_object_handle,
					var.var.name.c_str(),
					current_val);
			CRASH_COND(!scene_synchronizer->var_data_compare(current_val, var.var.value));
		}
#endif

		r_snapshot_db.add(var_has_value);
		if (var_has_value) {
			SceneSynchronizerBase::var_data_encode(r_snapshot_db, var.var.value);
		}
	}
}

void ServerSynchronizer::process_trickled_sync(double p_delta) {
	DataBuffer *tmp_buffer = memnew(DataBuffer);

	// Since the `update_rate` is a rate relative to the fixed_frame_delta,
	// we need to compute this factor to correctly scale the `update_rate`.
	const double current_frame_factor = p_delta / scene_synchronizer->get_fixed_frame_delta();

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
					SceneSynchronizerDebugger::singleton()->print(ERROR, "[FATAL] The `process_trickled_sync` found a node with ID `" + object_info.od->get_net_id() + "::" + object_info.od->object_name + "` that exceedes the max ID this function can network at the moment. Please report this, we will consider improving this function.", scene_synchronizer->get_network_interface().get_owner_name());
					continue;
				}

				// TODO use `DEBUG_ENABLED` here?
				if (!object_info.od->func_trickled_collect) {
					SceneSynchronizerDebugger::singleton()->print(ERROR, "The `process_trickled_sync` found a node `" + object_info.od->get_net_id() + "::" + object_info.od->object_name + "` with an invalid function `func_trickled_collect`. Please use `setup_deferred_sync` to correctly initialize this node for deferred sync.", scene_synchronizer->get_network_interface().get_owner_name());
					continue;
				}

				object_info._update_priority = 0.0;

				// Read the state and write into the tmp_buffer:
				tmp_buffer->begin_write(0);

				object_info.od->func_trickled_collect(*tmp_buffer, object_info.update_rate);
				if (tmp_buffer->total_size() > UINT16_MAX) {
					SceneSynchronizerDebugger::singleton()->print(ERROR, "The `process_trickled_sync` failed because the method `trickled_collect` for the node `" + object_info.od->get_net_id() + "::" + object_info.od->object_name + "` collected more than " + std::to_string(UINT16_MAX) + " bits. Please optimize your netcode to send less data.", scene_synchronizer->get_network_interface().get_owner_name());
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
				object_info._update_priority += object_info.update_rate * current_frame_factor;
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

void ServerSynchronizer::process_latency_update() {
	const std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();

	for (auto &[peer, peer_data] : scene_synchronizer->get_peers()) {
		std::map<int, PeerServerData>::iterator peer_server_data_it = NS::MapFunc::insert_if_new(peers_data, peer, PeerServerData());
		if (peer_server_data_it->second.latency_calculation_in_progress) {
			continue;
		}
		const auto interval = std::chrono::duration_cast<std::chrono::milliseconds>(now - peer_server_data_it->second.latency_ping_timestamp);
		if (interval.count() >= (scene_synchronizer->latency_update_rate * 1000.0)) {
			scene_synchronizer->rpc_handler_latency.rpc(
					scene_synchronizer->get_network_interface(),
					peer);
			peer_server_data_it->second.latency_ping_timestamp = now;
			peer_server_data_it->second.latency_calculation_in_progress = true;
		}
	}
}

void ServerSynchronizer::notify_latency_received(int p_peer) {
	const std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();

	std::map<int, PeerData>::iterator pd_it = NS::MapFunc::insert_if_new(scene_synchronizer->peer_data, p_peer, PeerData());
	std::map<int, PeerServerData>::iterator psd_it = NS::MapFunc::insert_if_new(peers_data, p_peer, PeerServerData());

	const auto rtt_interval = std::chrono::duration_cast<std::chrono::milliseconds>(now - psd_it->second.latency_ping_timestamp);
	// Clamlatency to 1k as 1k ms latency is way too high to matter anyway.
	const std::uint64_t rtt = rtt_interval.count();
	pd_it->second.set_latency(rtt);
	psd_it->second.latency_calculation_in_progress = false;
	psd_it->second.latency_ping_timestamp = now;

	// Notify all sync groups about this peer having newly calculated latency.
	for (auto &group : sync_groups) {
		group.notify_peer_has_newly_calculated_latency(p_peer);
	}
}

void ServerSynchronizer::process_adjust_clients_controller_tick_rate(double p_delta) {
	for (auto &it : scene_synchronizer->peer_data) {
		const int peer = it.first;
		const PeerData &peer_data = it.second;
		ObjectData *object_data = scene_synchronizer->get_object_data(peer_data.controller_id);
		if (object_data) {
			if (object_data->get_controller()) {
				process_adjust_client_controller_tick_rate(
						p_delta,
						peer,
						*object_data->get_controller());
			}
		}
	}
}

void ServerSynchronizer::process_adjust_client_controller_tick_rate(double p_delta, int p_controller_peer, NetworkedControllerBase &p_controller) {
	CRASH_COND(!p_controller.is_server_controller());

	if (!p_controller.get_server_controller_unchecked()->streaming_paused) {
		return;
	}

	p_controller.get_server_controller_unchecked()->additional_fps_notif_timer += p_delta;
	if (p_controller.get_server_controller_unchecked()->additional_fps_notif_timer < scene_synchronizer->get_tick_speedup_notification_delay()) {
		return;
	}

	// Time to tell the client a new speedup.
	p_controller.get_server_controller_unchecked()->additional_fps_notif_timer = 0;

	const std::int8_t distance_to_optimal = p_controller.get_server_controller_unchecked()->compute_client_tick_rate_distance_to_optimal();

	std::uint8_t compressed_distance;
	memcpy(
			&compressed_distance,
			&distance_to_optimal,
			sizeof(uint8_t));

	Vector<uint8_t> packet_data;
	packet_data.push_back(compressed_distance);

	scene_synchronizer->rpc_handle_notify_fps_acceleration.rpc(
			scene_synchronizer->get_network_interface(),
			p_controller_peer,
			packet_data);
}

int ServerSynchronizer::fetch_sub_processes_count(double p_delta) {
	time_bank += p_delta;
	const double sub_frames = std::floor(time_bank * static_cast<double>(scene_synchronizer->get_frames_per_seconds()));
	time_bank -= sub_frames / static_cast<double>(scene_synchronizer->get_frames_per_seconds());
	// Clamp the maximum possible frames that we can process on a single frame.
	// This is a guard to make sure we do not process way too many frames on a single frame.
	return std::min(static_cast<int>(scene_synchronizer->get_max_sub_process_per_frame()), static_cast<int>(sub_frames));
}

ClientSynchronizer::ClientSynchronizer(SceneSynchronizerBase *p_node) :
		Synchronizer(p_node) {
	clear();

	notify_server_full_snapshot_is_needed();
}

void ClientSynchronizer::clear() {
	player_controller_object_data = nullptr;
	objects_names.clear();
	last_received_snapshot.input_id = FrameIndex::NONE;
	last_received_snapshot.object_vars.clear();
	client_snapshots.clear();
	last_received_server_snapshot_index = FrameIndex::NONE;
	last_received_server_snapshot.reset();
	last_checked_input = { 0 };
	enabled = true;
	need_full_snapshot_notified = false;
}

void ClientSynchronizer::process(double p_delta) {
	NS_PROFILE

	SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "ClientSynchronizer::process", true);

#ifdef DEBUG_ENABLED
	if make_unlikely (p_delta > (scene_synchronizer->get_fixed_frame_delta() + (scene_synchronizer->get_fixed_frame_delta() * 0.2))) {
		const bool silent = !ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/debugger/log_debug_fps_warnings");
		SceneSynchronizerDebugger::singleton()->debug_warning(&scene_synchronizer->get_network_interface(), "Current FPS is " + itos(Engine::get_singleton()->get_frames_per_second()) + ", but the minimum required FPS is " + itos(scene_synchronizer->get_frames_per_seconds()) + ", the client is unable to generate enough inputs for the server.", silent);
	}
#endif

	process_server_sync();
	process_simulation(p_delta);
	process_trickled_sync(p_delta);

#if DEBUG_ENABLED
	if (player_controller_object_data) {
		NetworkedControllerBase *controller = player_controller_object_data->get_controller();
		PlayerController *player_controller = controller->get_player_controller();
		const int client_peer = scene_synchronizer->network_interface->fetch_local_peer_id();
		SceneSynchronizerDebugger::singleton()->write_dump(client_peer, player_controller->get_current_frame_index().id);
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
	store_controllers_snapshot(last_received_snapshot);
}

void ClientSynchronizer::on_object_data_added(NS::ObjectData *p_object_data) {
}

void ClientSynchronizer::on_object_data_removed(NS::ObjectData &p_object_data) {
	if (player_controller_object_data == &p_object_data) {
		player_controller_object_data = nullptr;
		last_received_server_snapshot.reset();
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
	NS_PROFILE

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
		last_received_server_snapshot_index = FrameIndex::NONE;
		last_received_server_snapshot.reset();
		client_snapshots.clear();
	}

	if (p_object_data->get_controller()->is_player_controller()) {
		if (player_controller_object_data != nullptr) {
			SceneSynchronizerDebugger::singleton()->print(ERROR, "Only one player controller is supported, at the moment. Make sure this is the case.", scene_synchronizer->get_network_interface().get_owner_name());
		} else {
			// Set this player controller as active.
			player_controller_object_data = p_object_data;
			last_received_server_snapshot_index = FrameIndex::NONE;
			last_received_server_snapshot.reset();
			client_snapshots.clear();
		}
	}
}

const std::vector<ObjectData *> &ClientSynchronizer::get_active_objects() const {
	if make_likely (player_controller_object_data && enabled) {
		return active_objects;
	} else {
		// Since there is no player controller or the sync is disabled, this
		// assumes that all registered objects are relevant and simulated.
		return scene_synchronizer->get_all_object_data();
	}
}

void ClientSynchronizer::store_snapshot() {
	NS_PROFILE

	NetworkedControllerBase *controller = player_controller_object_data->get_controller();

#ifdef DEBUG_ENABLED
	if make_unlikely (client_snapshots.size() > 0 && controller->get_current_frame_index() <= client_snapshots.back().input_id) {
		ASSERT_NO_ENTRY_MSG("During snapshot creation, for controller " + player_controller_object_data->object_name + ", was found an ID for an older snapshots. New input ID: " + std::string(controller->get_current_frame_index()) + " Last saved snapshot input ID: " + std::string(client_snapshots.back().input_id) + ".");
	}
#endif

	client_snapshots.push_back(NS::Snapshot());

	NS::Snapshot &snap = client_snapshots.back();
	snap.input_id = controller->get_current_frame_index();

	update_client_snapshot(snap);
}

void ClientSynchronizer::store_controllers_snapshot(
		const NS::Snapshot &p_snapshot) {
	// Put the parsed snapshot into the queue.

	if (p_snapshot.input_id == FrameIndex::NONE) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The Client received the server snapshot WITHOUT `input_id`.", true);
		// The controller node is not registered so just assume this snapshot is the most up-to-date.
		last_received_server_snapshot.emplace(Snapshot::make_copy(p_snapshot));
		last_received_server_snapshot_index = p_snapshot.input_id;

	} else {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The Client received the server snapshot: " + uitos(p_snapshot.input_id.id), true);
		ENSURE_MSG(
				last_received_server_snapshot_index == FrameIndex::NONE ||
						last_received_server_snapshot_index <= p_snapshot.input_id,
				"The client received a too old snapshot. If this happens back to back for a long period it's a bug, otherwise can be ignored. last_received_server_snapshot_index: " + std::to_string(last_received_server_snapshot_index.id) + " p_snapshot.input_id: " + std::to_string(p_snapshot.input_id.id));
		last_received_server_snapshot.emplace(Snapshot::make_copy(p_snapshot));
		last_received_server_snapshot_index = p_snapshot.input_id;
	}

	CRASH_COND(last_received_server_snapshot_index != p_snapshot.input_id);
}

void ClientSynchronizer::process_server_sync() {
	NS_PROFILE
	process_received_server_state();

	// Now trigger the END_SYNC event.
	signal_end_sync_changed_variables_events();
}

void ClientSynchronizer::process_received_server_state() {
	NS_PROFILE

	// --- Phase one: find the snapshot to check. ---
	if (!last_received_server_snapshot) {
		// No snapshots to recover for this controller. Nothing to do.
		return;
	}

	if (last_received_server_snapshot->input_id == FrameIndex::NONE) {
		// The server last received snapshot is a no input snapshot. Just assume it's the most up-to-date.
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The client received a \"no input\" snapshot, so the client is setting it right away assuming is the most updated one.", true);

		apply_snapshot(*last_received_server_snapshot, NetEventFlag::SYNC_RECOVER, nullptr);
		last_received_server_snapshot.reset();
		return;
	}

	ENSURE_MSG(player_controller_object_data, "There is no player controller and the only allowed snapshot are the one with `FrameIndex` set to NONE. The current one is set to " + last_received_server_snapshot->input_id + " so it's ignored.");

	NetworkedControllerBase *controller = player_controller_object_data->get_controller();
	PlayerController *player_controller = controller->get_player_controller();

#ifdef DEBUG_ENABLED
	if (client_snapshots.empty() == false) {
		// The SceneSynchronizer and the PlayerController are always in sync.
		CRASH_COND_MSG(client_snapshots.back().input_id != player_controller->last_known_frame_index(), "This should not be possible: snapshot input: " + uitos(client_snapshots.back().input_id.id) + " last_know_input: " + uitos(player_controller->last_known_frame_index().id));
	}
#endif

	if (client_snapshots.empty()) {
		// No client input, this happens when the stream is paused.
		process_paused_controller_recovery();
		scene_synchronizer->event_state_validated.broadcast(last_checked_input, false);
		// Clear the server snapshot.
		last_received_server_snapshot.reset();
		return;
	}

	// Find the best recoverable input_id.
	last_checked_input = last_received_server_snapshot->input_id;

	// Drop all the old client snapshots until the one that we need.
	while (client_snapshots.front().input_id < last_checked_input) {
		client_snapshots.pop_front();
	}

#ifdef DEBUG_ENABLED
	// This can't be triggered because this case is already handled above.
	CRASH_COND(last_checked_input == FrameIndex::NONE);
	if (!client_snapshots.empty()) {
		// This can't be triggered because the client accepts snapshots that are
		// newer (or at least the same) of the last checked one.
		// The client keep all the unprocessed snapshots.
		// NOTE: the -1 check is needed for the cases when the same snapshot is
		//       processed twice (in that case the input_id is already cleared).
		CRASH_COND(client_snapshots.front().input_id != last_checked_input && (client_snapshots.front().input_id - 1) != last_checked_input);
	}
#endif

	bool need_rewind;
	NS::Snapshot no_rewind_recover;
	if make_likely (!client_snapshots.empty() && client_snapshots.front().input_id == last_checked_input) {
		// In this case the client is checking the frame for the first time, and
		// this is the most common case.

		need_rewind = __pcr__fetch_recovery_info(
				last_checked_input,
				no_rewind_recover);

		// Popout the client snapshot.
		client_snapshots.pop_front();
	} else {
		// This case is less likely to happen, and in this case the client
		// received the same frame (from the server) twice, so just assume we
		// need a rewind.
		need_rewind = true;
	}

	scene_synchronizer->event_state_validated.broadcast(last_checked_input, need_rewind);

	// --- Phase three: recover and rewind. ---

	if (need_rewind) {
		SceneSynchronizerDebugger::singleton()->notify_event(SceneSynchronizerDebugger::FrameEvent::CLIENT_DESYNC_DETECTED);
		SceneSynchronizerDebugger::singleton()->print(
				__INTERNAL,
				std::string("Recover input: ") + std::string(last_checked_input) + " - Last input: " + std::string(player_controller->get_stored_frame_index(-1)),
				scene_synchronizer->get_network_interface().get_owner_name());

		// Sync.
		__pcr__sync__rewind();

		// Rewind.
		__pcr__rewind(
				last_checked_input,
				player_controller_object_data,
				controller,
				player_controller);
	} else {
		if (no_rewind_recover.input_id == FrameIndex{ 0 }) {
			SceneSynchronizerDebugger::singleton()->notify_event(SceneSynchronizerDebugger::FrameEvent::CLIENT_DESYNC_DETECTED_SOFT);

			// Sync.
			__pcr__sync__no_rewind(no_rewind_recover);
		}

		// No rewind.
		__pcr__no_rewind(last_checked_input, player_controller);
	}

	// Clear the server snapshot.
	last_received_server_snapshot.reset();
}

bool ClientSynchronizer::__pcr__fetch_recovery_info(
		const FrameIndex p_input_id,
		NS::Snapshot &r_no_rewind_recover) {
	NS_PROFILE
	std::vector<std::string> differences_info;

#ifdef DEBUG_ENABLED
	std::vector<ObjectNetId> different_node_data;
	const bool is_equal = NS::Snapshot::compare(
			*scene_synchronizer,
			*last_received_server_snapshot,
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

			const std::vector<NS::NameAndVar> *server_node_vars = ObjectNetId{ uint32_t(last_received_server_snapshot->object_vars.size()) } <= net_node_id ? nullptr : &(last_received_server_snapshot->object_vars[net_node_id.id]);
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

			scene_synchronizer->event_desync_detected_with_info.broadcast(p_input_id, rew_node_data->app_object_handle, variable_names, client_values, server_values);
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
		SceneSynchronizerDebugger::singleton()->print(INFO, "Rewind on frame " + p_input_id + " is needed because:", scene_synchronizer->get_network_interface().get_owner_name());
		for (int i = 0; i < int(differences_info.size()); i++) {
			SceneSynchronizerDebugger::singleton()->print(INFO, "|- " + differences_info[i], scene_synchronizer->get_network_interface().get_owner_name());
		}
	}

	return !is_equal;
}

void ClientSynchronizer::__pcr__sync__rewind() {
	NS_PROFILE
	// Apply the server snapshot so to go back in time till that moment,
	// so to be able to correctly reply the movements.

	std::vector<std::string> applied_data_info;

	const NS::Snapshot &server_snapshot = *last_received_server_snapshot;
	apply_snapshot(
			server_snapshot,
			NetEventFlag::SYNC_RECOVER | NetEventFlag::SYNC_RESET,
			scene_synchronizer->debug_rewindings_enabled ? &applied_data_info : nullptr);

	if (applied_data_info.size() > 0) {
		SceneSynchronizerDebugger::singleton()->print(INFO, "Full reset:", scene_synchronizer->get_network_interface().get_owner_name());
		for (int i = 0; i < int(applied_data_info.size()); i++) {
			SceneSynchronizerDebugger::singleton()->print(INFO, "|- " + applied_data_info[i], scene_synchronizer->get_network_interface().get_owner_name());
		}
	}
}

void ClientSynchronizer::__pcr__rewind(
		const FrameIndex p_checkable_frame_index,
		NS::ObjectData *p_local_controller_node,
		NetworkedControllerBase *p_local_controller,
		PlayerController *p_local_player_controller) {
	NS_PROFILE
	const int frames_to_rewind = p_local_player_controller->get_frames_count();

#ifdef DEBUG_ENABLED
	// Unreachable because the SceneSynchronizer and the PlayerController
	// have the same stored data at this point: thanks to the `event_state_validated`
	// the NetController clears its stored frames.
	CRASH_COND_MSG(client_snapshots.size() != size_t(frames_to_rewind), "Beware that `client_snapshots.size()` (" + itos(client_snapshots.size()) + ") and `remaining_inputs` (" + itos(frames_to_rewind) + ") should be the same.");
#endif

#ifdef DEBUG_ENABLED
	// Used to double check all the instants have been processed.
	bool has_next = false;
#endif
	for (int i = 0; i < frames_to_rewind; i += 1) {
		const FrameIndex frame_id_to_process = p_local_player_controller->get_stored_frame_index(i);
#ifdef NS_PROFILING_ENABLED
		std::string prof_info = "Index: " + std::to_string(i) + " Frame ID: " + std::to_string(frame_id_to_process.id);
		NS_PROFILE_NAMED_WITH_INFO("Rewinding frame", prof_info);
#endif

		scene_synchronizer->change_events_begin(NetEventFlag::SYNC_RECOVER | NetEventFlag::SYNC_REWIND);

		// Step 1 -- Notify the local controller about the instant to process
		//           on the next process.
		scene_synchronizer->event_rewind_frame_begin.broadcast(frame_id_to_process, i, frames_to_rewind);
#ifdef DEBUG_ENABLED
		has_next = p_local_controller->has_another_instant_to_process_after(i);
		SceneSynchronizerDebugger::singleton()->print(
				INFO,
				"Rewind, processed controller: " + p_local_controller_node->object_name + " Frame: " + std::string(frame_id_to_process),
				scene_synchronizer->get_network_interface().get_owner_name(),
				scene_synchronizer->debug_rewindings_enabled);
#endif

		// Step 2 -- Process the scene.
		{
			NS_PROFILE_NAMED("process_functions__execute");
			scene_synchronizer->process_functions__execute();
		}

		// Step 3 -- Pull node changes.
		{
			NS_PROFILE_NAMED("detect_and_signal_changed_variables");
			scene_synchronizer->detect_and_signal_changed_variables(NetEventFlag::SYNC_RECOVER | NetEventFlag::SYNC_REWIND);
		}

		// Step 4 -- Update snapshots.
		{
			NS_PROFILE_NAMED("update_client_snapshot");
			update_client_snapshot(client_snapshots[i]);
		}
	}

#ifdef DEBUG_ENABLED
	// Unreachable because the above loop consume all instants, so the last
	// process will set this to false.
	CRASH_COND(has_next);
#endif
}

void ClientSynchronizer::__pcr__sync__no_rewind(const NS::Snapshot &p_no_rewind_recover) {
	NS_PROFILE
	CRASH_COND_MSG(p_no_rewind_recover.input_id != FrameIndex{ 0 }, "This function is never called unless there is something to recover without rewinding.");

	// Apply found differences without rewind.
	std::vector<std::string> applied_data_info;

	apply_snapshot(
			p_no_rewind_recover,
			NetEventFlag::SYNC_RECOVER,
			scene_synchronizer->debug_rewindings_enabled ? &applied_data_info : nullptr,
			// ALWAYS skips custom data because partial snapshots don't contain custom_data.
			true);

	if (applied_data_info.size() > 0) {
		SceneSynchronizerDebugger::singleton()->print(INFO, "Partial reset:", scene_synchronizer->get_network_interface().get_owner_name());
		for (int i = 0; i < int(applied_data_info.size()); i++) {
			SceneSynchronizerDebugger::singleton()->print(INFO, "|- " + applied_data_info[i], scene_synchronizer->get_network_interface().get_owner_name());
		}
	}

	// Update the last client snapshot.
	if (!client_snapshots.empty()) {
		update_client_snapshot(client_snapshots.back());
	}
}

void ClientSynchronizer::__pcr__no_rewind(
		const FrameIndex p_checkable_input_id,
		PlayerController *p_player_controller) {
	NS_PROFILE
}

void ClientSynchronizer::process_paused_controller_recovery() {
	NS_PROFILE

#ifdef DEBUG_ENABLED
	CRASH_COND(!last_received_server_snapshot);
	CRASH_COND(client_snapshots.empty() == false);
#endif

	std::vector<std::string> applied_data_info;

	apply_snapshot(
			*last_received_server_snapshot,
			NetEventFlag::SYNC_RECOVER,
			&applied_data_info);

	last_received_server_snapshot.reset();

	if (applied_data_info.size() > 0) {
		SceneSynchronizerDebugger::singleton()->print(INFO, "Paused controller recover:", scene_synchronizer->get_network_interface().get_owner_name());
		for (int i = 0; i < int(applied_data_info.size()); i++) {
			SceneSynchronizerDebugger::singleton()->print(INFO, "|- " + applied_data_info[i], scene_synchronizer->get_network_interface().get_owner_name());
		}
	}
}

int ClientSynchronizer::calculates_sub_ticks(const double p_delta) {
	const double frames_per_seconds = 1.0 / p_delta;
	// Extract the frame acceleration:
	// 1. convert the Accelerated Tick Hz to second.
	const double fully_accelerated_delta = 1.0 / (frames_per_seconds + acceleration_fps_speed);

	// 2. Subtract the `accelerated_delta - delta` to obtain the acceleration magnitude.
	const double acceleration_delta = std::abs(fully_accelerated_delta - p_delta);

	// 3. Avoids overshots by taking the smallest value between `acceleration_delta` and the `remaining timer`.
	const double frame_acceleration_delta = std::max(0.0, std::min(acceleration_delta, acceleration_fps_timer));

	// Updates the timer by removing the extra accelration.
	acceleration_fps_timer = std::max(acceleration_fps_timer - frame_acceleration_delta, 0.0);

	// Calculates the pretended delta.
	pretended_delta = p_delta + (frame_acceleration_delta * NS::sign(acceleration_fps_speed));

	// Add the current delta to the bank
	time_bank += pretended_delta;

	const int sub_ticks = std::floor(time_bank * static_cast<double>(scene_synchronizer->get_frames_per_seconds()));

	time_bank -= static_cast<double>(sub_ticks) / static_cast<double>(scene_synchronizer->get_frames_per_seconds());
	if make_unlikely (time_bank < 0.0) {
		time_bank = 0.0;
	}

	ENSURE_V_MSG(
			sub_ticks <= scene_synchronizer->get_max_sub_process_per_frame(),
			scene_synchronizer->get_max_sub_process_per_frame(),
			"This client generated a sub tick count of `" + std::to_string(sub_ticks) + "` that is higher than the `max_sub_process_per_frame` specified of `" + std::to_string(scene_synchronizer->get_max_sub_process_per_frame()) + "`. If the number is way too high (like 100 or 1k) it's a bug in the algorithm that you should notify, if it's just above the threshould you set, make sure the threshold is correctly set or ignore it if the client perfs are too poor." +
					" (in delta: " + std::to_string(p_delta) +
					" iteration per seconds: " + std::to_string(scene_synchronizer->get_frames_per_seconds()) +
					" fully_accelerated_delta: " + std::to_string(fully_accelerated_delta) +
					" acceleration_delta: " + std::to_string(acceleration_delta) +
					" frame_acceleration_delta: " + std::to_string(frame_acceleration_delta) +
					" acceleration_fps_speed: " + std::to_string(acceleration_fps_speed) +
					" acceleration_fps_timer: " + std::to_string(acceleration_fps_timer) +
					" pretended_delta: " + std::to_string(pretended_delta) +
					" time_bank: " + std::to_string(time_bank) +
					")");

	return sub_ticks;
}

void ClientSynchronizer::process_simulation(double p_delta) {
	NS_PROFILE

	if make_unlikely (player_controller_object_data == nullptr || enabled == false) {
		// No player controller so can't process the simulation.
		// TODO Remove this constraint?

		// Make sure to fetch changed variable anyway.
		scene_synchronizer->detect_and_signal_changed_variables(NetEventFlag::CHANGE);
		return;
	}

	NetworkedControllerBase *controller = player_controller_object_data->get_controller();
	PlayerController *player_controller = controller->get_player_controller();

	// Due to some lag we may want to speed up the input_packet
	// generation, for this reason here I'm performing a sub tick.
	//
	// keep in mind that we are just pretending that the time
	// is advancing faster, for this reason we are still using
	// `delta` to step the controllers_node_data.
	//
	// The dolls may want to speed up too, so to consume the inputs faster
	// and get back in time with the server.
	int sub_ticks = calculates_sub_ticks(p_delta);
#ifdef NS_PROFILING_ENABLED
	std::string perf_info = "In delta: " + std::to_string(p_delta) + " sub ticks: " + std::to_string(sub_ticks) + " net frames per seconds: " + std::to_string(scene_synchronizer->get_frames_per_seconds());
	NS_PROFILE_SET_INFO(perf_info);
#endif

	if (sub_ticks == 0) {
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "No sub ticks: this is not bu a bug; it's the lag compensation algorithm.", true);
	}

	while (sub_ticks > 0) {
#ifdef NS_PROFILING_ENABLED
		std::string sub_perf_info = "Fixed delta: " + std::to_string(scene_synchronizer->get_fixed_frame_delta()) + " remaining ticks: " + std::to_string(sub_ticks);
		NS_PROFILE_NAMED_WITH_INFO("PROCESS", sub_perf_info)
#endif
		SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "ClientSynchronizer::process::sub_process " + itos(sub_ticks), true);
		SceneSynchronizerDebugger::singleton()->scene_sync_process_start(scene_synchronizer);

		// Process the scene.
		scene_synchronizer->process_functions__execute();

		scene_synchronizer->detect_and_signal_changed_variables(NetEventFlag::CHANGE);

		if (controller->player_has_new_input()) {
			store_snapshot();
		}

		sub_ticks -= 1;
		SceneSynchronizerDebugger::singleton()->scene_sync_process_end(scene_synchronizer);

#if DEBUG_ENABLED
		if (sub_ticks > 0) {
			// This is an intermediate sub tick, so store the dumlatency.
			// The last sub frame is not dumped, untile the end of the frame, so we can capture any subsequent message.
			const int client_peer = scene_synchronizer->network_interface->fetch_local_peer_id();
			SceneSynchronizerDebugger::singleton()->write_dump(client_peer, player_controller->get_current_frame_index().id);
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
		void (*p_input_id_parse)(void *p_user_pointer, FrameIndex p_input_id),
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
		FrameIndex input_id;
		p_snapshot.read(input_id.id);
		ERR_FAIL_COND_V_MSG(p_snapshot.is_buffer_failed(), false, "This snapshot is corrupted as the `InputID` expected is not set.");
		p_input_id_parse(p_user_pointer, input_id);

		// Fetch `active_node_list_byte_array`.
		bool has_active_list_array;
		p_snapshot.read(has_active_list_array);
		ERR_FAIL_COND_V_MSG(p_snapshot.is_buffer_failed(), false, "This snapshot is corrupted as the `has_active_list_array` boolean expected is not set.");
		if (has_active_list_array) {
			std::vector<ObjectNetId> sd_simulated_objects;
			sd_simulated_objects.reserve(scene_synchronizer->get_all_object_data().size());

			// Fetch the array.
			while (true) {
				ObjectNetId id;
				p_snapshot.read(id.id);
				ERR_FAIL_COND_V_MSG(p_snapshot.is_buffer_failed(), false, "This snapshot is corrupted as fetching `ObjectNetId` failed.");

				if (id == ObjectNetId::NONE) {
					// The end.
					break;
				}
				sd_simulated_objects.push_back(id);
			}

			p_simulated_objects_parse(p_user_pointer, std::move(sd_simulated_objects));
		}
	}

	{
		// Fetch latencys
		while (true) {
			bool has_next_latency = false;
			p_snapshot.read(has_next_latency);
			ENSURE_V_MSG(!p_snapshot.is_buffer_failed(), false, "This snapshot is corrupted as fetching `has_next_latency` failed.");
			if (has_next_latency) {
				int peer;
				p_snapshot.read(peer);
				ENSURE_V_MSG(!p_snapshot.is_buffer_failed(), false, "This snapshot is corrupted as fetching `peer` failed.");
				std::uint8_t compressed_latency;
				p_snapshot.read(compressed_latency);
				ENSURE_V_MSG(!p_snapshot.is_buffer_failed(), false, "This snapshot is corrupted as fetching `compressed_latency` failed.");
				std::map<int, PeerData>::iterator peer_data_it = NS::MapFunc::insert_if_new(scene_synchronizer->peer_data, peer, PeerData());
				peer_data_it->second.set_compressed_latency(compressed_latency);
			} else {
				break;
			}
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
					const std::string *object_name_ptr = NS::MapFunc::get_or_null(objects_names, net_id);

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
					SceneSynchronizerDebugger::singleton()->print(WARNING, "The object " + object_name + " still doesn't exist.", scene_synchronizer->get_network_interface().get_owner_name());
				} else {
					// Register this object, so to make sure the client is tracking it.
					ObjectLocalId reg_obj_id;
					scene_synchronizer->register_app_object(app_object_handle, &reg_obj_id);
					if (reg_obj_id != ObjectLocalId::NONE) {
						synchronizer_object_data = scene_synchronizer->get_object_data(reg_obj_id);
						// Set the NetId.
						synchronizer_object_data->set_net_id(net_id);
					} else {
						SceneSynchronizerDebugger::singleton()->print(ERROR, "[BUG] This object " + object_name + " was known on this client. Though, was not possible to register it as sync object.", scene_synchronizer->get_network_interface().get_owner_name());
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
		ENSURE_V_MSG(!p_snapshot.is_buffer_failed(), false, "This snapshot is corrupted. The `vars_count` was expected here.");

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
				ENSURE_V_MSG(!p_snapshot.is_buffer_failed(), false, "This snapshot is corrupted. The `var_has_value` was expected at this point. Object: `" + synchronizer_object_data->object_name + "` Var: `" + var_desc.var.name + "`");

				if (var_has_value) {
					VarData value;
					SceneSynchronizerBase::var_data_decode(value, p_snapshot);
					ENSURE_V_MSG(!p_snapshot.is_buffer_failed(), false, "This snapshot is corrupted. The `variable value` was expected at this point. Object: `" + synchronizer_object_data->object_name + "` Var: `" + var_desc.var.name + "`");

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
		SceneSynchronizerDebugger::singleton()->print(ERROR, "[FATAL] The function `receive_trickled_sync_data` received malformed data.", scene_synchronizer->get_network_interface().get_owner_name());
		// Nothing to fetch.
		return;
	}

	const uint32_t epoch = future_epoch_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_1);

	DataBuffer *db = memnew(DataBuffer);

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
			SceneSynchronizerDebugger::singleton()->print(ERROR, "The function `receive_trickled_sync_data` failed applying the epoch because the received buffer is malformed. The node with ID `" + node_id + "` reported that the sub buffer size is `" + std::to_string(buffer_bit_count) + "` but the main-buffer doesn't have so many bits.", scene_synchronizer->get_network_interface().get_owner_name());
			break;
		}

		const int current_offset = future_epoch_buffer.get_bit_offset();
		const int expected_bit_offset_after_apply = current_offset + buffer_bit_count;

		NS::ObjectData *od = scene_synchronizer->get_object_data(node_id, false);
		if (od == nullptr) {
			SceneSynchronizerDebugger::singleton()->debug_print(&scene_synchronizer->get_network_interface(), "The function `receive_trickled_sync_data` is skiplatency the node with ID `" + itos(node_id.id) + "` as it was not found locally.");
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
			SceneSynchronizerDebugger::singleton()->print(INFO, "The function `receive_trickled_sync_data` is skiplatency the node `" + stream.od->object_name + "` as the function `trickled_collect` failed executing.", scene_synchronizer->get_network_interface().get_owner_name());
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

		// Reset the alpha so we can start interpolating.
		stream.alpha = 0.0;
		if (stream.past_epoch < stream.future_epoch) {
			stream.epochs_timespan = (float(stream.future_epoch) - float(stream.past_epoch)) * scene_synchronizer->get_fixed_frame_delta();
		} else {
			// The interpolation didn't start yet, so put the span to 0.0
			stream.epochs_timespan = 0.0;
		}
	}

	memdelete(db);
}

void ClientSynchronizer::process_trickled_sync(double p_delta) {
	NS_PROFILE

	DataBuffer *db1 = memnew(DataBuffer);
	DataBuffer *db2 = memnew(DataBuffer);

	for (int i = 0; i < int(trickled_sync_array.size()); ++i) {
		TrickledSyncInterpolationData &stream = trickled_sync_array[i];
		if (stream.epochs_timespan <= 0.001) {
			// The stream is not yet started.
			// OR
			// The stream for this node is stopped as the data received is old.
			continue;
		}

		NS::ObjectData *od = stream.od;
		if (od == nullptr) {
			SceneSynchronizerDebugger::singleton()->print(ERROR, "The function `process_received_trickled_sync_data` found a null NodeData into the `trickled_sync_array`; this is not supposed to happen.", scene_synchronizer->get_network_interface().get_owner_name());
			continue;
		}

#ifdef DEBUG_ENABLED
		if (!od->func_trickled_apply) {
			SceneSynchronizerDebugger::singleton()->print(ERROR, "The function `process_received_trickled_sync_data` skip the node `" + od->object_name + "` has an invalid apply epoch function named `trickled_apply`. Remotely you used the function `setup_trickled_sync` properly, while locally you didn't. Fix it.", scene_synchronizer->get_network_interface().get_owner_name());
			continue;
		}
#endif

		stream.alpha += p_delta / stream.epochs_timespan;
		stream.alpha = std::min(stream.alpha, scene_synchronizer->get_max_trickled_interpolation_alpha());
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
			SceneSynchronizerDebugger::singleton()->print(ERROR, "At this point the client is supposed to be disabled. This is a bug that must be solved.", scene_synchronizer->get_network_interface().get_owner_name());
		}
		// The netwroking is disabled and we can re-enable it.
		enabled = true;
		want_to_enable = false;
		scene_synchronizer->event_sync_started.broadcast();
	}

	need_full_snapshot_notified = false;

	NS::Snapshot received_snapshot;
	received_snapshot.copy(last_received_snapshot);
	received_snapshot.input_id = FrameIndex::NONE;

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
			[](void *p_user_pointer, FrameIndex p_input_id) {
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
		SceneSynchronizerDebugger::singleton()->print(ERROR, "Snapshot parsing failed.", scene_synchronizer->get_network_interface().get_owner_name());
		return false;
	}

	if make_unlikely (received_snapshot.input_id == FrameIndex::NONE && player_controller_object_data != nullptr) {
		// We espect that the player_controller is updated by this new snapshot,
		// so make sure it's done so.
		SceneSynchronizerDebugger::singleton()->print(ERROR, "The player controller (" + player_controller_object_data->object_name + ") was not part of the received snapshot, this happens when the server destroys the peer controller.", player_controller_object_data->object_name);
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
	NS_PROFILE

	r_snapshot.simulated_objects = simulated_objects;

	{
		NS_PROFILE_NAMED("Fetch `custom_data`");
		r_snapshot.has_custom_data = scene_synchronizer->synchronizer_manager->snapshot_get_custom_data(nullptr, r_snapshot.custom_data);
	}

	// Make sure we have room for all the NodeData.
	r_snapshot.object_vars.resize(scene_synchronizer->objects_data_storage.get_sorted_objects_data().size());

	// Fetch the data.
	for (const NS::ObjectData *od : scene_synchronizer->objects_data_storage.get_sorted_objects_data()) {
		NS_PROFILE_NAMED("Update object data");

		if (od == nullptr || od->realtime_sync_enabled_on_client == false) {
			continue;
		}

#ifdef NS_PROFILING_ENABLED
		std::string perf_info = "Object Name: " + od->object_name;
		NS_PROFILE_SET_INFO(perf_info);
#endif

		// Make sure this ID is valid.
		ERR_FAIL_COND_MSG(od->get_net_id() == ObjectNetId::NONE, "[BUG] It's not expected that the client has an uninitialized NetNodeId into the `organized_node_data` ");

#ifdef DEBUG_ENABLED
		CRASH_COND_MSG(od->get_net_id().id >= uint32_t(r_snapshot.object_vars.size()), "This array was resized above, this can't be triggered.");
#endif

		std::vector<NS::NameAndVar> *snap_node_vars = r_snapshot.object_vars.data() + od->get_net_id().id;
		snap_node_vars->resize(od->vars.size());

		NS::NameAndVar *snap_node_vars_ptr = snap_node_vars->data();
		for (std::size_t v = 0; v < od->vars.size(); v += 1) {
#ifdef NS_PROFILING_ENABLED
			std::string sub_perf_info = "Var: " + od->vars[v].var.name;
			NS_PROFILE_NAMED_WITH_INFO("Update object data variable", sub_perf_info);
#endif
			if (od->vars[v].enabled) {
				snap_node_vars_ptr[v].name = od->vars[v].var.name;
				snap_node_vars_ptr[v].value.copy(od->vars[v].var.value);
			} else {
				snap_node_vars_ptr[v].name = std::string();
				snap_node_vars_ptr[v].value = VarData();
			}
		}
	}
}

void ClientSynchronizer::update_simulated_objects_list(const std::vector<ObjectNetId> &p_simulated_objects) {
	NS_PROFILE

	// Reset the simulated object first.
	for (auto od : scene_synchronizer->get_all_object_data()) {
		if (!od) {
			continue;
		}
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
	active_objects.clear();
	for (ObjectNetId id : simulated_objects) {
		active_objects.push_back(scene_synchronizer->get_object_data(id));
	}
}

void ClientSynchronizer::apply_snapshot(
		const NS::Snapshot &p_snapshot,
		int p_flag,
		std::vector<std::string> *r_applied_data_info,
		bool p_skip_custom_data) {
	NS_PROFILE

	const std::vector<NS::NameAndVar> *snap_objects_vars = p_snapshot.object_vars.data();

	scene_synchronizer->change_events_begin(p_flag);

	update_simulated_objects_list(p_snapshot.simulated_objects);

	for (ObjectNetId net_node_id = { 0 }; net_node_id < ObjectNetId{ uint32_t(p_snapshot.object_vars.size()) }; net_node_id += 1) {
		NS::ObjectData *object_data = scene_synchronizer->get_object_data(net_node_id);

		if (object_data == nullptr) {
			// This can happen, and it's totally expected, because the server
			// doesn't always sync ALL the node_data: so that will result in a
			// not registered node.
			continue;
		}

		if (object_data->realtime_sync_enabled_on_client == false) {
			// This node sync is disabled.
			continue;
		}

		const std::vector<NS::NameAndVar> &snap_object_vars = snap_objects_vars[net_node_id.id];

		if (r_applied_data_info) {
			r_applied_data_info->push_back("Applied snapshot data on the node: " + std::string(object_data->object_name.c_str()));
		}

		// NOTE: The vars may not contain ALL the variables: it depends on how
		//       the snapshot was captured.
		for (VarId v = { 0 }; v < VarId{ uint32_t(snap_object_vars.size()) }; v += 1) {
			if (snap_object_vars[v.id].name.empty()) {
				// This variable was not set, skip it.
				continue;
			}

#ifdef DEBUG_ENABLED
			ASSERT_COND_MSG(snap_object_vars[v.id].name == object_data->vars[v.id].var.name, "The variable name, on both snapshot and client scene_sync, are supposed to be exactly the same at this point. Snapshot `" + snap_object_vars[v.id].name + "` ClientSceneSync `" + object_data->vars[v.id].var.name + "`");
#endif

			const std::string &variable_name = snap_object_vars[v.id].name;
			const VarData &snap_value = snap_object_vars[v.id].value;
			VarData current_val;
			const bool get_var_success = scene_synchronizer->synchronizer_manager->get_variable(
					object_data->app_object_handle,
					variable_name.c_str(),
					current_val);

			if (!get_var_success || !SceneSynchronizerBase::var_data_compare(current_val, snap_value)) {
				object_data->vars[v.id].var.value.copy(snap_value);

				scene_synchronizer->synchronizer_manager->set_variable(
						object_data->app_object_handle,
						variable_name.c_str(),
						snap_value);

				scene_synchronizer->change_event_add(
						object_data,
						v,
						current_val);

#ifdef DEBUG_ENABLED
				if (scene_synchronizer->pedantic_checks) {
					// Make sure the set value matches the one just set.
					scene_synchronizer->synchronizer_manager->get_variable(
							object_data->app_object_handle,
							variable_name.c_str(),
							current_val);
					ASSERT_COND_MSG(SceneSynchronizerBase::var_data_compare(current_val, snap_value), "There was a fatal error while setting the propertly `" + variable_name + "` on the object `" + object_data->object_name + "`. The set data differs from the property set by the NetSync: set data `" + scene_synchronizer->var_data_stringify(current_val, true).c_str() + "` NetSync data `" + scene_synchronizer->var_data_stringify(snap_value, true) + "`");
				}
#endif

				if (r_applied_data_info) {
					r_applied_data_info->push_back(std::string() + " |- Variable: " + variable_name + " New value: " + SceneSynchronizerBase::var_data_stringify(snap_value));
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
