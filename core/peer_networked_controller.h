#pragma once

#include "core.h"
#include "data_buffer.h"
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
	friend class SceneSynchronizerBase;
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
	// The peer associated to this NetController.
	int authority_peer = -1;

	ControllerType controller_type = CONTROLLER_TYPE_NULL;
	Controller *controller = nullptr;
	DataBuffer inputs_buffer;

	SceneSynchronizerBase *scene_synchronizer = nullptr;

	bool are_controllable_objects_sorted = false;
	std::vector<ObjectData *> _sorted_controllable_objects;

	bool has_player_new_input = false;

	PHandler event_handler_peer_status_updated = NullPHandler;

public: // -------------------------------------------------------------- Events
	Processor<> event_controller_reset;
	Processor<FrameIndex> event_input_missed;

public:
	PeerNetworkedController();
	~PeerNetworkedController();

public: // ---------------------------------------------------------------- APIs
	void notify_controllable_objects_changed();

	const std::vector<ObjectData *> &get_sorted_controllable_objects();

	int get_max_redundant_inputs() const;

	FrameIndex get_current_frame_index() const;

	const DataBuffer &get_inputs_buffer() const {
		return inputs_buffer;
	}

	DataBuffer &get_inputs_buffer_mut() {
		return inputs_buffer;
	}

	void server_set_peer_simulating_this_controller(int p_peer, bool p_simulating);
	bool server_is_peer_simulating_this_controller(int p_peer) const;

public: // -------------------------------------------------------------- Events
	bool has_another_instant_to_process_after(int p_i) const;
	void process(float p_delta);

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

	void controllable_collect_input(float p_delta, DataBuffer &r_data_buffer);
	bool controllable_are_inputs_different(DataBuffer &p_data_buffer_A, DataBuffer &p_data_buffer_B);
	void controllable_process(float p_delta, DataBuffer &p_data_buffer);

	void notify_receive_inputs(const std::vector<std::uint8_t> &p_data);

private:
	void player_set_has_new_input(bool p_has);

public:
	bool player_has_new_input() const;

	bool can_simulate();

protected:
	void notify_controller_reset();

public:
	bool __input_data_parse(
			const std::vector<std::uint8_t> &p_data,
			void *p_user_pointer,
			void (*p_input_parse)(void *p_user_pointer, FrameIndex p_input_id, std::uint16_t p_input_size_in_bits, const BitArray &p_input));
};

struct FrameInput {
	FrameIndex id = FrameIndex::NONE;
	BitArray inputs_buffer;
	std::uint16_t buffer_size_bit = 0;
	FrameIndex similarity = FrameIndex::NONE;

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
	virtual void process(float p_delta) = 0;

	virtual bool receive_inputs(const std::vector<std::uint8_t> &p_data) { return false; };
};

struct RemotelyControlledController : public Controller {
	FrameIndex current_input_buffer_id = FrameIndex::NONE;
	std::uint32_t ghost_input_count = 0;
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
	virtual bool fetch_next_input(float p_delta);

	virtual void set_frame_input(const FrameInput &p_frame_snapshot, bool p_first_input);

	virtual void process(float p_delta) override;

	virtual bool receive_inputs(const std::vector<std::uint8_t> &p_data) override;
};

struct ServerController : public RemotelyControlledController {
	std::vector<int> peers_simulating_this_controller;

	ServerController(
			PeerNetworkedController *p_node);

	virtual void process(float p_delta) override;

	virtual void on_peer_update(bool p_peer_enabled) override;

	virtual void set_frame_input(const FrameInput &p_frame_snapshot, bool p_first_input) override;

	void notify_send_state();

	virtual bool receive_inputs(const std::vector<std::uint8_t> &p_data) override;
};

struct AutonomousServerController final : public ServerController {
	AutonomousServerController(
			PeerNetworkedController *p_node);

	virtual bool receive_inputs(const std::vector<std::uint8_t> &p_data) override;
	virtual int get_inputs_count() const override;
	virtual bool fetch_next_input(float p_delta) override;
};

struct PlayerController final : public Controller {
	NS::PHandler event_handler_rewind_frame_begin = NS::NullPHandler;
	NS::PHandler event_handler_state_validated = NS::NullPHandler;

