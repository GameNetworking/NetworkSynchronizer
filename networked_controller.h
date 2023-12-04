#pragma once

#include "core/core.h"
#include "core/network_interface.h"
#include "core/processor.h"
#include "net_utilities.h"
#include <deque>

NS_NAMESPACE_BEGIN

class SceneSynchronizerBase;
class NetworkInterface;

struct Controller;
struct ServerController;
struct PlayerController;
struct DollController;
struct NoNetController;

class NetworkedControllerManager {
public:
	virtual ~NetworkedControllerManager() {}

	virtual void collect_inputs(double p_delta, class DataBuffer &r_buffer) = 0;
	virtual void controller_process(double p_delta, DataBuffer &p_buffer) = 0;
	virtual bool are_inputs_different(DataBuffer &p_buffer_A, DataBuffer &p_buffer_B) = 0;
	virtual uint32_t count_input_size(DataBuffer &p_buffer) = 0;
};

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
class NetworkedControllerBase {
	friend class NS::SceneSynchronizerBase;
	template <class NetInterfaceClass>
	friend class NetworkedController;
	friend struct RemotelyControlledController;
	friend struct ServerController;
	friend struct PlayerController;
	friend struct DollController;

public:
	enum ControllerType {
		CONTROLLER_TYPE_NULL,
		CONTROLLER_TYPE_NONETWORK,
		CONTROLLER_TYPE_PLAYER,
		CONTROLLER_TYPE_AUTONOMOUS_SERVER,
		CONTROLLER_TYPE_SERVER,
		CONTROLLER_TYPE_DOLL
	};

public:
	NetworkedControllerManager *networked_controller_manager = nullptr;

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

	/// Amount of time an inputs is re-sent to each peer.
	/// Resenging inputs is necessary because the packets may be lost since as
	/// they are sent in an unreliable way.
	int max_redundant_inputs = 6;

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

	ControllerType controller_type = CONTROLLER_TYPE_NULL;
	Controller *controller = nullptr;
	// Created using `memnew` into the constructor:
	// The reason why this is a pointer allocated on the heap explicitely using
	// `memnew` is becouse in Godot 4 GDScript doesn't properly handle non
	// `memnew` created Objects.
	DataBuffer *inputs_buffer = nullptr;

	NS::SceneSynchronizerBase *scene_synchronizer = nullptr;

	bool has_player_new_input = false;

	ObjectNetId net_id = ObjectNetId::NONE;

	NS::NetworkInterface *network_interface = nullptr;

	RpcHandle<const Vector<uint8_t> &> rpc_handle_receive_input;
	RpcHandle<bool> rpc_handle_set_server_controlled;

	NS::PHandler process_handler_process = NS::NullPHandler;

	NS::PHandler event_handler_rewind_frame_begin = NS::NullPHandler;
	NS::PHandler event_handler_state_validated = NS::NullPHandler;
	NS::PHandler event_handler_peer_status_updated = NS::NullPHandler;

public: // -------------------------------------------------------------- Events
	Processor<> event_controller_reset;
	Processor<FrameIndex> event_input_missed;
	Processor<uint32_t /*p_input_worst_receival_time_ms*/, int /*p_optimal_frame_delay*/, int /*p_current_frame_delay*/, int /*p_distance_to_optimal*/> event_client_speedup_adjusted;

private:
	NetworkedControllerBase(NetworkInterface *p_network_interface);

public:
	~NetworkedControllerBase();

public: // -------------------------------------------------------- Manager APIs
	/// Setup the controller
	void setup(NetworkedControllerManager &p_controller_manager);

	/// Prepare the controller for destruction.
	void conclude();

public: // ---------------------------------------------------------------- APIs
	NS::NetworkInterface &get_network_interface() {
		return *network_interface;
	}
	const NS::NetworkInterface &get_network_interface() const {
		return *network_interface;
	}

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

	FrameIndex get_current_frame_index() const;

	const DataBuffer &get_inputs_buffer() const {
		return *inputs_buffer;
	}

	DataBuffer &get_inputs_buffer_mut() {
		return *inputs_buffer;
	}

	void server_set_peer_simulating_this_controller(int p_peer, bool p_simulating);
	bool server_is_peer_simulating_this_controller(int p_peer) const;

	int server_get_associated_peer() const;

public: // -------------------------------------------------------------- Events
	bool has_another_instant_to_process_after(int p_i) const;
	void process(double p_delta);

	/// Returns the server controller or nullptr if this is not a server.
	ServerController *get_server_controller();
	const ServerController *get_server_controller() const;
	ServerController *get_server_controller_unchecked();
	const ServerController *get_server_controller_unchecked() const;
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

	void unregister_with_synchronizer(NS::SceneSynchronizerBase *p_synchronizer);
	void notify_registered_with_synchronizer(NS::SceneSynchronizerBase *p_synchronizer, NS::ObjectData &p_nd);
	NS::SceneSynchronizerBase *get_scene_synchronizer() const;
	bool has_scene_synchronizer() const;

	void on_peer_status_updated(const NS::ObjectData *p_object_data, int p_peer_id, bool p_connected, bool p_enabled);
	void on_state_validated(FrameIndex p_frame_index);
	void on_rewind_frame_begin(FrameIndex p_input_id, int p_index, int p_count);

	/* On server rpc functions. */
	void rpc_receive_inputs(const Vector<uint8_t> &p_data);

	/* On client rpc functions. */
	void rpc_set_server_controlled(bool p_server_controlled);

private:
	void player_set_has_new_input(bool p_has);

public:
	bool player_has_new_input() const;

