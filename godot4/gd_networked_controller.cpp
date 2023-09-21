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

#include "gd_networked_controller.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/error/error_macros.h"
#include "core/io/marshalls.h"
#include "gd_network_interface.h"
#include "modules/network_synchronizer/core/processor.h"
#include "modules/network_synchronizer/scene_synchronizer_debugger.h"
#include "scene/main/multiplayer_api.h"

#define METADATA_SIZE 1

void GdNetworkedController::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_server_controlled", "server_controlled"), &GdNetworkedController::set_server_controlled);
	ClassDB::bind_method(D_METHOD("get_server_controlled"), &GdNetworkedController::get_server_controlled);

	ClassDB::bind_method(D_METHOD("set_player_input_storage_size", "size"), &GdNetworkedController::set_player_input_storage_size);
	ClassDB::bind_method(D_METHOD("get_player_input_storage_size"), &GdNetworkedController::get_player_input_storage_size);

	ClassDB::bind_method(D_METHOD("set_max_redundant_inputs", "max_redundant_inputs"), &GdNetworkedController::set_max_redundant_inputs);
	ClassDB::bind_method(D_METHOD("get_max_redundant_inputs"), &GdNetworkedController::get_max_redundant_inputs);

	ClassDB::bind_method(D_METHOD("set_tick_speedup_notification_delay", "delay_in_ms"), &GdNetworkedController::set_tick_speedup_notification_delay);
	ClassDB::bind_method(D_METHOD("get_tick_speedup_notification_delay"), &GdNetworkedController::get_tick_speedup_notification_delay);

	ClassDB::bind_method(D_METHOD("set_network_traced_frames", "size"), &GdNetworkedController::set_network_traced_frames);
	ClassDB::bind_method(D_METHOD("get_network_traced_frames"), &GdNetworkedController::get_network_traced_frames);

	ClassDB::bind_method(D_METHOD("set_min_frames_delay", "val"), &GdNetworkedController::set_min_frames_delay);
	ClassDB::bind_method(D_METHOD("get_min_frames_delay"), &GdNetworkedController::get_min_frames_delay);

	ClassDB::bind_method(D_METHOD("set_max_frames_delay", "val"), &GdNetworkedController::set_max_frames_delay);
	ClassDB::bind_method(D_METHOD("get_max_frames_delay"), &GdNetworkedController::get_max_frames_delay);

	ClassDB::bind_method(D_METHOD("set_tick_acceleration", "acceleration"), &GdNetworkedController::set_tick_acceleration);
	ClassDB::bind_method(D_METHOD("get_tick_acceleration"), &GdNetworkedController::get_tick_acceleration);

	ClassDB::bind_method(D_METHOD("get_current_input_id"), &GdNetworkedController::get_current_input_id);

	ClassDB::bind_method(D_METHOD("player_get_pretended_delta"), &GdNetworkedController::player_get_pretended_delta);

	ClassDB::bind_method(D_METHOD("is_server_controller"), &GdNetworkedController::is_server_controller);
	ClassDB::bind_method(D_METHOD("is_player_controller"), &GdNetworkedController::is_player_controller);
	ClassDB::bind_method(D_METHOD("is_doll_controller"), &GdNetworkedController::is_doll_controller);
	ClassDB::bind_method(D_METHOD("is_nonet_controller"), &GdNetworkedController::is_nonet_controller);

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

	ClassDB::bind_method(D_METHOD("_rpc_server_send_inputs"), &GdNetworkedController::_rpc_server_send_inputs);
	ClassDB::bind_method(D_METHOD("_rpc_set_server_controlled"), &GdNetworkedController::_rpc_set_server_controlled);
	ClassDB::bind_method(D_METHOD("_rpc_notify_fps_acceleration"), &GdNetworkedController::_rpc_notify_fps_acceleration);

	ADD_SIGNAL(MethodInfo("controller_reset"));
	ADD_SIGNAL(MethodInfo("input_missed", PropertyInfo(Variant::INT, "missing_input_id")));
	ADD_SIGNAL(MethodInfo("client_speedup_adjusted", PropertyInfo(Variant::INT, "input_worst_receival_time_ms"), PropertyInfo(Variant::INT, "optimal_frame_delay"), PropertyInfo(Variant::INT, "current_frame_delay"), PropertyInfo(Variant::INT, "distance_to_optimal")));
}