	FrameIndex current_input_id;
	std::uint32_t input_buffers_counter;
	bool streaming_paused = false;

	std::deque<FrameInput> frames_input;
	std::vector<std::uint8_t> cached_packet_data;
	int queued_instant_to_process = -1;

	PlayerController(PeerNetworkedController *p_node);
	~PlayerController();

	void notify_frame_checked(FrameIndex p_input_id);
	int get_frames_count() const;
	int count_frames_after(FrameIndex p_frame_index) const;
	FrameIndex last_known_frame_index() const;
	FrameIndex get_stored_frame_index(int p_i) const;
	virtual FrameIndex get_current_frame_index() const override;

	void on_rewind_frame_begin(FrameIndex p_frame_index, int p_rewinding_index, int p_rewinding_frame_count);
	bool has_another_instant_to_process_after(int p_i) const;
	virtual void process(float p_delta) override;
	void on_state_validated(FrameIndex p_frame_index, bool p_detected_desync);

	virtual bool receive_inputs(const std::vector<std::uint8_t> &p_data) override;

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

	// The lastest `FrameIndex` validated.
	FrameIndex last_doll_validated_input = FrameIndex::NONE;
	// The lastest `FrameIndex` on which the server / doll snapshots were compared.
	FrameIndex last_doll_compared_input = FrameIndex::NONE;
	FrameIndex queued_frame_index_to_process = FrameIndex{ { 0 } };
	int queued_instant_to_process = -1;

	// Contains the controlled nodes frames snapshot.
	std::vector<DollSnapshot> server_snapshots;

	// Contains the controlled nodes frames snapshot.
	std::vector<DollSnapshot> client_snapshots;

public:
	DollController(PeerNetworkedController *p_node);
	~DollController();

	virtual bool receive_inputs(const std::vector<std::uint8_t> &p_data) override;
	void on_rewind_frame_begin(FrameIndex p_frame_index, int p_rewinding_index, int p_rewinding_frame_count);
	int fetch_optimal_queued_inputs() const;
	virtual bool fetch_next_input(float p_delta) override;
	virtual void process(float p_delta) override;

	void on_state_validated(FrameIndex p_frame_index, bool p_detected_desync);
	void notify_frame_checked(FrameIndex p_input_id);
	void clear_previously_generated_client_snapshots();

	void on_received_server_snapshot(const Snapshot &p_snapshot);
	void on_snapshot_update_finished(const Snapshot &p_snapshot);
	void copy_controlled_objects_snapshot(
			const Snapshot &p_snapshot,
			std::vector<DollSnapshot> &r_snapshots,
			bool p_store_even_when_doll_is_not_processing);

	FrameIndex fetch_checkable_snapshot(DollSnapshot *&r_client_snapshot, DollSnapshot *&r_server_snapshot);

	// Checks whether this doll requires a reconciliation.
	// The check done is relative to the doll timeline, and not the scene sync timeline.
	bool __pcr__fetch_recovery_info(
			const FrameIndex p_checking_frame_index,
			const int p_frame_count_to_rewind,
			Snapshot *r_no_rewind_recover,
			std::vector<std::string> *r_differences_info
#ifdef NS_DEBUG_ENABLED
			,
			std::vector<ObjectNetId> *r_different_node_data
#endif
	);

	void on_snapshot_applied(const Snapshot &p_global_server_snapshot, const int p_frame_count_to_rewind);
	void apply_snapshot_no_input_reconciliation(const Snapshot &p_global_server_snapshot);
	void apply_snapshot_instant_input_reconciliation(const Snapshot &p_global_server_snapshot, const int p_frame_count_to_rewind);
	void apply_snapshot_rewinding_input_reconciliation(const Snapshot &p_global_server_snapshot, const int p_frame_count_to_rewind);
};

/// This controller is used when the game instance is not a peer of any kind.
/// This controller keeps the workflow as usual so it's possible to use the
/// `NetworkedController` even without network.
struct NoNetController : public Controller {
	FrameIndex frame_id;

	NoNetController(PeerNetworkedController *p_node);

	virtual void process(float p_delta) override;
	virtual FrameIndex get_current_frame_index() const override;
};

NS_NAMESPACE_END