	bool is_realtime_enabled();

protected:
	void notify_controller_reset();

public:
	bool __input_data_parse(
			const Vector<uint8_t> &p_data,
			void *p_user_pointer,
			void (*p_input_parse)(void *p_user_pointer, FrameIndex p_input_id, int p_input_size_in_bits, const BitArray &p_input));

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
	FrameIndex id;
	BitArray inputs_buffer;
	uint32_t buffer_size_bit;
	FrameIndex similarity;
	/// Local timestamp.
	uint32_t received_timestamp;

	bool operator==(const FrameSnapshot &p_other) const {
		return p_other.id == id;
	}
};

struct Controller {
	NetworkedControllerBase *node;

	Controller(NetworkedControllerBase *p_node) :
			node(p_node) {}

	virtual ~Controller() = default;

	virtual void ready() {}
	virtual FrameIndex get_current_frame_index() const = 0;
	virtual void process(double p_delta) = 0;
	virtual void on_state_validated(FrameIndex p_frame_index) {}

	virtual bool receive_inputs(const Vector<uint8_t> &p_data) { return false; };
	virtual void queue_instant_process(FrameIndex p_input_id, int p_index, int p_count) {}
};

struct RemotelyControlledController : public Controller {
	FrameIndex current_input_buffer_id = FrameIndex::NONE;
	uint32_t ghost_input_count = 0;
	std::deque<FrameSnapshot> snapshots;
	// The stream is paused when the client send an empty buffer.
	bool streaming_paused = false;

	bool peer_enabled = false;

public:
	RemotelyControlledController(NetworkedControllerBase *p_node);

	virtual void on_peer_update(bool p_peer_enabled);

	virtual FrameIndex get_current_frame_index() const override;
	virtual int get_inputs_count() const;
	FrameIndex last_known_frame_index() const;

	/// Fetch the next inputs, returns true if the input is new.
	virtual bool fetch_next_input(real_t p_delta);

	virtual void set_frame_input(const FrameSnapshot &p_frame_snapshot, bool p_first_input);

	virtual void process(double p_delta) override;

	virtual bool receive_inputs(const Vector<uint8_t> &p_data) override;
};

struct ServerController : public RemotelyControlledController {
	float additional_fps_notif_timer = 0;

	std::vector<int> peers_simulating_this_controller;

	uint32_t previous_frame_received_timestamp = UINT32_MAX;
	NS::StatisticalRingBuffer<uint32_t> network_watcher;
	NS::StatisticalRingBuffer<int> consecutive_input_watcher;

	ServerController(
			NetworkedControllerBase *p_node,
			int p_traced_frames);

	virtual void process(double p_delta) override;

	virtual void on_peer_update(bool p_peer_enabled) override;

	virtual void set_frame_input(const FrameSnapshot &p_frame_snapshot, bool p_first_input) override;

	void notify_send_state();

	virtual bool receive_inputs(const Vector<uint8_t> &p_data) override;

	uint32_t convert_input_id_to(int p_other_peer, uint32_t p_input_id) const;

	std::int8_t compute_client_tick_rate_distance_to_optimal();
};

struct AutonomousServerController final : public ServerController {
	AutonomousServerController(
			NetworkedControllerBase *p_node);

	virtual bool receive_inputs(const Vector<uint8_t> &p_data) override;
	virtual int get_inputs_count() const override;
	virtual bool fetch_next_input(real_t p_delta) override;
};

struct PlayerController final : public Controller {
	FrameIndex current_input_id;
	uint32_t input_buffers_counter;
	bool streaming_paused = false;

	std::deque<FrameSnapshot> frames_snapshot;
	LocalVector<uint8_t> cached_packet_data;
	int queued_instant_to_process = -1;

	PlayerController(NetworkedControllerBase *p_node);

	void notify_frame_checked(FrameIndex p_input_id);
	int get_frames_count() const;
	FrameIndex last_known_frame_index() const;
	FrameIndex get_stored_frame_index(int p_i) const;
	virtual FrameIndex get_current_frame_index() const override;

	virtual void queue_instant_process(FrameIndex p_input_id, int p_index, int p_count) override;
	bool has_another_instant_to_process_after(int p_i) const;
	virtual void process(double p_delta) override;
	virtual void on_state_validated(FrameIndex p_frame_index) override;

	virtual bool receive_inputs(const Vector<uint8_t> &p_data) override;

	void store_input_buffer(FrameIndex p_frame_index);

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
struct DollController final : public RemotelyControlledController {
	DollController(NetworkedControllerBase *p_node);

	FrameIndex last_checked_input = { 0 };
	int queued_instant_to_process = -1;

	virtual bool receive_inputs(const Vector<uint8_t> &p_data) override;
	virtual void queue_instant_process(FrameIndex p_input_id, int p_index, int p_count) override;
	virtual bool fetch_next_input(real_t p_delta) override;
	virtual void process(double p_delta) override;
	virtual void on_state_validated(FrameIndex p_frame_index) override;
	void notify_frame_checked(FrameIndex p_input_id);
};

/// This controller is used when the game instance is not a peer of any kind.
/// This controller keeps the workflow as usual so it's possible to use the
/// `NetworkedController` even without network.
struct NoNetController : public Controller {
	FrameIndex frame_id;

	NoNetController(NetworkedControllerBase *p_node);

	virtual void process(double p_delta) override;
	virtual FrameIndex get_current_frame_index() const override;
};

template <class NetInterfaceClass>
class NetworkedController : public NetworkedControllerBase {
	NetInterfaceClass custom_network_interface;

public:
	NetworkedController() :
			NetworkedControllerBase(&custom_network_interface) {}

	NetInterfaceClass &get_network_interface() {
		return custom_network_interface;
	}

	const NetInterfaceClass &get_network_interface() const {
		return custom_network_interface;
	}
};

NS_NAMESPACE_END
