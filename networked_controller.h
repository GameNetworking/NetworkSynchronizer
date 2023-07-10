/*************************************************************************/
/*  networked_controller.h                                               */
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

#include "scene/main/node.h"

#include "data_buffer.h"
#include "net_utilities.h"
#include <deque>

#ifndef NETWORKED_CONTROLLER_H
#define NETWORKED_CONTROLLER_H

class SceneSynchronizer;
struct Controller;
struct ServerController;
struct PlayerController;
struct DollController;
struct NoNetController;

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
class NetworkedController : public Node {
	GDCLASS(NetworkedController, Node);

	friend class SceneSynchronizer;
	friend class RemotelyControlledController;
	friend class ServerController;

public:
	enum ControllerType {
		CONTROLLER_TYPE_NULL,
		CONTROLLER_TYPE_NONETWORK,
		CONTROLLER_TYPE_PLAYER,
		CONTROLLER_TYPE_AUTONOMOUS_SERVER,
		CONTROLLER_TYPE_SERVER,
		CONTROLLER_TYPE_DOLL
	};

	GDVIRTUAL2(_collect_inputs, real_t, DataBuffer *);
	GDVIRTUAL2(_controller_process, real_t, DataBuffer *);
	GDVIRTUAL2R(bool, _are_inputs_different, DataBuffer *, DataBuffer *);
	GDVIRTUAL1RC(int, _count_input_size, DataBuffer *);

private:
	/// When `true`, this controller is controlled by the server: All the clients
	/// see it as a `Doll`.
	/// This property is really useful to implement bots (Character controlled by
	/// the AI).
	///
	/// NOTICE: Generally you specify this property on the editor, in addition
	/// it's possible to change this at runtime: this will cause the server to
	/// notify all the clients; so the switch is not immediate. This feature can be
	/// used to switch the Character possession between the AI (Server) and
	/// PlayerController (Client) without the need to re-instantiate the Character.
	bool server_controlled = false;

	/// The input storage size is used to cap the amount of inputs collected by
	/// the `PlayerController`.
	///
	/// The server sends a message, to all the connected peers, notifing its
	/// status at a fixed interval.
	/// The peers, after receiving this update, removes all the old inputs until
	/// that moment.
	///
	/// `input_storage_size`: is too small, the player may stop collect
	/// - Too small value makes the `PlayerController` stop collecting inputs
	///   too early, in case of lag.
	/// - Too big values may introduce too much latency, because the player keep
	///   pushing new inputs without receiving the server snapshot.
	///
	/// With 60 iteration per seconds a good value is `180` (60 * 3) so the
	/// `PlayerController` can be at max 3 seconds ahead the `ServerController`.
	int player_input_storage_size = 180;

	/// Amount of time an inputs is re-sent to each peer.
	/// Resenging inputs is necessary because the packets may be lost since as
	/// they are sent in an unreliable way.
	int max_redundant_inputs = 6;

	/// Time in seconds between each `tick_speedup` that the server sends to the
	/// client. In ms.
	int tick_speedup_notification_delay = 600;

	/// The connection quality is established by watching the time passed
	/// between each input is received.
	/// The more this time is the same the more the connection health is good.
	///
	/// The `network_traced_frames` defines how many frames have
	/// to be used to establish the connection quality.
	/// - Big values make the mechanism too slow.
	/// - Small values make the mechanism too sensible.
	int network_traced_frames = 120;

	/// The `ServerController` will try to keep a margin of error, so that
	/// network oscillations doesn't leave the `ServerController` without
	/// inputs.
	///
	/// This margin of error is called `optimal_frame_delay` and it changes
	/// depending on the connection health:
	/// it can go from `min_frames_delay` to `max_frames_delay`.
	int min_frames_delay = 2;
	int max_frames_delay = 7;

	/// Amount of additional frames produced per second.
	double tick_acceleration = 5.0;

	ControllerType controller_type = CONTROLLER_TYPE_NULL;
	Controller *controller = nullptr;
	// Created using `memnew` into the constructor:
	// The reason why this is a pointer allocated on the heap explicitely using
	// `memnew` is becouse in Godot 4 GDScript doesn't properly handle non
	// `memnew` created Objects.
	DataBuffer *inputs_buffer = nullptr;

	SceneSynchronizer *scene_synchronizer = nullptr;

	bool has_player_new_input = false;

	// Peer controlling this controller.
	int peer_id = -1;

	NetNodeId node_id = NetID_NONE;

public:
	static void _bind_methods();

public:
	NetworkedController();
	~NetworkedController();

	void set_server_controlled(bool p_server_controlled);
	bool get_server_controlled() const;

	void set_player_input_storage_size(int p_size);
	int get_player_input_storage_size() const;

	void set_max_redundant_inputs(int p_max);
	int get_max_redundant_inputs() const;

	void set_tick_speedup_notification_delay(int p_delay_in_ms);
	int get_tick_speedup_notification_delay() const;

