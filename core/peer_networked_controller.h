#pragma once

#include "../data_buffer.h"
#include "core.h"
#include "net_utilities.h"
#include "processor.h"
#include "snapshot.h"
#include <deque>

NS_NAMESPACE_BEGIN

class SceneSynchronizerBase;
class NetworkInterface;

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
class PeerNetworkedController final {
	friend class NS::SceneSynchronizerBase;
	template <class NetInterfaceClass>
	friend class NetworkedController;
	friend struct RemotelyControlledController;
	friend struct ServerController;
	friend struct PlayerController;
	friend struct DollController;
	friend struct AutonomousServerController;
	friend struct NoNetController;

public:
	enum ControllerType {
		CONTROLLER_TYPE_NULL,
		CONTROLLER_TYPE_NONETWORK,
		CONTROLLER_TYPE_PLAYER,
		CONTROLLER_TYPE_AUTONOMOUS_SERVER,
		CONTROLLER_TYPE_SERVER,
		CONTROLLER_TYPE_DOLL
	};

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

	// The peer associated to this NetController.
	int authority_peer = -1;

	ControllerType controller_type = CONTROLLER_TYPE_NULL;
	Controller *controller = nullptr;
	// Created using `memnew` into the constructor:
	// The reason why this is a pointer allocated on the heap explicitely using
	// `memnew` is becouse in Godot 4 GDScript doesn't properly handle non
	// `memnew` created Objects.
	DataBuffer *inputs_buffer = nullptr;

	NS::SceneSynchronizerBase *scene_synchronizer = nullptr;

	bool are_controllable_objects_sorted = false;
	std::vector<ObjectData *> _sorted_controllable_objects;

	bool has_player_new_input = false;

	NS::PHandler event_handler_peer_status_updated = NS::NullPHandler;

public: // -------------------------------------------------------------- Events
	Processor<> event_controller_reset;
	Processor<FrameIndex> event_input_missed;
	Processor<uint32_t /*p_input_worst_receival_time_ms*/, int /*p_optimal_frame_delay*/, int /*p_current_frame_delay*/, int /*p_distance_to_optimal*/> event_client_speedup_adjusted;

public:
	PeerNetworkedController();
	~PeerNetworkedController();

public: // ---------------------------------------------------------------- APIs
	void notify_controllable_objects_changed();

	const std::vector<ObjectData *> &get_sorted_controllable_objects();

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

	void setup_synchronizer(NS::SceneSynchronizerBase &p_synchronizer, int p_peer);
	void remove_synchronizer();

	int get_authority_peer() const { return authority_peer; }

	NS::SceneSynchronizerBase *get_scene_synchronizer() const;
	bool has_scene_synchronizer() const;

	void on_peer_status_updated(int p_peer_id, bool p_connected, bool p_enabled);

	void controllable_collect_input(double p_delta, DataBuffer &r_data_buffer);
	int controllable_count_input_size(DataBuffer &p_data_buffer);
	bool controllable_are_inputs_different(DataBuffer &p_data_buffer_A, DataBuffer &p_data_buffer_B);
	void controllable_process(double p_delta, DataBuffer &p_data_buffer);

	void notify_receive_inputs(const Vector<uint8_t> &p_data);
	void notify_set_server_controlled(bool p_server_controlled);

private:
	void player_set_has_new_input(bool p_has);

public:
	bool player_has_new_input() const;

	bool can_simulate();

protected:
	void notify_controller_reset();

public:
	bool __input_data_parse(
			const Vector<uint8_t> &p_data,
			void *p_user_pointer,
			void (*p_input_parse)(void *p_user_pointer, FrameIndex p_input_id, int p_input_size_in_bits, const BitArray &p_input));
};

struct FrameInput {
	FrameIndex id = FrameIndex::NONE;
	BitArray inputs_buffer;
	uint32_t buffer_size_bit = 0;
	FrameIndex similarity = FrameIndex::NONE;
	/// Local timestamp.
	uint32_t received_timestamp = 0;

	bool operator==(const FrameInput &p_other) const {
		return p_other.id == id;
	}
};

struct Controller {
	PeerNetworkedController *peer_controller;

	Controller(PeerNetworkedController *p_peer_controller) :
			peer_controller(p_peer_controller) {}

	virtual ~Controller() = default;

	virtual void ready() {}
	virtual FrameIndex get_current_frame_index() const = 0;
	virtual void process(double p_delta) = 0;

	virtual bool receive_inputs(const Vector<uint8_t> &p_data) { return false; };
};

struct RemotelyControlledController : public Controller {
	FrameIndex current_input_buffer_id = FrameIndex::NONE;
	uint32_t ghost_input_count = 0;
	std::deque<FrameInput> frames_input;
	// The stream is paused when the client send an empty buffer.
	bool streaming_paused = false;

	bool peer_enabled = false;

public:
	RemotelyControlledController(PeerNetworkedController *p_node);

	virtual void on_peer_update(bool p_peer_enabled);

	virtual FrameIndex get_current_frame_index() const override;
	virtual int get_inputs_count() const;
	FrameIndex last_known_frame_index() const;

	/// Fetch the next inputs, returns true if the input is new.
	virtual bool fetch_next_input(double p_delta);