GdNetworkedController::GdNetworkedController() :
		Node() {
	Dictionary rpc_config_reliable;
	rpc_config_reliable["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
	rpc_config_reliable["call_local"] = false;
	rpc_config_reliable["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;

	Dictionary rpc_config_unreliable = rpc_config_reliable;
	rpc_config_unreliable["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_UNRELIABLE;

	rpc_config(SNAME("_rpc_server_send_inputs"), rpc_config_unreliable);
	rpc_config(SNAME("_rpc_set_server_controlled"), rpc_config_reliable);
	rpc_config(SNAME("_rpc_notify_fps_acceleration"), rpc_config_unreliable);

	event_handler_controller_reset =
			networked_controller.event_controller_reset.bind([this]() -> void {
				emit_signal("controller_reset");
			});

	event_handler_input_missed =
			networked_controller.event_input_missed.bind([this](uint32_t p_input_id) -> void {
				emit_signal("input_missed", p_input_id);
			});

	event_handler_client_speedup_adjusted =
			networked_controller.event_client_speedup_adjusted.bind(
					[this](uint32_t p_input_worst_receival_time_ms,
							int p_optimal_frame_delay,
							int p_current_frame_delay,
							int p_distance_to_optimal) -> void {
						emit_signal(
								"client_speedup_adjusted",
								p_input_worst_receival_time_ms,
								p_optimal_frame_delay,
								p_current_frame_delay,
								p_distance_to_optimal);
					});
}

GdNetworkedController::~GdNetworkedController() {
	networked_controller.event_controller_reset.unbind(event_handler_controller_reset);
	networked_controller.event_input_missed.unbind(event_handler_input_missed);
	networked_controller.event_client_speedup_adjusted.unbind(event_handler_client_speedup_adjusted);
	event_handler_controller_reset = NS::NullPHandler;
	event_handler_input_missed = NS::NullPHandler;
	event_handler_client_speedup_adjusted = NS::NullPHandler;
}

void GdNetworkedController::_notification(int p_what) {
	switch (p_what) {
		/*
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
		*/
		case NOTIFICATION_ENTER_TREE: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}

			GdNetworkInterface *ni = memnew(GdNetworkInterface);
			ni->owner = this;
			networked_controller.setup(
					*ni,
					*this);

		} break;
#ifdef DEBUG_ENABLED
		case NOTIFICATION_READY: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}

			validate_script_implementation();
		} break;
#endif
		case NOTIFICATION_EXIT_TREE: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}

			NS::NetworkInterface &ni = networked_controller.get_network_interface();
			networked_controller.conclude();
			memdelete(&ni);
		}
	}
}

void GdNetworkedController::set_server_controlled(bool p_server_controlled) {
	networked_controller.set_server_controlled(p_server_controlled);

#ifdef DEBUG_ENABLED
	if (GDVIRTUAL_IS_OVERRIDDEN(_collect_inputs) == false && networked_controller.get_server_controlled() == false) {
		WARN_PRINT("In your script you must inherit the virtual method `_collect_inputs` to correctly use the `NetworkedController`.");
	}

	if (GDVIRTUAL_IS_OVERRIDDEN(_controller_process) == false && networked_controller.get_server_controlled() == false) {
		WARN_PRINT("In your script you must inherit the virtual method `_controller_process` to correctly use the `NetworkedController`.");
	}

	if (GDVIRTUAL_IS_OVERRIDDEN(_are_inputs_different) == false && networked_controller.get_server_controlled() == false) {
		WARN_PRINT("In your script you must inherit the virtual method `_are_inputs_different` to correctly use the `NetworkedController`.");
	}

	if (GDVIRTUAL_IS_OVERRIDDEN(_count_input_size) == false && networked_controller.get_server_controlled() == false) {
		WARN_PRINT("In your script you must inherit the virtual method `_count_input_size` to correctly use the `NetworkedController`.");
	}
#endif
}

bool GdNetworkedController::get_server_controlled() const {
	return networked_controller.get_server_controlled();
}

void GdNetworkedController::set_player_input_storage_size(int p_size) {
	networked_controller.set_player_input_storage_size(p_size);
}

int GdNetworkedController::get_player_input_storage_size() const {
	return networked_controller.get_player_input_storage_size();
}

void GdNetworkedController::set_max_redundant_inputs(int p_max) {
	networked_controller.set_max_redundant_inputs(p_max);
}

int GdNetworkedController::get_max_redundant_inputs() const {
	return networked_controller.get_max_redundant_inputs();
}

void GdNetworkedController::set_tick_speedup_notification_delay(int p_delay) {
	networked_controller.set_tick_speedup_notification_delay(p_delay);
}

int GdNetworkedController::get_tick_speedup_notification_delay() const {
	return networked_controller.get_tick_speedup_notification_delay();
}

void GdNetworkedController::set_network_traced_frames(int p_size) {
	networked_controller.set_network_traced_frames(p_size);
}

int GdNetworkedController::get_network_traced_frames() const {
	return networked_controller.get_network_traced_frames();
}

void GdNetworkedController::set_min_frames_delay(int p_val) {
	networked_controller.set_min_frames_delay(p_val);
}

