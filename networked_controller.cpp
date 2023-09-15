/*************************************************************************/
/*  networked_controller.cpp                                             */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2021 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2021 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

/**
	@author AndreaCatania
*/

#include "networked_controller.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/marshalls.h"
#include "godot4/gd_network_interface.h"
#include "scene_synchronizer.h"
#include "scene_synchronizer_debugger.h"
#include <algorithm>

#define METADATA_SIZE 1

void NetworkedController::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_server_controlled", "server_controlled"), &NetworkedController::set_server_controlled);
	ClassDB::bind_method(D_METHOD("get_server_controlled"), &NetworkedController::get_server_controlled);

	ClassDB::bind_method(D_METHOD("set_player_input_storage_size", "size"), &NetworkedController::set_player_input_storage_size);
	ClassDB::bind_method(D_METHOD("get_player_input_storage_size"), &NetworkedController::get_player_input_storage_size);

	ClassDB::bind_method(D_METHOD("set_max_redundant_inputs", "max_redundant_inputs"), &NetworkedController::set_max_redundant_inputs);
	ClassDB::bind_method(D_METHOD("get_max_redundant_inputs"), &NetworkedController::get_max_redundant_inputs);

	ClassDB::bind_method(D_METHOD("set_tick_speedup_notification_delay", "delay_in_ms"), &NetworkedController::set_tick_speedup_notification_delay);
	ClassDB::bind_method(D_METHOD("get_tick_speedup_notification_delay"), &NetworkedController::get_tick_speedup_notification_delay);

	ClassDB::bind_method(D_METHOD("set_network_traced_frames", "size"), &NetworkedController::set_network_traced_frames);
	ClassDB::bind_method(D_METHOD("get_network_traced_frames"), &NetworkedController::get_network_traced_frames);

	ClassDB::bind_method(D_METHOD("set_min_frames_delay", "val"), &NetworkedController::set_min_frames_delay);
	ClassDB::bind_method(D_METHOD("get_min_frames_delay"), &NetworkedController::get_min_frames_delay);

	ClassDB::bind_method(D_METHOD("set_max_frames_delay", "val"), &NetworkedController::set_max_frames_delay);
	ClassDB::bind_method(D_METHOD("get_max_frames_delay"), &NetworkedController::get_max_frames_delay);

	ClassDB::bind_method(D_METHOD("set_tick_acceleration", "acceleration"), &NetworkedController::set_tick_acceleration);
	ClassDB::bind_method(D_METHOD("get_tick_acceleration"), &NetworkedController::get_tick_acceleration);

	ClassDB::bind_method(D_METHOD("get_current_input_id"), &NetworkedController::get_current_input_id);

	ClassDB::bind_method(D_METHOD("player_get_pretended_delta"), &NetworkedController::player_get_pretended_delta);

	ClassDB::bind_method(D_METHOD("on_peer_status_updated"), &NetworkedController::on_peer_status_updated);
	ClassDB::bind_method(D_METHOD("on_state_validated"), &NetworkedController::on_state_validated);
	ClassDB::bind_method(D_METHOD("on_rewind_frame_begin"), &NetworkedController::on_rewind_frame_begin);

	ClassDB::bind_method(D_METHOD("_rpc_server_send_inputs"), &NetworkedController::_rpc_server_send_inputs);
	ClassDB::bind_method(D_METHOD("_rpc_set_server_controlled"), &NetworkedController::_rpc_set_server_controlled);
	ClassDB::bind_method(D_METHOD("_rpc_notify_fps_acceleration"), &NetworkedController::_rpc_notify_fps_acceleration);

	ClassDB::bind_method(D_METHOD("is_server_controller"), &NetworkedController::is_server_controller);
	ClassDB::bind_method(D_METHOD("is_player_controller"), &NetworkedController::is_player_controller);
	ClassDB::bind_method(D_METHOD("is_doll_controller"), &NetworkedController::is_doll_controller);
	ClassDB::bind_method(D_METHOD("is_nonet_controller"), &NetworkedController::is_nonet_controller);

	GDVIRTUAL_BIND(_collect_inputs, "delta", "buffer");
	GDVIRTUAL_BIND(_controller_process, "delta", "buffer");
	GDVIRTUAL_BIND(_are_inputs_different, "inputs_A", "inputs_B");
	GDVIRTUAL_BIND(_count_input_size, "inputs");

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "server_controlled"), "set_server_controlled", "get_server_controlled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "input_storage_size", PROPERTY_HINT_RANGE, "5,2000,1"), "set_player_input_storage_size", "get_player_input_storage_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_redundant_inputs", PROPERTY_HINT_RANGE, "0,1000,1"), "set_max_redundant_inputs", "get_max_redundant_inputs");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tick_speedup_notification_delay", PROPERTY_HINT_RANGE, "0,5000,1"), "set_tick_speedup_notification_delay", "get_tick_speedup_notification_delay");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "network_traced_frames", PROPERTY_HINT_RANGE, "1,1000,1"), "set_network_traced_frames", "get_network_traced_frames");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "min_frames_delay", PROPERTY_HINT_RANGE, "0,100,1"), "set_min_frames_delay", "get_min_frames_delay");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_frames_delay", PROPERTY_HINT_RANGE, "0,100,1"), "set_max_frames_delay", "get_max_frames_delay");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tick_acceleration", PROPERTY_HINT_RANGE, "0.1,20.0,0.01"), "set_tick_acceleration", "get_tick_acceleration");

	ADD_SIGNAL(MethodInfo("controller_reset"));
	ADD_SIGNAL(MethodInfo("input_missed", PropertyInfo(Variant::INT, "missing_input_id")));
	ADD_SIGNAL(MethodInfo("client_speedup_adjusted", PropertyInfo(Variant::INT, "input_worst_receival_time_ms"), PropertyInfo(Variant::INT, "optimal_frame_delay"), PropertyInfo(Variant::INT, "current_frame_delay"), PropertyInfo(Variant::INT, "distance_to_optimal")));
}

NetworkedController::NetworkedController() :
		Node() {
	GdNetworkInterface *ni = memnew(GdNetworkInterface);
	ni->owner = this;
	network_interface = ni;

	inputs_buffer = memnew(DataBuffer);

	network_interface->configure_rpc(SNAME("_rpc_server_send_inputs"), false, false);
	network_interface->configure_rpc(SNAME("_rpc_set_server_controlled"), false, true);
	network_interface->configure_rpc(SNAME("_rpc_notify_fps_acceleration"), false, false);
}

NetworkedController::~NetworkedController() {
	memdelete(inputs_buffer);
	inputs_buffer = nullptr;

	if (controller != nullptr) {
		memdelete(controller);
		controller = nullptr;
		controller_type = CONTROLLER_TYPE_NULL;
	}

	memdelete(network_interface);
	network_interface = nullptr;
}