	virtual void set_frame_input(const FrameInput &p_frame_snapshot, bool p_first_input);

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
			PeerNetworkedController *p_node,
			int p_traced_frames);

	virtual void process(double p_delta) override;

	virtual void on_peer_update(bool p_peer_enabled) override;

	virtual void set_frame_input(const FrameInput &p_frame_snapshot, bool p_first_input) override;

	void notify_send_state();

	virtual bool receive_inputs(const Vector<uint8_t> &p_data) override;

	std::int8_t compute_client_tick_rate_distance_to_optimal();
};

struct AutonomousServerController final : public ServerController {
	AutonomousServerController(
			PeerNetworkedController *p_node);

	virtual bool receive_inputs(const Vector<uint8_t> &p_data) override;
	virtual int get_inputs_count() const override;
	virtual bool fetch_next_input(double p_delta) override;
};

struct PlayerController final : public Controller {
	NS::PHandler event_handler_rewind_frame_begin = NS::NullPHandler;
	NS::PHandler event_handler_state_validated = NS::NullPHandler;

	FrameIndex current_input_id;
	uint32_t input_buffers_counter;
	bool streaming_paused = false;

	std::deque<FrameInput> frames_input;
	LocalVector<uint8_t> cached_packet_data;
	int queued_instant_to_process = -1;

	PlayerController(PeerNetworkedController *p_node);
	~PlayerController();

	void notify_frame_checked(FrameIndex p_input_id);
	int get_frames_count() const;
	int count_frames_after(FrameIndex p_frame_index) const;
	FrameIndex last_known_frame_index() const;
	FrameIndex get_stored_frame_index(int p_i) const;
	virtual FrameIndex get_current_frame_index() const override;

	void on_rewind_frame_begin(FrameIndex p_input_id, int p_index, int p_count);
	bool has_another_instant_to_process_after(int p_i) const;
	virtual void process(double p_delta) override;
	void on_state_validated(FrameIndex p_frame_index, bool p_detected_desync);

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
public:
	struct DollSnapshot final {
		// Contains the data useful for the Doll.
		Snapshot data;
		FrameIndex doll_executed_input = FrameIndex::NONE;

	public:
		DollSnapshot() = default;
		DollSnapshot(DollSnapshot &&p_other) = default;
		DollSnapshot &operator=(DollSnapshot &&p_other) = default;

		DollSnapshot(const DollSnapshot &p_other) = delete;
		DollSnapshot operator=(const DollSnapshot &p_other) = delete;

		DollSnapshot(FrameIndex p_index) { doll_executed_input = p_index; }

		bool operator==(const DollSnapshot &p_other) const { return doll_executed_input == p_other.doll_executed_input; }
	};

public:
	NS::PHandler event_handler_received_snapshot = NS::NullPHandler;
	NS::PHandler event_handler_rewind_frame_begin = NS::NullPHandler;
	NS::PHandler event_handler_state_validated = NS::NullPHandler;
	NS::PHandler event_handler_client_snapshot_updated = NS::NullPHandler;
	NS::PHandler event_handler_snapshot_applied = NS::NullPHandler;

	FrameIndex last_checked_input = FrameIndex::NONE;
	FrameIndex last_doll_checked_input = FrameIndex::NONE;
	int queued_instant_to_process = -1;

	// Contains the controlled nodes frames snapshot.
	std::vector<DollSnapshot> server_snapshots;

	// Contains the controlled nodes frames snapshot.
	std::vector<DollSnapshot> client_snapshots;

public:
	DollController(PeerNetworkedController *p_node);
	~DollController();

	virtual bool receive_inputs(const Vector<uint8_t> &p_data) override;
	void on_rewind_frame_begin(FrameIndex p_input_id, int p_index, int p_count);
	int fetch_optimal_queued_inputs() const;
	virtual bool fetch_next_input(double p_delta) override;
	virtual void process(double p_delta) override;
	void on_state_validated(FrameIndex p_frame_index, bool p_detected_desync);
	void notify_frame_checked(FrameIndex p_input_id);

	void on_received_server_snapshot(const Snapshot &p_snapshot);
	void on_snapshot_update_finished(const Snapshot &p_snapshot);
	void copy_controlled_objects_snapshot(const Snapshot &p_snapshot, std::vector<DollSnapshot> &r_snapshots);

	// Checks whether this doll requires a reconciliation.
	// The check done is relative to the doll timeline, and not the scene sync timeline.
	bool __pcr__fetch_recovery_info(
			FrameIndex p_checking_frame_index,
			Snapshot *r_no_rewind_recover,
			std::vector<std::string> *r_differences_info
#ifdef DEBUG_ENABLED
			,
			std::vector<ObjectNetId> *r_different_node_data
#endif
	) const;

	void on_snapshot_applied(const Snapshot &p_snapshot, const int p_frame_count_to_rewind);

	DollSnapshot *find_snapshot_by_snapshot_id(std::vector<DollSnapshot> &p_snapshots, FrameIndex p_index) const;
	const DollSnapshot *find_snapshot_by_snapshot_id(const std::vector<DollSnapshot> &p_snapshots, FrameIndex p_index) const;
};

/// This controller is used when the game instance is not a peer of any kind.
/// This controller keeps the workflow as usual so it's possible to use the
/// `NetworkedController` even without network.
struct NoNetController : public Controller {
	FrameIndex frame_id;

	NoNetController(PeerNetworkedController *p_node);

	virtual void process(double p_delta) override;
	virtual FrameIndex get_current_frame_index() const override;
};

NS_NAMESPACE_END
