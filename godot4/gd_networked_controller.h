/*************************************************************************/
/*  gd_networked_controller.h                                            */
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

#ifndef GD_NETWORKED_CONTROLLER_H
#define GD_NETWORKED_CONTROLLER_H

#include "modules/network_synchronizer/core/processor.h"
#include "modules/network_synchronizer/godot4/gd_network_interface.h"
#include "modules/network_synchronizer/networked_controller.h"
#include "scene/main/node.h"

/// The `NetworkedController` is responsible to sync the `Player` inputs between
/// the peers. This allows to control a character, or an object with high precision
/// and replicates that movement on all connected peers.
///
/// The `NetworkedController` will sync inputs, based on those will perform
/// operations.
/// The result of these operations, are guaranteed to be the same accross the
/// peers, if we stay under the assumption that the initial state is the same.
///
/// Is possible to use the `SceneSynchronizer` to keep the state in sync with the
/// peers.
///
// # Implementation details
//
// The `NetworkedController` perform different operations depending where it's
// instantiated.
// The most important part is inside the `PlayerController`, `ServerController`,
// `DollController`, `NoNetController`.
class GdNetworkedController : public Node, public NS::NetworkedControllerManager {
	GDCLASS(GdNetworkedController, Node);

public:
	GDVIRTUAL2(_collect_inputs, real_t, DataBuffer *);
	GDVIRTUAL2(_controller_process, real_t, DataBuffer *);
	GDVIRTUAL2R(bool, _are_inputs_different, DataBuffer *, DataBuffer *);
	GDVIRTUAL1RC(int, _count_input_size, DataBuffer *);

private:
	NS::NetworkedController<GdNetworkInterface> networked_controller;

	NS::PHandler event_handler_controller_reset = NS::NullPHandler;
	NS::PHandler event_handler_input_missed = NS::NullPHandler;
	NS::PHandler event_handler_client_speedup_adjusted = NS::NullPHandler;

public:
	static void _bind_methods();

public:
	GdNetworkedController();
	~GdNetworkedController();

	void _notification(int p_what);

	NS::NetworkedControllerBase *get_networked_controller() { return &networked_controller; }
	const NS::NetworkedControllerBase *get_networked_controller() const { return &networked_controller; }

	void set_server_controlled(bool p_server_controlled);
	bool get_server_controlled() const;

	void set_max_redundant_inputs(int p_max);
	int get_max_redundant_inputs() const;

	void set_network_traced_frames(int p_size);
	int get_network_traced_frames() const;

	void set_min_frames_delay(int p_val);
	int get_min_frames_delay() const;

	void set_max_frames_delay(int p_val);
	int get_max_frames_delay() const;

	uint32_t get_current_input_id() const;

	bool is_networking_initialized() const;
	bool is_server_controller() const;
	bool is_player_controller() const;
	bool is_doll_controller() const;
	bool is_nonet_controller() const;

public: // ----------------------------------------------------------- Interface
	virtual void collect_inputs(double p_delta, DataBuffer &r_buffer) override;
	virtual void controller_process(double p_delta, DataBuffer &p_buffer) override;
	virtual bool are_inputs_different(DataBuffer &p_buffer_A, DataBuffer &p_buffer_B) override;
	virtual uint32_t count_input_size(DataBuffer &p_buffer) override;

	// This funtion is used to sync data betweend the server and the client.
	void _rpc_net_sync_reliable(const Vector<uint8_t> &p_args);
	// This funtion is used to sync data betweend the server and the client.
	void _rpc_net_sync_unreliable(const Vector<uint8_t> &p_args);

public:
	virtual void validate_script_implementation();
};

#endif