	void set_network_traced_frames(int p_size);
	int get_network_traced_frames() const;

	void set_min_frames_delay(int p_val);
	int get_min_frames_delay() const;

	void set_max_frames_delay(int p_val);
	int get_max_frames_delay() const;

	void set_tick_acceleration(double p_acceleration);
	double get_tick_acceleration() const;

	uint32_t get_current_input_id() const;

	const DataBuffer &get_inputs_buffer() const {
		return *inputs_buffer;
	}

	DataBuffer &get_inputs_buffer_mut() {
		return *inputs_buffer;
	}

	/// Returns the pretended delta used by the player.
	real_t player_get_pretended_delta() const;

	virtual void validate_script_implementation();
	virtual void native_collect_inputs(double p_delta, DataBuffer &r_buffer);
	virtual void native_controller_process(double p_delta, DataBuffer &p_buffer);
	virtual bool native_are_inputs_different(DataBuffer &p_buffer_A, DataBuffer &p_buffer_B);
	virtual uint32_t native_count_input_size(DataBuffer &p_buffer);

	bool has_another_instant_to_process_after(int p_i) const;
	void process(double p_delta);

	/// Returns the server controller or nullptr if this is not a server.
	ServerController *get_server_controller();
	const ServerController *get_server_controller() const;
	/// Returns the player controller or nullptr if this is not a player.
	PlayerController *get_player_controller();
	const PlayerController *get_player_controller() const;
	/// Returns the doll controller or nullptr if this is not a doll.
	DollController *get_doll_controller();
	const DollController *get_doll_controller() const;
	/// Returns the no net controller or nullptr if this is not a no net.
	NoNetController *get_nonet_controller();
	const NoNetController *get_nonet_controller() const;

	bool is_networking_initialized() const;
	bool is_server_controller() const;
	bool is_player_controller() const;
	bool is_doll_controller() const;
	bool is_nonet_controller() const;

public:
	void set_inputs_buffer(const BitArray &p_new_buffer, uint32_t p_metadata_size_in_bit, uint32_t p_size_in_bit);

	void notify_registered_with_synchronizer(SceneSynchronizer *p_synchronizer);
	SceneSynchronizer *get_scene_synchronizer() const;
	bool has_scene_synchronizer() const;

	void on_peer_status_updated(Node *p_node, NetNodeId p_id, int p_peer_id, bool p_connected, bool p_enabled);
	void on_state_validated(uint32_t p_input_id);
	void on_rewind_frame_begin(uint32_t p_input_id, int p_index, int p_count);

	/* On server rpc functions. */
	void _rpc_server_send_inputs(const Vector<uint8_t> &p_data);

	/* On client rpc functions. */
	void _rpc_set_server_controlled(bool p_server_controlled);
	void _rpc_notify_fps_acceleration(const Vector<uint8_t> &p_data);

	void player_set_has_new_input(bool p_has);
	bool player_has_new_input() const;

	bool is_realtime_enabled();

protected:
	void _notification(int p_what);
	void notify_controller_reset();

public:
	bool __input_data_parse(
			const Vector<uint8_t> &p_data,
			void *p_user_pointer,
			void (*p_input_parse)(void *p_user_pointer, uint32_t p_input_id, int p_input_size_in_bits, const BitArray &p_input));

	/// This function is able to get the InputId for this buffer.
	bool __input_data_get_first_input_id(
			const Vector<uint8_t> &p_data,
			uint32_t &p_input_id) const;

	/// This function is able to set a new InputId for this buffer.
	bool __input_data_set_first_input_id(
			Vector<uint8_t> &p_data,
			uint32_t p_input_id);
};

struct FrameSnapshot {
	uint32_t id;
	BitArray inputs_buffer;
	uint32_t buffer_size_bit;
	uint32_t similarity;
	/// Local timestamp.
	uint32_t received_timestamp;

	bool operator==(const FrameSnapshot &p_other) const {
		return p_other.id == id;
	}
};

struct Controller {
	NetworkedController *node;

	Controller(NetworkedController *p_node) :
			node(p_node) {}

	virtual ~Controller() = default;

	virtual void ready() {}
	virtual uint32_t get_current_input_id() const = 0;
	virtual void process(double p_delta) = 0;

	virtual bool receive_inputs(const Vector<uint8_t> &p_data){};
	virtual void notify_input_checked(uint32_t p_input_id) {}
	virtual void queue_instant_process(uint32_t p_input_id, int p_index, int p_count) {}
};

struct RemotelyControlledController : public Controller {
	uint32_t current_input_buffer_id = UINT32_MAX;
	uint32_t ghost_input_count = 0;
	std::deque<FrameSnapshot> snapshots;
	// The stream is paused when the client send an empty buffer.
	bool streaming_paused = false;

	bool peer_enabled = false;

public:
	RemotelyControlledController(NetworkedController *p_node);

	virtual void on_peer_update(bool p_peer_enabled);