int GdNetworkedController::get_min_frames_delay() const {
	return networked_controller.get_min_frames_delay();
}

void GdNetworkedController::set_max_frames_delay(int p_val) {
	networked_controller.set_max_frames_delay(p_val);
}

int GdNetworkedController::get_max_frames_delay() const {
	return networked_controller.get_max_frames_delay();
}

void GdNetworkedController::set_tick_acceleration(double p_acceleration) {
	networked_controller.set_tick_acceleration(p_acceleration);
}

double GdNetworkedController::get_tick_acceleration() const {
	return networked_controller.get_tick_acceleration();
}

uint32_t GdNetworkedController::get_current_input_id() const {
	return networked_controller.get_current_input_id();
}

real_t GdNetworkedController::player_get_pretended_delta() const {
	return networked_controller.player_get_pretended_delta();
}

bool GdNetworkedController::is_networking_initialized() const {
	return networked_controller.is_networking_initialized();
}

bool GdNetworkedController::is_server_controller() const {
	return networked_controller.is_server_controller();
}

bool GdNetworkedController::is_player_controller() const {
	return networked_controller.is_player_controller();
}

bool GdNetworkedController::is_doll_controller() const {
	return networked_controller.is_doll_controller();
}

bool GdNetworkedController::is_nonet_controller() const {
	return networked_controller.is_nonet_controller();
}

void GdNetworkedController::collect_inputs(double p_delta, DataBuffer &r_buffer) {
	PROFILE_NODE

	const bool executed = GDVIRTUAL_CALL(_collect_inputs, p_delta, &r_buffer);
	if (executed == false) {
		NET_DEBUG_ERR("The function _collect_inputs was not executed!");
	}
}

void GdNetworkedController::controller_process(double p_delta, DataBuffer &p_buffer) {
	PROFILE_NODE

	const bool executed = GDVIRTUAL_CALL(
			_controller_process,
			p_delta,
			&p_buffer);

	if (executed == false) {
		NET_DEBUG_ERR("The function _controller_process was not executed!");
	}
}

bool GdNetworkedController::are_inputs_different(DataBuffer &p_buffer_A, DataBuffer &p_buffer_B) {
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

uint32_t GdNetworkedController::count_input_size(DataBuffer &p_buffer) {
	PROFILE_NODE

	int input_size = 0;
	const bool executed = GDVIRTUAL_CALL(_count_input_size, &p_buffer, input_size);
	if (executed == false) {
		NET_DEBUG_ERR("The function `_count_input_size` was not executed.");
	}
	return uint32_t(input_size >= 0 ? input_size : 0);
}

void GdNetworkedController::rpc_send__server_send_inputs(int p_peer_id, const Vector<uint8_t> &p_data) {
	rpc_id(
			p_peer_id,
			SNAME("_rpc_server_send_inputs"),
			p_data);
}

void GdNetworkedController::rpc_send__set_server_controlled(int p_peer_id, bool p_server_controlled) {
	rpc_id(
			p_peer_id,
			SNAME("_rpc_set_server_controlled"),
			p_server_controlled);
}

void GdNetworkedController::rpc_send__notify_fps_acceleration(int p_peer_id, const Vector<uint8_t> &p_data) {
	rpc_id(
			p_peer_id,
			SNAME("_rpc_notify_fps_acceleration"),
			p_data);
}

void GdNetworkedController::_rpc_server_send_inputs(const Vector<uint8_t> &p_data) {
	networked_controller.rpc_receive__server_send_inputs(p_data);
}

void GdNetworkedController::_rpc_set_server_controlled(bool p_server_controlled) {
	networked_controller.rpc_receive__set_server_controlled(p_server_controlled);
}

void GdNetworkedController::_rpc_notify_fps_acceleration(const Vector<uint8_t> &p_data) {
	networked_controller.rpc_receive__notify_fps_acceleration(p_data);
}

void GdNetworkedController::validate_script_implementation() {
	ERR_FAIL_COND_MSG(has_method("_collect_inputs") == false && networked_controller.get_server_controlled() == false, "In your script you must inherit the virtual method `_collect_inputs` to correctly use the `GdNetworkedController`.");
	ERR_FAIL_COND_MSG(has_method("_controller_process") == false && networked_controller.get_server_controlled() == false, "In your script you must inherit the virtual method `_controller_process` to correctly use the `GdNetworkedController`.");
	ERR_FAIL_COND_MSG(has_method("_are_inputs_different") == false && networked_controller.get_server_controlled() == false, "In your script you must inherit the virtual method `_are_inputs_different` to correctly use the `GdNetworkedController`.");
	ERR_FAIL_COND_MSG(has_method("_count_input_size") == false && networked_controller.get_server_controlled() == false, "In your script you must inherit the virtual method `_count_input_size` to correctly use the `GdNetworkedController`.");
}