void NetworkedController::set_server_controlled(bool p_server_controlled) {
	if (server_controlled == p_server_controlled) {
		// It's the same, nothing to do.
		return;
	}

	if (is_networking_initialized()) {
		if (is_server_controller()) {
			// This is the server, let's start the procedure to switch controll mode.

#ifdef DEBUG_ENABLED
			CRASH_COND_MSG(scene_synchronizer == nullptr, "When the `NetworkedController` is a server, the `scene_synchronizer` is always set.");
#endif

			// First update the variable.
			server_controlled = p_server_controlled;

			// Notify the `SceneSynchronizer` about it.
			scene_synchronizer->notify_controller_control_mode_changed(this);

			// Tell the client to do the switch too.
			if (network_interface->get_unit_authority() != 1) {
				network_interface->rpc(
						network_interface->get_unit_authority(),
						SNAME("_rpc_set_server_controlled"),
						server_controlled);
			} else {
				SceneSynchronizerDebugger::singleton()->debug_warning(network_interface, "The node is owned by the server, there is no client that can control it; please assign the proper authority.");
			}

		} else if (is_player_controller() || is_doll_controller()) {
			SceneSynchronizerDebugger::singleton()->debug_warning(network_interface, "You should never call the function `set_server_controlled` on the client, this has an effect only if called on the server.");

		} else if (is_nonet_controller()) {
			// There is no networking, the same instance is both the client and the
			// server already, nothing to do.
			server_controlled = p_server_controlled;

		} else {
#ifdef DEBUG_ENABLED
			CRASH_NOW_MSG("Unreachable, all the cases are handled.");
#endif
		}
	} else {
		// This called during initialization or on the editor, nothing special just
		// set it.
		server_controlled = p_server_controlled;
	}
#ifdef DEBUG_ENABLED
	if (GDVIRTUAL_IS_OVERRIDDEN(_collect_inputs) == false && server_controlled == false) {
		WARN_PRINT("In your script you must inherit the virtual method `_collect_inputs` to correctly use the `NetworkedController`.");
	}

	if (GDVIRTUAL_IS_OVERRIDDEN(_controller_process) == false && server_controlled == false) {
		WARN_PRINT("In your script you must inherit the virtual method `_controller_process` to correctly use the `NetworkedController`.");
	}

	if (GDVIRTUAL_IS_OVERRIDDEN(_are_inputs_different) == false && server_controlled == false) {
		WARN_PRINT("In your script you must inherit the virtual method `_are_inputs_different` to correctly use the `NetworkedController`.");
	}

	if (GDVIRTUAL_IS_OVERRIDDEN(_count_input_size) == false && server_controlled == false) {
		WARN_PRINT("In your script you must inherit the virtual method `_count_input_size` to correctly use the `NetworkedController`.");
	}
#endif
}

bool NetworkedController::get_server_controlled() const {
	return server_controlled;
}

void NetworkedController::set_player_input_storage_size(int p_size) {
	player_input_storage_size = p_size;
}

int NetworkedController::get_player_input_storage_size() const {
	return player_input_storage_size;
}

void NetworkedController::set_max_redundant_inputs(int p_max) {
	max_redundant_inputs = p_max;
}

int NetworkedController::get_max_redundant_inputs() const {
	return max_redundant_inputs;
}

void NetworkedController::set_tick_speedup_notification_delay(int p_delay) {
	tick_speedup_notification_delay = p_delay;
}

int NetworkedController::get_tick_speedup_notification_delay() const {
	return tick_speedup_notification_delay;
}

void NetworkedController::set_network_traced_frames(int p_size) {
	network_traced_frames = p_size;
}

int NetworkedController::get_network_traced_frames() const {
	return network_traced_frames;
}

void NetworkedController::set_min_frames_delay(int p_val) {
	min_frames_delay = p_val;
}

int NetworkedController::get_min_frames_delay() const {
	return min_frames_delay;
}

void NetworkedController::set_max_frames_delay(int p_val) {
	max_frames_delay = p_val;
}

int NetworkedController::get_max_frames_delay() const {
	return max_frames_delay;
}

void NetworkedController::set_tick_acceleration(double p_acceleration) {
	tick_acceleration = p_acceleration;
}

double NetworkedController::get_tick_acceleration() const {
	return tick_acceleration;
}

uint32_t NetworkedController::get_current_input_id() const {
	ERR_FAIL_NULL_V(controller, 0);
	return controller->get_current_input_id();
}

real_t NetworkedController::player_get_pretended_delta() const {
	ERR_FAIL_COND_V_MSG(is_player_controller() == false, 1.0, "This function can be called only on client.");
	return get_player_controller()->pretended_delta;
}

void NetworkedController::validate_script_implementation() {
	ERR_FAIL_COND_MSG(has_method("_collect_inputs") == false && server_controlled == false, "In your script you must inherit the virtual method `_collect_inputs` to correctly use the `NetworkedController`.");
	ERR_FAIL_COND_MSG(has_method("_controller_process") == false && server_controlled == false, "In your script you must inherit the virtual method `_controller_process` to correctly use the `NetworkedController`.");
	ERR_FAIL_COND_MSG(has_method("_are_inputs_different") == false && server_controlled == false, "In your script you must inherit the virtual method `_are_inputs_different` to correctly use the `NetworkedController`.");
	ERR_FAIL_COND_MSG(has_method("_count_input_size") == false && server_controlled == false, "In your script you must inherit the virtual method `_count_input_size` to correctly use the `NetworkedController`.");
}

void NetworkedController::native_collect_inputs(double p_delta, DataBuffer &r_buffer) {
	PROFILE_NODE

	const bool executed = GDVIRTUAL_CALL(_collect_inputs, p_delta, &r_buffer);
	if (executed == false) {
		NET_DEBUG_ERR("The function _collect_inputs was not executed!");
	}
}

void NetworkedController::native_controller_process(double p_delta, DataBuffer &p_buffer) {
	PROFILE_NODE

	const bool executed = GDVIRTUAL_CALL(
			_controller_process,
			p_delta,
			&p_buffer);

	if (executed == false) {
		NET_DEBUG_ERR("The function _controller_process was not executed!");
	}
}

bool NetworkedController::native_are_inputs_different(DataBuffer &p_buffer_A, DataBuffer &p_buffer_B) {
	PROFILE_NODE

	bool are_different = true;
	const bool executed = GDVIRTUAL_CALL(
			_are_inputs_different,
			&p_buffer_A,
			&p_buffer_B,
			are_different);

	if (executed == false) {
		NET_DEBUG_ERR("The function _are_inputs_different was not executed!");
		return true;
	}

	return are_different;
}

uint32_t NetworkedController::native_count_input_size(DataBuffer &p_buffer) {
	PROFILE_NODE

	int input_size = 0;
	const bool executed = GDVIRTUAL_CALL(_count_input_size, &p_buffer, input_size);
	if (executed == false) {
		NET_DEBUG_ERR("The function `_count_input_size` was not executed.");
	}
	return uint32_t(input_size >= 0 ? input_size : 0);
}

bool NetworkedController::has_another_instant_to_process_after(int p_i) const {
	ERR_FAIL_COND_V_MSG(is_player_controller() == false, false, "Can be executed only on player controllers.");
	return static_cast<PlayerController *>(controller)->has_another_instant_to_process_after(p_i);
}

void NetworkedController::process(double p_delta) {
	// This function is called by the `SceneSync` because it's registered as
	// processing function.
	controller->process(p_delta);
}

ServerController *NetworkedController::get_server_controller() {
	ERR_FAIL_COND_V_MSG(is_server_controller() == false, nullptr, "This controller is not a server controller.");
	return static_cast<ServerController *>(controller);
}

const ServerController *NetworkedController::get_server_controller() const {
	ERR_FAIL_COND_V_MSG(is_server_controller() == false, nullptr, "This controller is not a server controller.");
	return static_cast<const ServerController *>(controller);
}

PlayerController *NetworkedController::get_player_controller() {
	ERR_FAIL_COND_V_MSG(is_player_controller() == false, nullptr, "This controller is not a player controller.");
	return static_cast<PlayerController *>(controller);
}

const PlayerController *NetworkedController::get_player_controller() const {
	ERR_FAIL_COND_V_MSG(is_player_controller() == false, nullptr, "This controller is not a player controller.");
	return static_cast<const PlayerController *>(controller);
}

DollController *NetworkedController::get_doll_controller() {
	ERR_FAIL_COND_V_MSG(is_doll_controller() == false, nullptr, "This controller is not a doll controller.");
	return static_cast<DollController *>(controller);
}

const DollController *NetworkedController::get_doll_controller() const {
	ERR_FAIL_COND_V_MSG(is_doll_controller() == false, nullptr, "This controller is not a doll controller.");
	return static_cast<const DollController *>(controller);
}

NoNetController *NetworkedController::get_nonet_controller() {
	ERR_FAIL_COND_V_MSG(is_nonet_controller() == false, nullptr, "This controller is not a no net controller.");
	return static_cast<NoNetController *>(controller);
}

const NoNetController *NetworkedController::get_nonet_controller() const {
	ERR_FAIL_COND_V_MSG(is_nonet_controller() == false, nullptr, "This controller is not a no net controller.");
	return static_cast<const NoNetController *>(controller);
}

bool NetworkedController::is_networking_initialized() const {
	return controller_type != CONTROLLER_TYPE_NULL;
}