	virtual uint32_t get_current_input_id() const override;
	virtual int get_inputs_count() const;
	uint32_t last_known_input() const;

	/// Fetch the next inputs, returns true if the input is new.
	virtual bool fetch_next_input(real_t p_delta);

	virtual void set_frame_input(const FrameSnapshot &p_frame_snapshot, bool p_first_input);

	virtual void process(double p_delta) override;

	virtual bool receive_inputs(const Vector<uint8_t> &p_data) override;
};

struct ServerController : public RemotelyControlledController {
	uint32_t additional_fps_notif_timer = 0;

	uint32_t previous_frame_received_timestamp = UINT32_MAX;
	NetUtility::StatisticalRingBuffer<uint32_t> network_watcher;
	NetUtility::StatisticalRingBuffer<int> consecutive_input_watcher;

	ServerController(
			NetworkedController *p_node,
			int p_traced_frames);

	virtual void process(double p_delta) override;

	virtual void on_peer_update(bool p_peer_enabled) override;

	virtual void set_frame_input(const FrameSnapshot &p_frame_snapshot, bool p_first_input) override;

	void notify_send_state();

	virtual bool receive_inputs(const Vector<uint8_t> &p_data) override;

	uint32_t convert_input_id_to(int p_other_peer, uint32_t p_input_id) const;

	/// This function updates the `tick_additional_fps` so that the `frames_inputs`
	/// size is enough to reduce the missing packets to 0.
	///
	/// When the internet connection is bad, the packets need more time to arrive.
	/// To heal this problem, the server tells the client to speed up a little bit
	/// so it send the inputs a bit earlier than the usual.
	///
	/// If the `frames_inputs` size is too big the input lag between the client and
	/// the server is artificial and no more dependent on the internet. For this
	/// reason the server tells the client to slowdown so to keep the `frames_inputs`
	/// size moderate to the needs.
	virtual void adjust_player_tick_rate(double p_delta);
};

struct AutonomousServerController : public ServerController {
	AutonomousServerController(
			NetworkedController *p_node);

	virtual bool receive_inputs(const Vector<uint8_t> &p_data) override;
	virtual int get_inputs_count() const override;
	virtual bool fetch_next_input(real_t p_delta) override;
	virtual void adjust_player_tick_rate(double p_delta) override;
};

struct PlayerController : public Controller {
	uint32_t current_input_id;
	uint32_t input_buffers_counter;
	double time_bank;
	double acceleration_fps_speed = 0.0;
	double acceleration_fps_timer = 1.0;
	bool streaming_paused = false;
	double pretended_delta = 1.0;

	std::deque<FrameSnapshot> frames_snapshot;
	LocalVector<uint8_t> cached_packet_data;
	int queued_instant_to_process = -1;

	PlayerController(NetworkedController *p_node);

	/// Returns the amount of frames to process for this frame.
	int calculates_sub_ticks(const double p_delta, const double p_iteration_per_seconds);
	virtual void notify_input_checked(uint32_t p_input_id) override;
	int get_frames_input_count() const;
	uint32_t last_known_input() const;
	uint32_t get_stored_input_id(int p_i) const;
	virtual uint32_t get_current_input_id() const override;

	virtual void queue_instant_process(uint32_t p_input_id, int p_index, int p_count) override;
	bool has_another_instant_to_process_after(int p_i) const;
	virtual void process(double p_delta) override;

	virtual bool receive_inputs(const Vector<uint8_t> &p_data) override;

	void store_input_buffer(uint32_t p_id);

	/// Sends an unreliable packet to the server, containing a packed array of
	/// frame snapshots.
	void send_frame_input_buffer_to_server();

	bool can_accept_new_inputs() const;
};

/// The doll controller is kind of special controller, it's using a
/// `ServerController` + `MastertController`.
/// The `DollController` receives inputs from the client as the server does,
/// and fetch them exactly like the server.
/// After the execution of the inputs, the puppet start to act like the player,
/// because it wait the player status from the server to correct its motion.
struct DollController : public RemotelyControlledController {
	DollController(NetworkedController *p_node);

	uint32_t last_checked_input = 0;
	int64_t virtual_current_input = 0;
	int queued_instant_to_process = -1;

	virtual bool receive_inputs(const Vector<uint8_t> &p_data) override;
	virtual void queue_instant_process(uint32_t p_input_id, int p_index, int p_count) override;
	virtual bool fetch_next_input(real_t p_delta) override;
	virtual void process(double p_delta) override;
	virtual void notify_input_checked(uint32_t p_input_id) override;
};

/// This controller is used when the game instance is not a peer of any kind.
/// This controller keeps the workflow as usual so it's possible to use the
/// `NetworkedController` even without network.
struct NoNetController : public Controller {
	uint32_t frame_id;

	NoNetController(NetworkedController *p_node);

	virtual void process(double p_delta) override;
	virtual uint32_t get_current_input_id() const override;
};

#endif