bool NetworkedController::is_server_controller() const {
	return controller_type == CONTROLLER_TYPE_SERVER || controller_type == CONTROLLER_TYPE_AUTONOMOUS_SERVER;
}

bool NetworkedController::is_player_controller() const {
	return controller_type == CONTROLLER_TYPE_PLAYER;
}

bool NetworkedController::is_doll_controller() const {
	return controller_type == CONTROLLER_TYPE_DOLL;
}

bool NetworkedController::is_nonet_controller() const {
	return controller_type == CONTROLLER_TYPE_NONETWORK;
}

void NetworkedController::set_inputs_buffer(const BitArray &p_new_buffer, uint32_t p_metadata_size_in_bit, uint32_t p_size_in_bit) {
	inputs_buffer->get_buffer_mut().get_bytes_mut() = p_new_buffer.get_bytes();
	inputs_buffer->shrink_to(p_metadata_size_in_bit, p_size_in_bit);
}

void NetworkedController::notify_registered_with_synchronizer(NS::SceneSynchronizer *p_synchronizer) {
	if (scene_synchronizer) {
		scene_synchronizer->disconnect("rewind_frame_begin", Callable(this, "on_rewind_frame_begin"));
		scene_synchronizer->disconnect("state_validated", Callable(this, "on_state_validated"));
		scene_synchronizer->disconnect("peer_status_updated", Callable(this, "on_peer_status_updated"));
		scene_synchronizer->unregister_process(this, PROCESSPHASE_PROCESS, callable_mp(this, &NetworkedController::process));
	}

	node_id = NetID_NONE;
	scene_synchronizer = p_synchronizer;

	if (scene_synchronizer) {
		scene_synchronizer->register_process(this, PROCESSPHASE_PROCESS, callable_mp(this, &NetworkedController::process));
		scene_synchronizer->connect("peer_status_updated", Callable(this, "on_peer_status_updated"));
		scene_synchronizer->connect("state_validated", Callable(this, "on_state_validated"));
		scene_synchronizer->connect("rewind_frame_begin", Callable(this, "on_rewind_frame_begin"));
	}
}

NS::SceneSynchronizer *NetworkedController::get_scene_synchronizer() const {
	return scene_synchronizer;
}

bool NetworkedController::has_scene_synchronizer() const {
	return scene_synchronizer;
}

void NetworkedController::on_peer_status_updated(Node *p_node, NetNodeId p_id, int p_peer_id, bool p_connected, bool p_enabled) {
	if (p_node == this) {
		if (p_connected) {
			peer_id = p_peer_id;
		} else {
			peer_id = -1;
		}

		if (is_server_controller()) {
			get_server_controller()->on_peer_update(p_connected && p_enabled);
		}
	}
}

void NetworkedController::on_state_validated(uint32_t p_input_id) {
	if (controller) {
		controller->notify_input_checked(p_input_id);
	}
}

void NetworkedController::on_rewind_frame_begin(uint32_t p_input_id, int p_index, int p_count) {
	if (controller && is_realtime_enabled()) {
		controller->queue_instant_process(p_input_id, p_index, p_count);
	}
}

void NetworkedController::_rpc_server_send_inputs(const Vector<uint8_t> &p_data) {
	if (controller) {
		controller->receive_inputs(p_data);
	}
}

void NetworkedController::_rpc_set_server_controlled(bool p_server_controlled) {
	ERR_FAIL_COND_MSG(is_player_controller() == false, "This function is supposed to be called on the server.");
	server_controlled = p_server_controlled;

	ERR_FAIL_COND_MSG(scene_synchronizer == nullptr, "The server controller is supposed to be set on the client at this point.");
	scene_synchronizer->notify_controller_control_mode_changed(this);
}

void NetworkedController::_rpc_notify_fps_acceleration(const Vector<uint8_t> &p_data) {
	ERR_FAIL_COND(is_player_controller() == false);
	ERR_FAIL_COND(p_data.size() != 1);

	int8_t additional_frames_to_produce;
	memcpy(
			&additional_frames_to_produce,
			&p_data[0],
			sizeof(int8_t));

	PlayerController *player_controller = static_cast<PlayerController *>(controller);

	// Slowdown the acceleration when near the target.
	player_controller->acceleration_fps_speed = CLAMP(double(additional_frames_to_produce) / get_tick_acceleration(), -1.0, 1.0) * get_tick_acceleration();
	const double acceleration_fps_speed_ABS = ABS(player_controller->acceleration_fps_speed);

	if (acceleration_fps_speed_ABS >= CMP_EPSILON2) {
		const double acceleration_time = double(ABS(additional_frames_to_produce)) / acceleration_fps_speed_ABS;
		player_controller->acceleration_fps_timer = acceleration_time;
	} else {
		player_controller->acceleration_fps_timer = 0.0;
	}

#ifdef DEBUG_ENABLED
	const bool debug = ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/debug_server_speedup");
	if (debug) {
		print_line(
				String() +
				"Client received speedup." +
				" Frames to produce: `" + itos(additional_frames_to_produce) + "`" +
				" Acceleration fps: `" + rtos(player_controller->acceleration_fps_speed) + "`" +
				" Acceleration time: `" + rtos(player_controller->acceleration_fps_timer) + "`");
	}
#endif
}

void NetworkedController::player_set_has_new_input(bool p_has) {
	has_player_new_input = p_has;
}

bool NetworkedController::player_has_new_input() const {
	return has_player_new_input;
}

bool NetworkedController::is_realtime_enabled() {
	if (node_id == NetID_NONE) {
		if (scene_synchronizer) {
			NetUtility::NodeData *nd = scene_synchronizer->find_node_data(this);
			if (nd) {
				node_id = nd->id;
			}
		}
	}
	if (node_id != NetID_NONE) {
		NetUtility::NodeData *nd = scene_synchronizer->get_node_data(node_id);
		if (nd) {
			return nd->realtime_sync_enabled_on_client;
		}
	}
	return false;
}

void NetworkedController::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}

#ifdef DEBUG_ENABLED
			// This can't happen, since only the doll are processed here.
			CRASH_COND(is_doll_controller() == false);
#endif
			const double physics_ticks_per_second = Engine::get_singleton()->get_physics_ticks_per_second();
			const double delta = 1.0 / physics_ticks_per_second;
			static_cast<DollController *>(controller)->process(delta);

		} break;
#ifdef DEBUG_ENABLED
		case NOTIFICATION_READY: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}

			validate_script_implementation();
		} break;
#endif
	}
}

void NetworkedController::notify_controller_reset() {
	emit_signal("controller_reset");
}

bool NetworkedController::__input_data_parse(
		const Vector<uint8_t> &p_data,
		void *p_user_pointer,
		void (*p_input_parse)(void *p_user_pointer, uint32_t p_input_id, int p_input_size_in_bits, const BitArray &p_input)) {
	// The packet is composed as follow:
	// |- Four bytes for the first input ID.
	// \- Array of inputs:
	//      |-- First byte the amount of times this input is duplicated in the packet.
	//      |-- inputs buffer.
	//
	// Let's decode it!

	const int data_len = p_data.size();

	int ofs = 0;

	ERR_FAIL_COND_V(data_len < 4, false);
	const uint32_t first_input_id = decode_uint32(p_data.ptr() + ofs);
	ofs += 4;

	uint32_t inserted_input_count = 0;

	// Contains the entire packet and in turn it will be seek to specific location
	// so I will not need to copy chunk of the packet data.
	DataBuffer *pir = memnew(DataBuffer);
	pir->copy(p_data);
	pir->begin_read();
	// TODO this is for 3.2
	//pir.get_buffer_mut().resize_in_bytes(data_len);
	//memcpy(pir.get_buffer_mut().get_bytes_mut().ptrw(), p_data.ptr(), data_len);

	while (ofs < data_len) {
		ERR_FAIL_COND_V_MSG(ofs + 1 > data_len, false, "The arrived packet size doesn't meet the expected size.");
		// First byte is used for the duplication count.
		const uint8_t duplication = p_data[ofs];
		ofs += 1;

		// Validate input
		const int input_buffer_offset_bit = ofs * 8;
		pir->shrink_to(input_buffer_offset_bit, (data_len - ofs) * 8);
		pir->seek(input_buffer_offset_bit);
		// Read metadata
		const bool has_data = pir->read_bool();

		const int input_size_in_bits = (has_data ? int(native_count_input_size(*pir)) : 0) + METADATA_SIZE;

		// Pad to 8 bits.
		const int input_size_padded =
				Math::ceil((static_cast<float>(input_size_in_bits)) / 8.0);
		ERR_FAIL_COND_V_MSG(ofs + input_size_padded > data_len, false, "The arrived packet size doesn't meet the expected size.");

		// Extract the data and copy into a BitArray.
		BitArray bit_array;
		bit_array.get_bytes_mut().resize(input_size_padded);
		memcpy(
				bit_array.get_bytes_mut().ptrw(),
				p_data.ptr() + ofs,
				input_size_padded);

		// The input is valid, and the bit array is created: now execute the callback.
		for (int sub = 0; sub <= duplication; sub += 1) {
			const uint32_t input_id = first_input_id + inserted_input_count;
			inserted_input_count += 1;

			p_input_parse(p_user_pointer, input_id, input_size_in_bits, bit_array);
		}

		// Advance the offset to parse the next input.
		ofs += input_size_padded;
	}

	memdelete(pir);
	pir = nullptr;

	ERR_FAIL_COND_V_MSG(ofs != data_len, false, "At the end was detected that the arrived packet has an unexpected size.");
	return true;
}

bool NetworkedController::__input_data_get_first_input_id(
		const Vector<uint8_t> &p_data,
		uint32_t &p_input_id) const {
	// The first four bytes are reserved for the input_id.
	if (p_data.size() < 4) {
		return false;
	}

	const uint8_t *ptrw = p_data.ptr();
	const uint32_t *ptrw_32bit = reinterpret_cast<const uint32_t *>(ptrw);
	p_input_id = ptrw_32bit[0];

	return true;
}

bool NetworkedController::__input_data_set_first_input_id(
		Vector<uint8_t> &p_data,
		uint32_t p_input_id) {
	// The first four bytes are reserved for the input_id.
	if (p_data.size() < 4) {
		return false;
	}

	uint8_t *ptrw = p_data.ptrw();
	uint32_t *ptrw_32bit = reinterpret_cast<uint32_t *>(ptrw);
	ptrw_32bit[0] = p_input_id;

	return true;
}

RemotelyControlledController::RemotelyControlledController(NetworkedController *p_node) :
		Controller(p_node) {}

void RemotelyControlledController::on_peer_update(bool p_peer_enabled) {
	if (p_peer_enabled == peer_enabled) {
		// Nothing to updated.
		return;
	}

	peer_enabled = p_peer_enabled;

	// Client inputs reset.
	ghost_input_count = 0;
	snapshots.clear();
}

uint32_t RemotelyControlledController::get_current_input_id() const {
	return current_input_buffer_id;
}

int RemotelyControlledController::get_inputs_count() const {
	return snapshots.size();
}

uint32_t RemotelyControlledController::last_known_input() const {
	if (snapshots.size() > 0) {
		return snapshots.back().id;
	} else {
		return UINT32_MAX;
	}
}

bool RemotelyControlledController::fetch_next_input(real_t p_delta) {
	bool is_new_input = true;

	if (unlikely(current_input_buffer_id == UINT32_MAX)) {
		// As initial packet, anything is good.
		if (snapshots.empty() == false) {
			// First input arrived.
			set_frame_input(snapshots.front(), true);
			snapshots.pop_front();
			// Start tracing the packets from this moment on.
			SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] Input `" + itos(current_input_buffer_id) + "` selected as first input.", true);
		} else {
			is_new_input = false;
			SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] Still no inputs.", true);
		}
	} else {
		const uint32_t next_input_id = current_input_buffer_id + 1;
		SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The server is looking for: " + itos(next_input_id), true);

		if (unlikely(streaming_paused)) {
			SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The streaming is paused.", true);
			// Stream is paused.
			if (snapshots.empty() == false &&
					snapshots.front().id >= next_input_id) {
				// A new input has arrived while the stream is paused.
				const bool is_buffer_void = (snapshots.front().buffer_size_bit - METADATA_SIZE) == 0;
				streaming_paused = is_buffer_void;
				set_frame_input(snapshots.front(), true);
				snapshots.pop_front();
				is_new_input = true;
			} else {
				// No inputs, or we are not yet arrived to the client input,
				// so just pretend the next input is void.
				node->set_inputs_buffer(BitArray(METADATA_SIZE), METADATA_SIZE, 0);
				is_new_input = false;
			}
		} else if (unlikely(snapshots.empty() == true)) {
			// The input buffer is empty; a packet is missing.
			SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] Missing input: " + itos(next_input_id) + " Input buffer is void, i'm using the previous one!");

			is_new_input = false;
			ghost_input_count += 1;

		} else {
			SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The input buffer is not empty, so looking for the next input. Hopefully `" + itos(next_input_id) + "`", true);

			// The input buffer is not empty, search the new input.
			if (next_input_id == snapshots.front().id) {
				SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The input `" + itos(next_input_id) + "` was found.", true);

				// Wow, the next input is perfect!
				set_frame_input(snapshots.front(), false);
				snapshots.pop_front();

				ghost_input_count = 0;
			} else {
				// The next packet is not here. This can happen when:
				// - The packet is lost or not yet arrived.
				// - The client for any reason desync with the server.
				//
				// In this cases, the server has the hard task to re-sync.
				//
				// # What it does, then?
				// Initially it see that only 1 packet is missing so it just use
				// the previous one and increase `ghost_inputs_count` to 1.
				//
				// The next iteration, if the packet is not yet arrived the
				// server trys to take the next packet with the `id` less or
				// equal to `next_packet_id + ghost_packet_id`.
				//
				// As you can see the server doesn't lose immediately the hope
				// to find the missing packets, but at the same time deals with
				// it so increases its search pool per each iteration.
				//
				// # Wise input search.
				// Let's consider the case when a set of inputs arrive at the
				// same time, while the server is struggling for the missing packets.
				//
				// In the meanwhile that the packets were chilling on the net,
				// the server were simulating by guessing on their data; this
				// mean that they don't have any longer room to be simulated
				// when they arrive, and the right thing would be just forget
				// about these.
				//
				// The thing is that these can still contain meaningful data, so
				// instead to jump directly to the newest we restart the inputs
				// from the next important packet.
				//
				// For this reason we keep track the amount of missing packets
				// using `ghost_input_count`.

				SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The input `" + itos(next_input_id) + "` was NOT found. Recovering process started.", true);
				SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] ghost_input_count: `" + itos(ghost_input_count) + "`", true);

				const int size = MIN(ghost_input_count, snapshots.size());
				const uint32_t ghost_packet_id = next_input_id + ghost_input_count;

				bool recovered = false;
				FrameSnapshot pi;

				DataBuffer *pir_A = memnew(DataBuffer);
				DataBuffer *pir_B = memnew(DataBuffer);
				pir_A->copy(node->get_inputs_buffer());

				for (int i = 0; i < size; i += 1) {
					SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] checking if `" + itos(snapshots.front().id) + "` can be used to recover `" + itos(next_input_id) + "`.", true);

					if (ghost_packet_id < snapshots.front().id) {
						SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The input `" + itos(snapshots.front().id) + "` can't be used as the ghost_packet_id (`" + itos(ghost_packet_id) + "`) is more than the input.", true);
						break;
					} else {
						const uint32_t input_id = snapshots.front().id;
						SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The input `" + itos(input_id) + "` is eligible as next frame.", true);

						pi = snapshots.front();
						snapshots.pop_front();
						recovered = true;

						// If this input has some important changes compared to the last
						// good input, let's recover to this point otherwise skip it
						// until the last one.
						// Useful to avoid that the server stay too much behind the
						// client.

						pir_B->copy(pi.inputs_buffer);
						pir_B->shrink_to(METADATA_SIZE, pi.buffer_size_bit - METADATA_SIZE);

						pir_A->begin_read();
						pir_A->seek(METADATA_SIZE);
						pir_B->begin_read();
						pir_B->seek(METADATA_SIZE);

						const bool are_different = node->native_are_inputs_different(*pir_A, *pir_B);
						if (are_different) {
							SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The input `" + itos(input_id) + "` is different from the one executed so far, so better to execute it.", true);
							break;
						}
					}
				}

				memdelete(pir_A);
				pir_A = nullptr;
				memdelete(pir_B);
				pir_B = nullptr;

				if (recovered) {
					set_frame_input(pi, false);
					ghost_input_count = 0;
					SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "Packet recovered. The new InputID is: `" + itos(current_input_buffer_id) + "`");
				} else {
					ghost_input_count += 1;
					is_new_input = false;
					SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "Packet still missing, the server is still using the old input.");
				}
			}
		}
	}

#ifdef DEBUG_ENABLED
	if (snapshots.empty() == false && current_input_buffer_id != UINT32_MAX) {
		// At this point is guaranteed that the current_input_buffer_id is never
		// greater than the first item contained by `snapshots`.
		CRASH_COND(current_input_buffer_id >= snapshots.front().id);
	}
#endif
	return is_new_input;
}

void RemotelyControlledController::set_frame_input(const FrameSnapshot &p_frame_snapshot, bool p_first_input) {
	node->set_inputs_buffer(
			p_frame_snapshot.inputs_buffer,
			METADATA_SIZE,
			p_frame_snapshot.buffer_size_bit - METADATA_SIZE);
	current_input_buffer_id = p_frame_snapshot.id;
}

void RemotelyControlledController::process(double p_delta) {
	const bool is_new_input = fetch_next_input(p_delta);

	if (unlikely(current_input_buffer_id == UINT32_MAX)) {
		// Skip this until the first input arrive.
		SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "Server skips this frame as the current_input_buffer_id == UINT32_MAX", true);
		return;
	}

#ifdef DEBUG_ENABLED
	if (!is_new_input) {
		node->emit_signal("input_missed", current_input_buffer_id + 1);
	}
#endif

	SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "RemotelyControlled process index: " + itos(current_input_buffer_id), true);

	node->get_inputs_buffer_mut().begin_read();
	node->get_inputs_buffer_mut().seek(METADATA_SIZE);
	SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(&node->get_network_interface(), SceneSynchronizerDebugger::READ);
	node->native_controller_process(
			p_delta,
			node->get_inputs_buffer_mut());
	SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();
}

bool is_remote_frame_A_older(const FrameSnapshot &p_snap_a, const FrameSnapshot &p_snap_b) {
	return p_snap_a.id < p_snap_b.id;
}

bool RemotelyControlledController::receive_inputs(const Vector<uint8_t> &p_data) {
	const uint32_t now = OS::get_singleton()->get_ticks_msec();
	struct SCParseTmpData {
		RemotelyControlledController *controller;
		NetworkedController *node_controller;
		uint32_t now;
	} tmp = {
		this,
		node,
		now
	};

	const bool success = node->__input_data_parse(
			p_data,
			&tmp,

			// Parse the Input:
			[](void *p_user_pointer, uint32_t p_input_id, int p_input_size_in_bits, const BitArray &p_bit_array) -> void {
				SCParseTmpData *pd = static_cast<SCParseTmpData *>(p_user_pointer);

				if (unlikely(pd->controller->current_input_buffer_id != UINT32_MAX && pd->controller->current_input_buffer_id >= p_input_id)) {
					// We already have this input, so we don't need it anymore.
					return;
				}

				FrameSnapshot rfs;
				rfs.id = p_input_id;

				const bool found = std::binary_search(
						pd->controller->snapshots.begin(),
						pd->controller->snapshots.end(),
						rfs,
						is_remote_frame_A_older);

				if (!found) {
					rfs.buffer_size_bit = p_input_size_in_bits;
					rfs.inputs_buffer = p_bit_array;
					rfs.received_timestamp = pd->now;

					pd->controller->snapshots.push_back(rfs);

					// Sort the new inserted snapshot.
					std::sort(
							pd->controller->snapshots.begin(),
							pd->controller->snapshots.end(),
							is_remote_frame_A_older);
				}
			});

#ifdef DEBUG_ENABLED
	if (snapshots.empty() == false && current_input_buffer_id != UINT32_MAX) {
		// At this point is guaranteed that the current_input_buffer_id is never
		// greater than the first item contained by `snapshots`.
		CRASH_COND(current_input_buffer_id >= snapshots.front().id);
	}
#endif

	if (!success) {
		SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::receive_input] Failed.");
	}

	return success;
}

ServerController::ServerController(
		NetworkedController *p_node,
		int p_traced_frames) :
		RemotelyControlledController(p_node),
		network_watcher(p_traced_frames, 0),
		consecutive_input_watcher(p_traced_frames, 0) {
}

void ServerController::process(double p_delta) {
	RemotelyControlledController::process(p_delta);

	if (streaming_paused == false) {
		adjust_player_tick_rate(p_delta);
	}
}

void ServerController::on_peer_update(bool p_peer_enabled) {
	if (p_peer_enabled == peer_enabled) {
		// Nothing to updated.
		return;
	}

	// ~~ Reset everything to avoid accumulate old data. ~~
	RemotelyControlledController::on_peer_update(p_peer_enabled);

	additional_fps_notif_timer = 0.0;
	previous_frame_received_timestamp = UINT32_MAX;
	network_watcher.reset(0.0);
	consecutive_input_watcher.reset(0.0);
}

void ServerController::set_frame_input(const FrameSnapshot &p_frame_snapshot, bool p_first_input) {
	// If `previous_frame_received_timestamp` is bigger: the controller was
	// disabled, so nothing to do.
	if (previous_frame_received_timestamp < p_frame_snapshot.received_timestamp) {
		const double physics_ticks_per_second = Engine::get_singleton()->get_physics_ticks_per_second();
		const uint32_t frame_delta_ms = (1.0 / physics_ticks_per_second) * 1000.0;

		const uint32_t receival_time = p_frame_snapshot.received_timestamp - previous_frame_received_timestamp;
		const uint32_t network_time = receival_time > frame_delta_ms ? receival_time - frame_delta_ms : 0;

		network_watcher.push(network_time);
	}

	RemotelyControlledController::set_frame_input(p_frame_snapshot, p_first_input);

	if (p_first_input) {
		// Reset the watcher, as this is the first input.
		network_watcher.reset(0);
		consecutive_input_watcher.reset(0.0);
		previous_frame_received_timestamp = UINT32_MAX;
	} else {
		previous_frame_received_timestamp = p_frame_snapshot.received_timestamp;
	}
}

void ServerController::notify_send_state() {
	// If the notified input is a void buffer, the client is allowed to pause
	// the input streaming. So missing packets are just handled as void inputs.
	if (current_input_buffer_id != UINT32_MAX && node->get_inputs_buffer().size() == 0) {
		streaming_paused = true;
	}
}

bool ServerController::receive_inputs(const Vector<uint8_t> &p_data) {
	Vector<uint8_t> data = p_data;

	const bool success = RemotelyControlledController::receive_inputs(data);

	if (success) {
		uint32_t input_id;
		const bool extraction_success = node->__input_data_get_first_input_id(data, input_id);
		CRASH_COND(!extraction_success);

		// The input parsing succeded on the server, now ping pong this to all the dolls.
		const SyncGroupId sync_group = node->get_scene_synchronizer()->sync_group_get_peer_group(node->peer_id);
		const LocalVector<int> *peers = node->get_scene_synchronizer()->sync_group_get_peers(sync_group);
		if (peers) {
			for (int i = 0; i < int(peers->size()); ++i) {
				const int peer_id = (*peers)[i];
				if (peer_id != node->peer_id) {
					// Convert the `input_id` to peer_id :: input_id.
					// So the peer can properly read the data.
					const uint32_t peer_input_id = convert_input_id_to(peer_id, input_id);

					if (peer_input_id == UINT32_MAX) {
						SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "The `input_id` conversion failed for the peer `" + itos(peer_id) + "`. This is expected untill the client is fully initialized.", true);
						continue;
					}

					node->__input_data_set_first_input_id(data, peer_input_id);

					node->network_interface->rpc(
							peer_id,
							SNAME("_rpc_server_send_inputs"),
							data);
				}
			}
		}
	}

	return success;
}

uint32_t ServerController::convert_input_id_to(int p_other_peer, uint32_t p_input_id) const {
	ERR_FAIL_COND_V(p_input_id == UINT32_MAX, UINT32_MAX);
	CRASH_COND(node->peer_id == p_other_peer); // This function must never be called for the same peer controlling this Character.
	const uint32_t current = get_current_input_id();
	const int64_t diff = int64_t(p_input_id) - int64_t(current);

	// Now find the other peer current_input_id to do the conversion.
	const NetworkedController *controller = node->get_scene_synchronizer()->get_controller_for_peer(p_other_peer, false);
	if (controller == nullptr || controller->get_current_input_id() == UINT32_MAX) {
		return UINT32_MAX;
	}
	return MAX(int64_t(controller->get_current_input_id()) + diff, 0);
}

int ceil_with_tolerance(double p_value, double p_tolerance) {
	return Math::ceil(p_value - p_tolerance);
}

void ServerController::adjust_player_tick_rate(double p_delta) {
	// Update the consecutive inputs.
	{
		int consecutive_inputs = 0;
		for (uint32_t i = 0; i < snapshots.size(); i += 1) {
			if (snapshots[i].id == (current_input_buffer_id + consecutive_inputs + 1)) {
				consecutive_inputs += 1;
			}
		}
		consecutive_input_watcher.push(consecutive_inputs);
	}

	const uint32_t now = OS::get_singleton()->get_ticks_msec();

	if ((additional_fps_notif_timer + node->get_tick_speedup_notification_delay()) < now) {
		// Time to tell the client a new speedup.

		additional_fps_notif_timer = now;

		const real_t min_frames_delay = node->get_min_frames_delay();
		const real_t max_frames_delay = node->get_max_frames_delay();

		// `worst_receival_time` is in ms and indicates the maximum time passed to receive a consecutive
		// input in the last `network_traced_frames` frames.
		const uint32_t worst_receival_time_ms = network_watcher.max();

		const double worst_receival_time = double(worst_receival_time_ms) / 1000.0;

		const int optimal_frame_delay_unclamped = ceil_with_tolerance(
				worst_receival_time / p_delta,
				p_delta * 0.05); // Tolerance of 5% of frame time.

		const int optimal_frame_delay = CLAMP(optimal_frame_delay_unclamped, min_frames_delay, max_frames_delay);

		const int consecutive_inputs = consecutive_input_watcher.average_rounded();

		const int8_t distance_to_optimal = CLAMP(optimal_frame_delay - consecutive_inputs, INT8_MIN, INT8_MAX);

		uint8_t compressed_distance;
		memcpy(
				&compressed_distance,
				&distance_to_optimal,
				sizeof(uint8_t));

#ifdef DEBUG_ENABLED
		const bool debug = ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/debug_server_speedup");
		const int current_frame_delay = consecutive_inputs;
		if (debug) {
			print_line(
					"Worst receival time (ms): `" + itos(worst_receival_time_ms) +
					"` Optimal frame delay: `" + itos(optimal_frame_delay) +
					"` Current frame delay: `" + itos(current_frame_delay) +
					"` Distance to optimal: `" + itos(distance_to_optimal) +
					"`");
		}
		node->emit_signal("client_speedup_adjusted", worst_receival_time_ms, optimal_frame_delay, current_frame_delay, distance_to_optimal);
#endif

		Vector<uint8_t> packet_data;
		packet_data.push_back(compressed_distance);

		node->network_interface->rpc(
				node->network_interface->get_unit_authority(),
				SNAME("_rpc_notify_fps_acceleration"),
				packet_data);
	}
}

AutonomousServerController::AutonomousServerController(
		NetworkedController *p_node) :
		ServerController(p_node, 1) {
}

bool AutonomousServerController::receive_inputs(const Vector<uint8_t> &p_data) {
	SceneSynchronizerDebugger::singleton()->debug_warning(node, "`receive_input` called on the `AutonomousServerController` - If this is called just after `set_server_controlled(true)` is called, you can ignore this warning, as the client is not aware about the switch for a really small window after this function call.");
	return false;
}

int AutonomousServerController::get_inputs_count() const {
	// No input collected by this class.
	return 0;
}

bool AutonomousServerController::fetch_next_input(real_t p_delta) {
	SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "Autonomous server fetch input.", true);

	node->get_inputs_buffer_mut().begin_write(METADATA_SIZE);
	node->get_inputs_buffer_mut().seek(METADATA_SIZE);
	SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(&node->get_network_interface(), SceneSynchronizerDebugger::WRITE);
	node->native_collect_inputs(p_delta, node->get_inputs_buffer_mut());
	SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();
	node->get_inputs_buffer_mut().dry();

	if (unlikely(current_input_buffer_id == UINT32_MAX)) {
		// This is the first input.
		current_input_buffer_id = 0;
	} else {
		// Just advance from now on.
		current_input_buffer_id += 1;
	}

	// The input is always new.
	return true;
}

void AutonomousServerController::adjust_player_tick_rate(double p_delta) {
	// Nothing to do, since the inputs are being collected on the server already.
}

PlayerController::PlayerController(NetworkedController *p_node) :
		Controller(p_node),
		current_input_id(UINT32_MAX),
		input_buffers_counter(0),
		time_bank(0.0),
		acceleration_fps_timer(0.0) {
}

int PlayerController::calculates_sub_ticks(const double p_delta, const double p_iteration_per_seconds) {
	// Extract the frame acceleration:
	// 1. convert the Accelerated Tick Hz to second.
	const double fully_accelerated_delta = 1.0 / (p_iteration_per_seconds + acceleration_fps_speed);

	// 2. Subtract the `accelerated delta - delta` to obtain the acceleration magnitude.
	const double acceleration_delta = ABS(fully_accelerated_delta - p_delta);

	// 3. Avoids overshots by taking the smallest value between `acceleration_delta` and the `remaining timer`.
	const double frame_acceleration_delta = MIN(acceleration_delta, acceleration_fps_timer);

	// Updates the timer by removing the extra accelration.
	acceleration_fps_timer = MAX(acceleration_fps_timer - frame_acceleration_delta, 0.0);

	// Calculates the pretended delta.
	pretended_delta = p_delta + (frame_acceleration_delta * SIGN(acceleration_fps_speed));

	// Add the current delta to the bank
	time_bank += pretended_delta;

	const int sub_ticks = int(time_bank / p_delta);

	time_bank -= static_cast<double>(sub_ticks) * p_delta;
	if (unlikely(time_bank < 0.0)) {
		time_bank = 0.0;
	}

	return sub_ticks;
}

void PlayerController::notify_input_checked(uint32_t p_input_id) {
	if (frames_snapshot.empty() || p_input_id < frames_snapshot.front().id || p_input_id > frames_snapshot.back().id) {
		// The received p_input_id is not known, so nothing to do.
		SceneSynchronizerDebugger::singleton()->debug_error(node, "The received snapshot, with input id: " + itos(p_input_id) + " is not known. This is a bug or someone is trying to hack.");
		return;
	}

	// Remove inputs prior to the known one. We may still need the known one
	// when the stream is paused.
	while (frames_snapshot.empty() == false && frames_snapshot.front().id <= p_input_id) {
		if (frames_snapshot.front().id == p_input_id) {
			streaming_paused = (frames_snapshot.front().buffer_size_bit - METADATA_SIZE) <= 0;
		}
		frames_snapshot.pop_front();
	}

#ifdef DEBUG_ENABLED
	// Unreachable, because the next input have always the next `p_input_id` or empty.
	CRASH_COND(frames_snapshot.empty() == false && (p_input_id + 1) != frames_snapshot.front().id);
#endif

	// Make sure the remaining inputs are 0 sized, if not streaming can't be paused.
	if (streaming_paused) {
		for (auto it = frames_snapshot.begin(); it != frames_snapshot.end(); it += 1) {
			if ((it->buffer_size_bit - METADATA_SIZE) > 0) {
				// Streaming can't be paused.
				streaming_paused = false;
				break;
			}
		}
	}
}

int PlayerController::get_frames_input_count() const {
	return frames_snapshot.size();
}

uint32_t PlayerController::last_known_input() const {
	return get_stored_input_id(-1);
}

uint32_t PlayerController::get_stored_input_id(int p_i) const {
	if (p_i < 0) {
		if (frames_snapshot.empty() == false) {
			return frames_snapshot.back().id;
		} else {
			return UINT32_MAX;
		}
	} else {
		const size_t i = p_i;
		if (i < frames_snapshot.size()) {
			return frames_snapshot[i].id;
		} else {
			return UINT32_MAX;
		}
	}
}

void PlayerController::queue_instant_process(uint32_t p_input_id, int p_index, int p_count) {
	if (p_index >= 0 && p_index < int(frames_snapshot.size())) {
		queued_instant_to_process = p_index;
#ifdef DEBUG_ENABLED
		CRASH_COND(frames_snapshot[p_index].id != p_input_id); // IMPOSSIBLE to trigger - without bugs.
#endif
	} else {
		queued_instant_to_process = -1;
	}
}

bool PlayerController::has_another_instant_to_process_after(int p_i) const {
	if (p_i >= 0 && p_i < int(frames_snapshot.size())) {
		return (p_i + 1) < int(frames_snapshot.size());
	} else {
		return false;
	}
}

void PlayerController::process(double p_delta) {
	if (unlikely(queued_instant_to_process >= 0)) {
		// There is a queued instant. It means the SceneSync is rewinding:
		// instead to fetch a new input, read it from the stored snapshots.
		DataBuffer ib(frames_snapshot[queued_instant_to_process].inputs_buffer);
		ib.shrink_to(METADATA_SIZE, frames_snapshot[queued_instant_to_process].buffer_size_bit - METADATA_SIZE);
		ib.begin_read();
		ib.seek(METADATA_SIZE);
		node->native_controller_process(p_delta, ib);
		queued_instant_to_process = -1;
	} else {
		// Process a new frame.
		// This handles: 1. Read input 2. Process 3. Store the input

		// We need to know if we can accept a new input because in case of bad
		// internet connection we can't keep accumulating inputs forever
		// otherwise the server will differ too much from the client and we
		// introduce virtual lag.
		const bool accept_new_inputs = can_accept_new_inputs();

		if (accept_new_inputs) {
			current_input_id = input_buffers_counter;

			SceneSynchronizerDebugger::singleton()->debug_print(&node->get_network_interface(), "Player process index: " + itos(current_input_id), true);

			node->get_inputs_buffer_mut().begin_write(METADATA_SIZE);

			node->get_inputs_buffer_mut().seek(METADATA_SIZE);

			SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(&node->get_network_interface(), SceneSynchronizerDebugger::WRITE);
			node->native_collect_inputs(p_delta, node->get_inputs_buffer_mut());
			SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();

			// Set metadata data.
			node->get_inputs_buffer_mut().seek(0);
			if (node->get_inputs_buffer().size() > 0) {
				node->get_inputs_buffer_mut().add_bool(true);
				streaming_paused = false;
			} else {
				node->get_inputs_buffer_mut().add_bool(false);
			}
		} else {
			SceneSynchronizerDebugger::singleton()->debug_warning(node, "It's not possible to accept new inputs. Is this lagging?");
		}

		node->get_inputs_buffer_mut().dry();
		node->get_inputs_buffer_mut().begin_read();
		node->get_inputs_buffer_mut().seek(METADATA_SIZE); // Skip meta.

		SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(&node->get_network_interface(), SceneSynchronizerDebugger::READ);
		// The physics process is always emitted, because we still need to simulate
		// the character motion even if we don't store the player inputs.
		node->native_controller_process(p_delta, node->get_inputs_buffer_mut());
		SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();

		node->player_set_has_new_input(false);
		if (accept_new_inputs) {
			if (streaming_paused == false) {
				input_buffers_counter += 1;
				store_input_buffer(current_input_id);
				send_frame_input_buffer_to_server();
				node->player_set_has_new_input(true);
			}
		}
	}
}

uint32_t PlayerController::get_current_input_id() const {
	return current_input_id;
}

bool PlayerController::receive_inputs(const Vector<uint8_t> &p_data) {
	SceneSynchronizerDebugger::singleton()->debug_error(node, "`receive_input` called on the `PlayerServerController` -This function is not supposed to be called on the player controller. Only the server and the doll should receive this.");
	return false;
}

void PlayerController::store_input_buffer(uint32_t p_id) {
	FrameSnapshot inputs;
	inputs.id = p_id;
	inputs.inputs_buffer = node->get_inputs_buffer().get_buffer();
	inputs.buffer_size_bit = node->get_inputs_buffer().size() + METADATA_SIZE;
	inputs.similarity = UINT32_MAX;
	inputs.received_timestamp = UINT32_MAX;
	frames_snapshot.push_back(inputs);
}

void PlayerController::send_frame_input_buffer_to_server() {
	// The packet is composed as follow:
	// - The following four bytes for the first input ID.
	// - Array of inputs:
	// |-- First byte the amount of times this input is duplicated in the packet.
	// |-- Input buffer.

	const size_t inputs_count = MIN(frames_snapshot.size(), static_cast<size_t>(node->get_max_redundant_inputs() + 1));
	CRASH_COND(inputs_count < 1); // Unreachable

#define MAKE_ROOM(p_size)                                              \
	if (cached_packet_data.size() < static_cast<size_t>(ofs + p_size)) \
		cached_packet_data.resize(ofs + p_size);

	int ofs = 0;

	// Let's store the ID of the first snapshot.
	MAKE_ROOM(4);
	const uint32_t first_input_id = frames_snapshot[frames_snapshot.size() - inputs_count].id;
	ofs += encode_uint32(first_input_id, cached_packet_data.ptr() + ofs);

	uint32_t previous_input_id = UINT32_MAX;
	uint32_t previous_input_similarity = UINT32_MAX;
	int previous_buffer_size = 0;
	uint8_t duplication_count = 0;

	DataBuffer *pir_A = memnew(DataBuffer);
	DataBuffer *pir_B = memnew(DataBuffer);
	pir_A->copy(node->get_inputs_buffer().get_buffer());

	// Compose the packets
	for (size_t i = frames_snapshot.size() - inputs_count; i < frames_snapshot.size(); i += 1) {
		bool is_similar = false;

		if (previous_input_id == UINT32_MAX) {
			// This happens for the first input of the packet.
			// Just write it.
			is_similar = false;
		} else if (duplication_count == UINT8_MAX) {
			// Prevent to overflow the `uint8_t`.
			is_similar = false;
		} else {
			if (frames_snapshot[i].similarity != previous_input_id) {
				if (frames_snapshot[i].similarity == UINT32_MAX) {
					// This input was never compared, let's do it now.
					pir_B->copy(frames_snapshot[i].inputs_buffer);
					pir_B->shrink_to(METADATA_SIZE, frames_snapshot[i].buffer_size_bit - METADATA_SIZE);

					pir_A->begin_read();
					pir_A->seek(METADATA_SIZE);
					pir_B->begin_read();
					pir_B->seek(METADATA_SIZE);

					const bool are_different = node->native_are_inputs_different(*pir_A, *pir_B);
					is_similar = !are_different;

				} else if (frames_snapshot[i].similarity == previous_input_similarity) {
					// This input is similar to the previous one, the thing is
					// that the similarity check was done on an older input.
					// Fortunatelly we are able to compare the similarity id
					// and detect its similarity correctly.
					is_similar = true;
				} else {
					// This input is simply different from the previous one.
					is_similar = false;
				}
			} else {
				// These are the same, let's save some space.
				is_similar = true;
			}
		}

		if (current_input_id == previous_input_id) {
			SceneSynchronizerDebugger::singleton()->notify_are_inputs_different_result(node, frames_snapshot[i].id, is_similar);
		} else if (current_input_id == frames_snapshot[i].id) {
			SceneSynchronizerDebugger::singleton()->notify_are_inputs_different_result(node, previous_input_id, is_similar);
		}

		if (is_similar) {
			// This input is similar to the previous one, so just duplicate it.
			duplication_count += 1;
			// In this way, we don't need to compare these frames again.
			frames_snapshot[i].similarity = previous_input_id;

			SceneSynchronizerDebugger::singleton()->notify_input_sent_to_server(node, frames_snapshot[i].id, previous_input_id);

		} else {
			// This input is different from the previous one, so let's
			// finalize the previous and start another one.

			SceneSynchronizerDebugger::singleton()->notify_input_sent_to_server(node, frames_snapshot[i].id, frames_snapshot[i].id);

			if (previous_input_id != UINT32_MAX) {
				// We can finally finalize the previous input
				cached_packet_data[ofs - previous_buffer_size - 1] = duplication_count;
			}

			// Resets the duplication count.
			duplication_count = 0;

			// Writes the duplication_count for this new input
			MAKE_ROOM(1);
			cached_packet_data[ofs] = 0;
			ofs += 1;

			// Write the inputs
			const int buffer_size = frames_snapshot[i].inputs_buffer.get_bytes().size();
			MAKE_ROOM(buffer_size);
			memcpy(
					cached_packet_data.ptr() + ofs,
					frames_snapshot[i].inputs_buffer.get_bytes().ptr(),
					buffer_size);
			ofs += buffer_size;

			// Let's see if we can duplicate this input.
			previous_input_id = frames_snapshot[i].id;
			previous_input_similarity = frames_snapshot[i].similarity;
			previous_buffer_size = buffer_size;

			pir_A->get_buffer_mut() = frames_snapshot[i].inputs_buffer;
			pir_A->shrink_to(METADATA_SIZE, frames_snapshot[i].buffer_size_bit - METADATA_SIZE);
		}
	}

	memdelete(pir_A);
	pir_A = nullptr;
	memdelete(pir_B);
	pir_B = nullptr;

	// Finalize the last added input_buffer.
	cached_packet_data[ofs - previous_buffer_size - 1] = duplication_count;

	// Make the packet data.
	Vector<uint8_t> packet_data;
	packet_data.resize(ofs);

	memcpy(
			packet_data.ptrw(),
			cached_packet_data.ptr(),
			ofs);

	const int server_peer_id = 1;
	node->get_network_interface().rpc(
			server_peer_id,
			SNAME("_rpc_server_send_inputs"),
			packet_data);
}

bool PlayerController::can_accept_new_inputs() const {
	return frames_snapshot.size() < static_cast<size_t>(node->get_player_input_storage_size());
}

DollController::DollController(NetworkedController *p_node) :
		RemotelyControlledController(p_node) {
}

bool DollController::receive_inputs(const Vector<uint8_t> &p_data) {
	const uint32_t now = OS::get_singleton()->get_ticks_msec();
	struct SCParseTmpData {
		DollController *controller;
		NetworkedController *node_controller;
		uint32_t now;
	} tmp = {
		this,
		node,
		now
	};

	const bool success = node->__input_data_parse(
			p_data,
			&tmp,

			// Parse the Input:
			[](void *p_user_pointer, uint32_t p_input_id, int p_input_size_in_bits, const BitArray &p_bit_array) -> void {
				SCParseTmpData *pd = static_cast<SCParseTmpData *>(p_user_pointer);

				CRASH_COND(p_input_id == UINT32_MAX);
				if (pd->controller->last_checked_input >= p_input_id) {
					// This input is already processed.
					return;
				}

				FrameSnapshot rfs;
				rfs.id = p_input_id;

				const bool found = std::binary_search(
						pd->controller->snapshots.begin(),
						pd->controller->snapshots.end(),
						rfs,
						is_remote_frame_A_older);

				if (!found) {
					rfs.buffer_size_bit = p_input_size_in_bits;
					rfs.inputs_buffer = p_bit_array;
					rfs.received_timestamp = pd->now;

					pd->controller->snapshots.push_back(rfs);

					// Sort the new inserted snapshots.
					std::sort(
							pd->controller->snapshots.begin(),
							pd->controller->snapshots.end(),
							is_remote_frame_A_older);
				}
			});

	if (!success) {
		SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[DollController::receive_input] Failed.");
	}

	return success;
}

void DollController::queue_instant_process(uint32_t p_input_id, int p_index, int p_count) {
	if (streaming_paused) {
		return;
	}

	for (size_t i = 0; i < snapshots.size(); ++i) {
		if (snapshots[i].id == p_input_id) {
			queued_instant_to_process = i;
			return;
		}
	}

	SceneSynchronizerDebugger::singleton()->debug_warning(node, "DollController was uable to find the input: " + itos(p_input_id) + " maybe it was never received?", true);
	queued_instant_to_process = snapshots.size();
	return;
}

bool DollController::fetch_next_input(real_t p_delta) {
	if (queued_instant_to_process >= 0) {
		if (queued_instant_to_process >= int(snapshots.size())) {
			return false;
		} else {
			// The SceneSync is rewinding the scene, so let's find the
			set_frame_input(snapshots[queued_instant_to_process], false);
			return true;
		}

	} else {
		if (current_input_buffer_id == UINT32_MAX) {
			if (snapshots.size() > 0) {
				// Anything, as first input is good.
				set_frame_input(snapshots.front(), true);
				return true;
			} else {
				return false;
			}
		} else {
			const uint32_t next_input_id = current_input_buffer_id + 1;
			// Loop the snapshots.
			for (size_t i = 0; i < snapshots.size(); ++i) {
				// Take any NEXT snapshot. Eventually the rewind will fix this.
				// NOTE: the snapshots are sorted.
				if (snapshots[i].id >= next_input_id) {
					set_frame_input(snapshots[i], false);
					return true;
				}
			}
			if (snapshots.size() > 0) {
				set_frame_input(snapshots.back(), false);
				// true anyway, don't stop the processing, just use the input.
				return true;
			}
		}
	}
	return false;
}

void DollController::process(double p_delta) {
	const bool is_new_input = fetch_next_input(p_delta);

	if (is_new_input) {
		SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "Doll process index: " + itos(current_input_buffer_id), true);

		node->get_inputs_buffer_mut().begin_read();
		node->get_inputs_buffer_mut().seek(METADATA_SIZE);
		SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(&node->get_network_interface(), SceneSynchronizerDebugger::READ);
		node->native_controller_process(
				p_delta,
				node->get_inputs_buffer_mut());
		SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();
	}

	queued_instant_to_process = -1;
}

void DollController::notify_input_checked(uint32_t p_input_id) {
	// Remove inputs prior to the known one. We may still need the known one
	// when the stream is paused.
	while (snapshots.empty() == false && snapshots.front().id <= p_input_id) {
		if (snapshots.front().id == p_input_id) {
			streaming_paused = (snapshots.front().buffer_size_bit - METADATA_SIZE) <= 0;
		}
		snapshots.pop_front();
	}

	last_checked_input = p_input_id;
}

NoNetController::NoNetController(NetworkedController *p_node) :
		Controller(p_node),
		frame_id(0) {
}

void NoNetController::process(double p_delta) {
	node->get_inputs_buffer_mut().begin_write(0); // No need of meta in this case.
	SceneSynchronizerDebugger::singleton()->debug_print(&node->get_network_interface(), "Nonet process index: " + itos(frame_id), true);
	SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(&node->get_network_interface(), SceneSynchronizerDebugger::WRITE);
	node->native_collect_inputs(p_delta, node->get_inputs_buffer_mut());
	SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();
	node->get_inputs_buffer_mut().dry();
	node->get_inputs_buffer_mut().begin_read();
	SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(&node->get_network_interface(), SceneSynchronizerDebugger::READ);
	node->native_controller_process(p_delta, node->get_inputs_buffer_mut());
	SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();
	frame_id += 1;
}

uint32_t NoNetController::get_current_input_id() const {
	return frame_id;
}
