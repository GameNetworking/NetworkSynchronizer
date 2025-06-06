#pragma once

#include "core/event_processor.h"
#include "core/network_interface.h"
#include "core/object_data_storage.h"
#include "core/processor.h"
#include "core/net_utilities.h"
#include "core/snapshot.h"
#include "core/scheduled_procedure.h"
#include <deque>
#include <map>
#include <optional>
#include <vector>

NS_NAMESPACE_BEGIN
class SynchronizerManager {
	class SceneSynchronizerBase *scene_synchronizer = nullptr;

public:
	virtual ~SynchronizerManager() {
	}

	void set_scene_synchronizer_base(SceneSynchronizerBase *p_scene_synchronizer) {
		scene_synchronizer = p_scene_synchronizer;
	}

	SceneSynchronizerBase *get_scene_synchronizer_base() const {
		return scene_synchronizer;
	}

	virtual void on_init_synchronizer(bool p_was_generating_ids) {
	}

	virtual void on_uninit_synchronizer() {
	}

#ifdef NS_DEBUG_ENABLED
	virtual void debug_only_validate_objects() {
	}

	// Unique ID that is used to validate the object and ensure that destroyed objects are properly unregistered.
	virtual uint64_t debug_only_get_object_id(ObjectHandle p_app_object_handle) const = 0;
#endif

	/// Add object data and generates the `ObjectNetId` if allowed.
	virtual void on_add_object_data(ObjectData &p_object_data) {
	}

	virtual void on_drop_object_data(ObjectData &p_object_data) {
	}

	virtual void on_sync_group_created(SyncGroupId p_group_id) {
	}

	/// This function is always executed on the server before anything else
	/// and it's here that you want to update the object relevancy.
	virtual void update_objects_relevancy() {
	}

	/// This function is called during the snapshot generation on both the client
	/// and the server and allows to add custom data to it.
	/// Returns true if the r_custom_data is set.
	virtual bool snapshot_get_custom_data(
			const SyncGroup *p_group,
			/// This is set to "true" when the current snapshot contains only
			/// part of the changed objects.
			bool p_is_partial_update,
			/// This list is populated only when `p_is_partial_update` is `true`
			/// and contains the indices of the simulated objects info that you
			/// can use to retrieve the ObjectData using `p_group->get_simulated_sync_objects()[index].od`.
			const std::vector<std::size_t> &p_partial_update_simulated_objects_info_indices,
			VarData &r_custom_data) {
		return false;
	}

	virtual std::uint8_t snapshot_get_custom_data_type() const {
		return 0;
	}

	/// This function is always called on client to merge the custom data
	/// between the client snapshot and the partial update snapshot received
	/// from the server.
	virtual bool snapshot_merge_custom_data_for_partial_update(
			const std::vector<ObjectNetId> &p_partial_update_objects,
			VarData &r_custom_data,
			const VarData &p_custom_data_from_server_snapshot) {
		return false;
	}

	virtual void snapshot_set_custom_data(const VarData &r_custom_data) {
	}

	virtual ObjectHandle fetch_app_object(const std::string &p_object_name) = 0;
	/// Returns the object name.
	/// NOTICE: The object name MUST be unique per object and MUST NEVER CHANGE.
	/// NOTICE: You can delay the object name initialization by returning an empty string "".
	///         This feature is useful in case this function is called before the name for the object is available.
	///         Once the name is returned for an object (the returned string is non empty "")
	///         the name must remain the same forever.
	/// NOTICE: The name must be unique across all the peers!
	virtual std::string fetch_object_name(ObjectHandle p_app_object_handle) const = 0;
	virtual void setup_synchronizer_for(ObjectHandle p_app_object_handle, ObjectLocalId p_id, SchemeId p_scheme_id) = 0;

	/// This allows the client to drop a server snapshot.
	/// This is for advanced use and allows to skip some snapshots based on some criteria.
	virtual bool can_client_store_server_snapshot(const RollingUpdateSnapshot &p_snapshot) const {
		return true;
	}
};

struct LagCompensationSettings {
	/// If true, the dolls (The objects controlled by other player) will guess
	/// the input if the input is missing.
	bool doll_allow_guess_input_when_missing = true;

	/// Forces the input reconciliation when the doll accumulates an input count
	/// that surpassed by X (defined below) the amount of frames to reconcile.
	/// NOTICE: This MUST never be less than 1;
	/// NOTICE: This should not be too low, or the doll will jump arround too much.
	/// NOTICE: To disable this featues you can set this to 10 or more.
	int doll_force_input_reconciliation = 10;

	/// The minimum amount of frames needed to trigger the input reconciliation.
	/// NOTE: This must be more than 1
	int doll_force_input_reconciliation_min_frames = 5;
};

struct Settings {
	LagCompensationSettings lag_compensation;
};

/// # SceneSynchronizer
///
/// NOTICE: Do not instantiate this class directly, please use `SceneSynchronizer<>` instead.
///
/// The `SceneSynchronizer` is responsible to keep the scene of all peers in sync.
/// Usually each peer has it instantiated, and depending if it's instantiated in
/// the server or in the client, it does a different thing.
///
/// ## The `Player` is playing the game on the server.
///
/// The server is authoritative and it can't never be wrong. For this reason
/// the `SceneSynchronizer` on the server sends at a fixed interval (defined by
/// `frame_confirmation_timespan`) a snapshot to all peers.
///
/// The clients receive the server snapshot, so it compares with the local
/// snapshot and if it's necessary perform the recovery.
///
/// ## Variable tracking
///
/// The `SceneSynchronizer` is able to track any node variable. It's possible to specify
/// the variables to track using the function `register_variable`.
///
/// ## NetworkedController
/// The `NetworkedController` is able to aquire the `Player` input and perform
/// operation in sync with other peers. When a discrepancy is found by the
/// `SceneSynchronizer`, it will drive the `NetworkedController` so to recover that
/// missalignment.
///
///
/// ## Processing function
/// Some objects, that are not direclty controlled by a `Player`, may need to be
/// in sync between peers; since those are not controlled by a `Player` is
/// not necessary use the `NetworkedController`.
///
/// It's possible to specify some process functions using `register_process`.
/// The `SceneSynchronizer` will call these functions each frame, in sync with the
/// other peers.
///
/// As example object we may think about a moving platform, or a bridge that
/// opens and close, or even a simple timer to track the match time.
/// An example implementation would be:
/// ```
/// var time := 0.0
///
/// func _ready():
/// 	# Make sure this never go out of sync.
/// 	SceneSynchronizer.register_variable(self, "time")
///
/// 	# Make sure to call this in sync with other peers.
/// 	SceneSynchronizer.register_process(self, "in_sync_process")
///
/// func in_sync_process(delta: float):
/// 	time += delta
/// ```
/// In the above code the variable `time` will always be in sync.
///
//
// # Implementation details.
//
// The entry point of the above mechanism is the function `SceneSynchronizer::process()`.
// The server `SceneSynchronizer` code is inside the class `ServerSynchronizer`.
// The client `SceneSynchronizer` code is inside the class `ClientSynchronizer`.
// The no networking `SceneSynchronizer` code is inside the class `NoNetSynchronizer`.
class SceneSynchronizerBase {
	template <class C, class NI>
	friend class SceneSynchronizer;
	friend class Synchronizer;
	friend class ServerSynchronizer;
	friend class ClientSynchronizer;
	friend class NoNetSynchronizer;

public:
	enum SynchronizerType {
		SYNCHRONIZER_TYPE_NULL,
		SYNCHRONIZER_TYPE_NONETWORK,
		SYNCHRONIZER_TYPE_CLIENT,
		SYNCHRONIZER_TYPE_SERVER
	};

protected:
	static void (*var_data_encode_func)(class DataBuffer &r_buffer, const VarData &p_val);
	static void (*var_data_decode_func)(VarData &r_val, DataBuffer &p_buffer, std::uint8_t p_var_type);
	static bool (*var_data_compare_func)(const VarData &p_A, const VarData &p_B);
	static std::string (*var_data_stringify_func)(const VarData &p_var_data, bool p_verbose);
	static bool var_data_stringify_force_verbose;

	static void (*print_line_func)(PrintMessageType p_level, const std::string &p_str);
	static void (*print_code_message_func)(const char *p_function, const char *p_file, int p_line, const std::string &p_error, const std::string &p_message, NS::PrintMessageType p_type);
	static void (*print_flush_stdout_func)();

#ifdef NS_DEBUG_ENABLED

public:
	const bool pedantic_checks = false;
	/// This is turned on by the integration tests to ensure no desync are
	/// triggered by `ClientSynchronizer::calculates_sub_ticks` returning > 1.
	const bool disable_client_sub_ticks = false;

protected:
#endif

protected: // --------------------------------------------------------- Settings

	/// Fixed rate at which the NetSync has to produce frames.
	int frames_per_seconds = 60;
	float fixed_frame_delta = 1.0f / frames_per_seconds;

	/// This number is used to clamp the maximum amount of frames that is
	/// possible to produce per frame by the client;
	/// To avoid generating way too many frames, eroding the client perf.
	/// NOTE: The client may want to generate more frames in case of:
	///       - The server asks for a speedup.
	///       - The client FPS are under the networking tick rate.
	std::uint8_t max_sub_process_per_frame = 4;

	/// The `ServerController` will try to keep a margin of error, so that
	/// network oscillations doesn't leave the `ServerController` without
	/// inputs.
	///
	/// This margin of error is called `optimal_frame_delay` and it changes
	/// depending on the connection health:
	/// it can go from `min_server_input_buffer_size` to `max_server_input_buffer_size`.
	int min_server_input_buffer_size = 2;
	int max_server_input_buffer_size = 7;

	int min_doll_input_buffer_size = 2;
	int max_doll_input_buffer_size = 7;

	/// Amount of time a player inputs is re-sent to each peer.
	/// Resending inputs is necessary because the packets may be lost since as
	/// they are sent in an unreliable way.
	int max_redundant_inputs = 6;

	/// Negligible packet loss we can just ignore.
	float negligible_packet_loss = 0.001f;

	/// The worst packet loss.
	/// NOTE: The smallest the more conservative the system is: increasing the
	///       server input buffer size to give enough time to inputs arrive the
	///       server before being processed.
	///       Too small number would make the server collects way too few inputs.
	/// Default 2.5%
	float worst_packet_loss = 0.025f;

	/// Amount of additional frames produced per second in % relative to
	/// `frames_per_seconds` defined above.
	float max_fps_acceleration_percentage = 0.2f;

	/// Interval (seconds) between each network statistic update sent to the clients
	float netstats_update_interval_sec = 0.6f;

	int max_trickled_objects_per_update = 30;
	float max_trickled_interpolation_alpha = 1.2f;

	/// How much time passes between each snapshot sent by the server to confirm
	/// a set of frames predicted by the client.
	float frame_confirmation_timespan = 1.0f;

	/// The amount of objects to include into the partial update.
	int max_objects_count_per_partial_update = 3;

	/// This parameter is used to defines how many intervals the client can ever
	/// predict.
	/// The NetSync stops recording more frames, if the clients overflow this span.
	/// - This is a way to keep the rewinginds smaller.
	/// - This is a way to avoid the client to go too ahead the server.
	float max_predicted_intervals = 2.0f;

	/// Can be 0.0 to update the relevancy each frame.
	float objects_relevancy_update_time = 0.5f;

	/// Update the latency each 3 seconds.
	float latency_update_rate = 3.0f;

	/// Set to false to drop all the undelivered RPCs because the object was not
	/// found at the time of the RPC receival.
	int store_undelivered_rpcs = true;

	/// The number of snapshots failures before the client requests a full
	/// snapshot to the server.
	int max_snapshot_parsing_failures = 10;

protected: // ----------------------------------------------------- User defined
	class NetworkInterface *network_interface = nullptr;
	SynchronizerManager *synchronizer_manager = nullptr;

protected: // -------------------------------------------------------- Internals
	RpcHandle<DataBuffer &> rpc_handler_state;
	RpcHandle<> rpc_handler_notify_need_full_snapshot;
	RpcHandle<bool> rpc_handler_set_network_enabled;
	RpcHandle<bool> rpc_handler_notify_peer_status;
	RpcHandle<const std::vector<std::uint8_t> &> rpc_handler_trickled_sync_data;
	RpcHandle<DataBuffer &> rpc_handle_notify_netstats;
	RpcHandle<ObjectNetId, ScheduledProcedureId, GlobalFrameIndex, const DataBuffer &> rpc_handle_notify_scheduled_procedure_start;
	RpcHandle<ObjectNetId, ScheduledProcedureId> rpc_handle_notify_scheduled_procedure_stop;
	RpcHandle<ObjectNetId, ScheduledProcedureId, GlobalFrameIndex> rpc_handle_notify_scheduled_procedure_pause;

	// Controller RPCs.
	RpcHandle<int, const std::vector<std::uint8_t> &> rpc_handle_receive_input;

	GlobalFrameIndex global_frame_index = GlobalFrameIndex{ 0 };

	Settings settings;
	bool settings_changed = true;

	SynchronizerType synchronizer_type = SYNCHRONIZER_TYPE_NULL;

	class Synchronizer *synchronizer = nullptr;
	bool recover_in_progress = false;
	bool reset_in_progress = false;
	bool rewinding_in_progress = false;
	bool end_sync = false;

	std::map<int, PeerData> peer_data;

	struct UndeliveredRpcs {
		int sender_peer;
		DataBuffer data_buffer;
	};

	std::map<ObjectNetId, std::map<std::uint8_t, UndeliveredRpcs>> undelivered_rpcs;

	bool generate_id = false;

	ObjectDataStorage objects_data_storage;

	int event_flag = 0;
	std::vector<ChangesListener *> changes_listeners;

	bool cached_process_functions_valid = false;
	Processor<float> cached_process_functions[PROCESS_PHASE_COUNT];

	bool debug_rewindings_enabled = false;
	PrintMessageType debug_rewindings_log_level = VERBOSE;
	bool debug_server_speedup = false;
	bool debug_log_nodes_relevancy_update = false;

	float time_bank = 0.0f;

public: // -------------------------------------------------------------- Events
	/// Called when the SceneSync starts to synchronize the objects.
	EventProcessor<> event_sync_started;
	/// Is called when the synchronization is paused.
	EventProcessor<> event_sync_paused;
	EventProcessor<const Settings &> event_settings_changed;
	/// Executed at the end of the processing.
	/// Notice this is not the sub and fixed time processing which is sync.
	/// This is emitted by the application processing function and the delta time is frame dependent.
	EventProcessor<float/*delta seconds*/> event_app_process_end;
	EventProcessor<int /*p_peer*/, bool /*p_connected*/, bool /*p_enabled*/> event_peer_status_updated;
	EventProcessor<FrameIndex, bool /*p_desync_detected*/> event_state_validated;
	EventProcessor<> event_rewind_starting;
	EventProcessor<> event_rewind_completed;
	EventProcessor<FrameIndex, int /*p_peer*/> event_sent_snapshot;
	/// This event is emitted when the current client state is stored into the snapshot.
	/// NOTE: This even is also executed during the rewinding, to update the previously stored states.
	/// NOTE: Something to remark is that the Snapshot data passed, is equal to
	///       the data read through the get functions, at the moment of the event.
	///       So, you can assume the snapshot contains the result of the last executed input.
	EventProcessor<const Snapshot & /*p_snapshot*/> event_snapshot_update_finished;
	EventProcessor<const Snapshot & /*p_snapshot*/, int /*p_frame_count_to_rewind*/> event_snapshot_applied;
	EventProcessor<const Snapshot & /*p_received_snapshot*/> event_received_server_snapshot;
	EventProcessor<FrameIndex /*p_frame_index*/, int /*p_rewinding_index*/, int /*p_rewinding_frame_count*/> event_rewind_frame_begin;
	EventProcessor<FrameIndex, ObjectHandle /*p_app_object_handle*/, const std::vector<std::optional<VarData>> & /*p_client_values*/, const std::vector<std::optional<VarData>> & /*p_server_values*/> event_desync_detected_with_info;

private:
	// This is private so this class can be created only from
	// `SceneSynchronizer<BaseClass>` and the user is forced to define a base class.
	SceneSynchronizerBase(NetworkInterface *p_network_interface, bool p_pedantic_checks, bool p_disable_client_sub_ticks);

public:
	~SceneSynchronizerBase();

public: // -------------------------------------------------------- Manager APIs
	static void install_synchronizer(
			void (*p_var_data_encode_func)(DataBuffer &r_buffer, const VarData &p_val),
			void (*p_var_data_decode_func)(VarData &r_val, DataBuffer &p_buffer, std::uint8_t p_variable_type),
			bool (*p_var_data_compare_func)(const VarData &p_A, const VarData &p_B),
			std::string (*p_var_data_stringify_func)(const VarData &p_var_data, bool p_verbose),
			void (*p_print_line_func)(PrintMessageType p_type, const std::string &p_str),
			void (*p_print_code_message_func)(const char *p_function, const char *p_file, int p_line, const std::string &p_error, const std::string &p_message, NS::PrintMessageType p_type),
			void (*p_print_flush_stdout_func)());

	/// Setup the synchronizer
	void setup(SynchronizerManager &p_synchronizer_manager);

	/// Prepare the synchronizer for destruction.
	void conclude();

	/// Process the SceneSync.
	void process(float p_delta);

	/// Call this function when a networked app object is destroyed.
	void on_app_object_removed(ObjectHandle p_app_object_handle);

public:
	static void var_data_encode(DataBuffer &r_buffer, const VarData &p_val, std::uint8_t p_variable_type);
	static void var_data_decode(VarData &r_val, DataBuffer &p_buffer, std::uint8_t p_variable_type);
	static bool var_data_compare(const VarData &p_A, const VarData &p_B);
	static std::string var_data_stringify(const VarData &p_var_data, bool p_verbose = false);
	static void __print_line(PrintMessageType p_level, const std::string &p_str);
	static void print_code_message(SceneSynchronizerDebugger *p_debugger, const char *p_function, const char *p_file, int p_line, const std::string &p_error, const std::string &p_message, NS::PrintMessageType p_type);
	static void print_flush_stdout();

	NetworkInterface &get_network_interface() {
		return *network_interface;
	}

	const NetworkInterface &get_network_interface() const {
		return *network_interface;
	}

	SynchronizerManager &get_synchronizer_manager() {
		return *synchronizer_manager;
	}

	const SynchronizerManager &get_synchronizer_manager() const {
		return *synchronizer_manager;
	}

	const Synchronizer *get_synchronizer_internal() const {
		return synchronizer;
	}

	Synchronizer *get_synchronizer_internal() {
		return synchronizer;
	}

	void set_frames_per_seconds(int p_fps);
	int get_frames_per_seconds() const;

	void set_max_objects_count_per_partial_update(int p_val) {
		max_objects_count_per_partial_update = p_val;
	}

	int get_max_objects_count_per_partial_update() const {
		return max_objects_count_per_partial_update;
	}

	// The tick delta time used to step the networking processing.
	float get_fixed_frame_delta() const;

	void set_max_sub_process_per_frame(std::uint8_t p_max_sub_process_per_frame);
	std::uint8_t get_max_sub_process_per_frame() const;

	void set_min_server_input_buffer_size(int p_val);
	int get_min_server_input_buffer_size() const;

	void set_max_server_input_buffer_size(int p_val);
	int get_max_server_input_buffer_size() const;

	void set_min_doll_input_buffer_size(int p_val) {
		min_doll_input_buffer_size = p_val;
	}

	int get_min_doll_input_buffer_size() const {
		return min_doll_input_buffer_size;
	}

	void set_max_doll_input_buffer_size(int p_val) {
		max_doll_input_buffer_size = p_val;
	}

	int get_max_doll_input_buffer_size() const {
		return max_doll_input_buffer_size;
	}

	void set_max_redundant_inputs(int p_val) {
		max_redundant_inputs = p_val;
	}

	int get_max_redundant_inputs() const {
		return max_redundant_inputs;
	}

	void set_negligible_packet_loss(float p_val);
	float get_negligible_packet_loss() const;

	void set_worst_packet_loss(float p_val);
	float get_worst_packet_loss() const;

	void set_max_fps_acceleration_percentage(float p_percentage);
	float get_max_fps_acceleration_percentage() const;

	void set_netstats_update_interval_sec(float p_delay_in_ms);
	float get_netstats_update_interval_sec() const;

	void set_max_trickled_objects_per_update(int p_rate);
	int get_max_trickled_objects_per_update() const;

	void set_max_trickled_interpolation_alpha(float p_int_alpha);
	float get_max_trickled_interpolation_alpha() const;

	void set_frame_confirmation_timespan(float p_interval);
	float get_frame_confirmation_timespan() const;

	void set_max_predicted_intervals(float p_max_predicted_intevals);
	float get_max_predicted_intervals() const;

	void set_objects_relevancy_update_time(float p_time);
	float get_objects_relevancy_update_time() const;

	void set_latency_update_rate(float p_rate_seconds);
	float get_latency_update_rate() const;

	void set_max_snapshot_parsing_failures(int p_max_snapshot_parsing_failures) {
		max_snapshot_parsing_failures = p_max_snapshot_parsing_failures;
	}

	int get_max_snapshot_parsing_failures() const {
		return max_snapshot_parsing_failures;
	}

	bool is_variable_registered(ObjectLocalId p_id, const std::string &p_variable) const;

	void set_debug_rewindings_enabled(bool p_enabled);

	bool get_debug_rewindings_enabled() const {
		return debug_rewindings_enabled;
	}

	void set_debug_rewindings_log_level(PrintMessageType p_level) {
		debug_rewindings_log_level = p_level;
	}

	PrintMessageType get_debug_rewindings_log_level() const {
		return debug_rewindings_log_level;
	}

	void set_debug_server_speedup(bool p_enabled);

	bool get_debug_server_speedup() const {
		return debug_server_speedup;
	}

	void set_debug_log_nodes_relevancy_update(bool p_enabled);

	bool get_debug_log_nodes_relevancy_update() const {
		return debug_log_nodes_relevancy_update;
	}

	static void var_data_stringify_set_force_verbose(bool p_force);
	static bool var_data_stringify_get_force_verbose();

public: // ---------------------------------------------------------------- RPCs
	void rpc_receive_state(DataBuffer &p_snapshot);
	void rpc__notify_need_full_snapshot();
	void rpc_set_network_enabled(bool p_enabled);
	void rpc_notify_peer_status(bool p_enabled);
	void rpc_trickled_sync_data(const std::vector<std::uint8_t> &p_data);
	void rpc_notify_netstats(DataBuffer &p_data);
	void rpc_notify_scheduled_procedure_start(ObjectNetId p_object_id, ScheduledProcedureId p_scheduled_procedure_id, GlobalFrameIndex p_frame_index, const DataBuffer &p_args);
	void rpc_notify_scheduled_procedure_stop(ObjectNetId p_object_id, ScheduledProcedureId p_scheduled_procedure_id);
	void rpc_notify_scheduled_procedure_pause(ObjectNetId p_object_id, ScheduledProcedureId p_scheduled_procedure_id, GlobalFrameIndex p_pause_frame);

	void call_rpc_receive_inputs(const std::vector<int> &p_recipients, int p_peer, const std::vector<std::uint8_t> &p_data);

	void rpc_receive_inputs(int p_peer, const std::vector<std::uint8_t> &p_data);

public: // ---------------------------------------------------------------- APIs
	GlobalFrameIndex get_global_frame_index() const {
		return global_frame_index;
	}

	void set_settings(Settings &p_settings);
	Settings &get_settings_mutable();
	const Settings &get_settings() const;

	void register_app_object(ObjectHandle p_app_object_handle, ObjectLocalId *out_id = nullptr, SchemeId p_scheme_id = SchemeId::DEFAULT);
	void unregister_app_object(ObjectLocalId p_id);
	void re_register_app_object(ObjectLocalId p_id, SchemeId p_scheme_id = SchemeId::DEFAULT);
	void setup_controller(
			ObjectLocalId p_id,
			std::function<void(float /*delta*/, DataBuffer & /*r_data_buffer*/)> p_collect_input_func,
			std::function<bool(DataBuffer & /*p_data_buffer_A*/, DataBuffer & /*p_data_buffer_B*/)> p_are_inputs_different_func,
			std::function<void(float /*delta*/, DataBuffer & /*p_data_buffer*/)> p_process_func);
	void set_controlled_by_peer(
			ObjectLocalId p_id,
			int p_peer);
	void register_variable(ObjectLocalId p_id, const std::string &p_variable_name, const NS_VarDataSetFunc &p_set_func, const NS_VarDataGetFunc &p_get_func);
	void unregister_variable(ObjectLocalId p_id, const std::string &p_variable);

	ObjectNetId get_app_object_net_id(ObjectLocalId p_local_id) const;
	ObjectNetId get_app_object_net_id(ObjectHandle p_app_object_handle) const;

	ObjectHandle get_app_object_from_id(ObjectNetId p_id, bool p_expected = true);
	ObjectHandle get_app_object_from_id_const(ObjectNetId p_id, bool p_expected = true) const;

	const std::vector<ObjectData *> &get_sorted_objects_data() const;
	const std::vector<ObjectData *> &get_all_object_data() const;
	const std::vector<ObjectData *> *get_peer_controlled_objects_data(int p_peer) const;

	/// Returns the variable ID relative to the `Object`.
	/// This may return `NONE` in various cases:
	/// - The Object is not registered.
	/// - The variable is not registered.
	VarId get_variable_id(ObjectLocalId p_id, const std::string &p_variable);

	void set_skip_rewinding(ObjectLocalId p_id, const std::string &p_variable, bool p_skip_rewinding);

	ListenerHandle track_variable_changes(
			ObjectLocalId p_id,
			const std::string &p_variable,
			std::function<void(const std::vector<VarData> &p_old_values)> p_listener_func,
			NetEventFlag p_flags = NetEventFlag::DEFAULT);

	ListenerHandle track_variables_changes(
			const std::vector<ObjectLocalId> &p_object_ids,
			const std::vector<std::string> &p_variables,
			std::function<void(const std::vector<VarData> &p_old_values)> p_listener_func,
			NetEventFlag p_flags = NetEventFlag::DEFAULT);

	void untrack_variable_changes(ListenerHandle p_handle);

	/// You can use the macro `callable_mp()` to register custom C++ function.
	PHandler register_process(ObjectLocalId p_id, ProcessPhase p_phase, std::function<void(float)> p_func);
	void unregister_process(ObjectLocalId p_id, ProcessPhase p_phase, PHandler p_func_handler);

	ScheduledProcedureId register_scheduled_procedure(
			ObjectLocalId p_id,
			const NS_ScheduledProcedureFunc &p_func);

	void unregister_scheduled_procedure(
			ObjectLocalId p_id,
			ScheduledProcedureId p_procedure_id);

	/// Starts the scheduled procedure.
	/// `p_peer_to_compensate` the peer_id that the server will use as base to
	///     establish the execution frame that starts after the already processed frames.
	///     Note: the client is always ahead the server and without this compensation the
	///     procedure would always be executed on a frame already processed on the client.
	GlobalFrameIndex scheduled_procedure_start(
			ObjectLocalId p_id,
			ScheduledProcedureId p_procedure_id,
			float p_execute_in_seconds,
			int p_peer_to_compensate = -1,
			float p_max_compensation_seconds = -1.0);

	void scheduled_procedure_stop(
			ObjectLocalId p_id,
			ScheduledProcedureId p_procedure_id);

	void scheduled_procedure_pause(
			ObjectLocalId p_id,
			ScheduledProcedureId p_procedure_id);

	GlobalFrameIndex scheduled_procedure_unpause(
			ObjectLocalId p_id,
			ScheduledProcedureId p_procedure_id,
			int p_peer_to_compensate = -1,
			float p_max_compensation_seconds = -1.0);

	float scheduled_procedure_get_remaining_seconds(
			ObjectLocalId p_id,
			ScheduledProcedureId p_procedure_id) const;

	bool scheduled_procedure_is_paused(
			ObjectLocalId p_id,
			ScheduledProcedureId p_procedure_id) const;

private:
	GlobalFrameIndex scheduled_procedure_compensate_execution_frame(GlobalFrameIndex p_execute_on_frame, int p_peer_to_compensate, float p_max_compensation_seconds) const;

private:
	bool rpc_is_allowed(ObjectLocalId p_id, int p_rpc_index, RpcRecipientFetch p_recipient) const;
	std::vector<int> rpc_fetch_recipients(ObjectLocalId p_id, int p_rpc_id, RpcRecipientFetch p_recipient) const;

public:
	/// Register a new RPC
	template <typename... ARGS>
	RpcHandle<ARGS...> register_rpc(
			ObjectLocalId p_id,
			std::function<void(ARGS...)> p_rpc_func,
			bool p_reliable,
			bool p_call_local,
			RpcAllowedSender p_allowed_senders = RpcAllowedSender::ALL) {
		ObjectData *object_data = get_object_data(p_id);
		NS_ENSURE_V_MSG(object_data, RpcHandle<ARGS...>(), "The objectLocalId `"+std::to_string(p_id.id)+"` was not found. RPC registration failed.");
		NS_ENSURE_V_MSG(network_interface, RpcHandle<ARGS...>(), "Network interface not specified. RPC registration failed.");
		return network_interface->rpc_config(p_rpc_func, p_reliable, p_call_local, p_allowed_senders, p_id, &object_data->rpcs_info);
	}

	/// Used to verify if the rpc can be triggered from this peer to the target peer.
	template <typename... ARGS>
	bool rpc_is_allowed(const RpcHandle<ARGS...> &p_rpc, RpcRecipientFetch p_recipient) const {
		return rpc_is_allowed(p_rpc.get_target_id(), p_rpc.get_index(), p_recipient);
	}

	/// Call the rpc
	template <typename... ARGS>
	void rpc_call(const RpcHandle<ARGS...> &p_rpc, RpcRecipientFetch p_recipient, ARGS... p_args) {
		const std::vector<int> recipients = rpc_fetch_recipients(p_rpc.get_target_id(), p_rpc.get_index(), p_recipient);
		p_rpc.rpc(get_network_interface(), recipients, p_args...);
	}

public:
	/// Setup the trickled sync method for this specific object.
	/// The trickled-sync is different from the realtime-sync because the data
	/// is streamed and not simulated.
	void setup_trickled_sync(
			ObjectLocalId p_id,
			std::function<void(DataBuffer & /*out_buffer*/, float /*update_rate*/)> p_func_trickled_collect,
			std::function<void(float /*delta*/, float /*interpolation_alpha*/, DataBuffer & /*past_buffer*/, DataBuffer & /*future_buffer*/)> p_func_trickled_apply);

	/// Returns the latency (RTT in ms) for this peer or -1 if the latency is not available.
	int get_peer_latency_ms(int p_peer) const;
	/// Returns the latency jittering (how much the latency oscillates).
	/// On client: This function returns 0 for non local peers.
	int get_peer_latency_jitter_ms(int p_peer) const;
	/// Returns the packet loss percentage.
	/// On client: This function returns 0 for non local peers.
	float get_peer_packet_loss_percentage(int p_peer) const;

	/// Creates a sync group containing the list of sync objects.
	/// The Peers listening to this group will receive the updates only
	/// from the objects within this group.
	SyncGroupId sync_group_create();

	/// IMPORTANT: The pointer returned is invalid at the end of the scope executing this function. Never store it.
	const SyncGroup *sync_group_get(SyncGroupId p_group_id) const;

	void sync_group_add_object(ObjectLocalId p_id, SyncGroupId p_group_id, bool p_realtime);
	void sync_group_add_object(ObjectNetId p_id, SyncGroupId p_group_id, bool p_realtime);
	void sync_group_add_object(ObjectData *p_object_data, SyncGroupId p_group_id, bool p_realtime);

	void sync_group_remove_object(ObjectLocalId p_id, SyncGroupId p_group_id);
	void sync_group_remove_object(ObjectNetId p_id, SyncGroupId p_group_id);
	void sync_group_remove_object(ObjectData *p_object_data, SyncGroupId p_group_id);

	void sync_group_fetch_object_grups(ObjectLocalId p_id, std::vector<SyncGroupId> &r_simulated_groups, std::vector<SyncGroupId> &r_trickled_groups) const;
	void sync_group_fetch_object_grups(ObjectNetId p_id, std::vector<SyncGroupId> &r_simulated_groups, std::vector<SyncGroupId> &r_trickled_groups) const;
	void sync_group_fetch_object_grups(const ObjectData *p_object_data, std::vector<SyncGroupId> &r_simulated_groups, std::vector<SyncGroupId> &r_trickled_groups) const;

	void sync_group_set_simulated_partial_update_timespan_seconds(ObjectLocalId p_id, SyncGroupId p_group_id, bool p_partial_update_enabled, float p_update_timespan);
	bool sync_group_is_simulated_partial_updating(ObjectLocalId p_id, SyncGroupId p_group_id) const;
	float sync_group_get_simulated_partial_update_timespan_seconds(ObjectLocalId p_id, SyncGroupId p_group_id) const;

	/// Use `std::move()` to transfer `p_new_realtime_object` and `p_new_trickled_objects`.
	void sync_group_replace_objects(SyncGroupId p_group_id, std::vector<SyncGroup::SimulatedObjectInfo> &&p_new_realtime_objects, std::vector<SyncGroup::TrickledObjectInfo> &&p_new_trickled_objects);

	void sync_group_remove_all_objects(SyncGroupId p_group_id);
	void sync_group_move_peer_to(int p_peer_id, SyncGroupId p_group_id);
	SyncGroupId sync_group_get_peer_group(int p_peer_id) const;
	const std::vector<int> *sync_group_get_listening_peers(SyncGroupId p_group_id) const;
	const std::vector<int> *sync_group_get_simulating_peers(SyncGroupId p_group_id) const;

	void sync_group_set_trickled_update_rate(ObjectLocalId p_id, SyncGroupId p_group_id, float p_update_rate);
	void sync_group_set_trickled_update_rate(ObjectNetId p_id, SyncGroupId p_group_id, float p_update_rate);
	float sync_group_get_trickled_update_rate(ObjectLocalId p_id, SyncGroupId p_group_id) const;
	float sync_group_get_trickled_update_rate(ObjectNetId p_id, SyncGroupId p_group_id) const;

	void sync_group_notify_scheduled_procedure_changed(ObjectData &p_object_data, ScheduledProcedureId p_scheduled_procedure_id);

	void sync_group_set_user_data(SyncGroupId p_group_id, uint64_t p_user_ptr);
	uint64_t sync_group_get_user_data(SyncGroupId p_group_id) const;

	/// Returns true during the rewinding process and the preceding snapshot apply,
	/// this is very useful to skip the execution of code during the rewinding process.
	bool is_resyncing() const;

	/// This returns true when the SceneSynchronizer is applying a snapshot just
	/// before starting a rewinding.
	bool is_resetting() const;

	/// Returns true during the rewinding process.
	bool is_rewinding() const;
	bool is_end_sync() const;

	std::size_t get_client_max_frames_storage_size() const;

	/// This function works only on server.
	void force_state_notify(SyncGroupId p_sync_group_id);
	void force_state_notify_all();

	void set_enabled(bool p_enable);
	bool is_enabled() const;

	void set_peer_networking_enable(int p_peer, bool p_enable);
	bool is_peer_networking_enabled(int p_peer) const;

	void on_peer_connected(int p_peer);
	void on_peer_disconnected(int p_peer);

	void init_synchronizer(bool p_was_generating_ids);
	void uninit_synchronizer();
	void reset_synchronizer_mode();

	/// Removes all the ObjectData, changed variables, processing functions.
	void clear();
	/// Removes all the Peers.
	void clear_peers();

	/// Completely reset the SceneSync so the object can be used in a new context.
	/// NOTICE: Setup must be called again onced reset is executed.
	void reset();

	void detect_and_signal_changed_variables(int p_flags);

	void change_events_begin(int p_flag);
	void change_event_add(ObjectData *p_object_data, VarId p_var_id, const VarData &p_old);
	void change_events_flush();

	const std::vector<SimulatedObjectInfo> *client_get_simulated_objects() const;
	bool client_is_simulated_object(ObjectLocalId p_id) const;

	SceneSynchronizerDebugger &get_debugger() const {
		return network_interface->get_debugger();
	};

	float get_time_bank() const {
		return time_bank;
	}

	std::string debug_get_data_objects_table(int columns_count, int table_column_width) const;
	std::string debug_get_data_objects_table(int columns_count, int table_column_width, const std::vector<const ObjectData *> &objects) const;

public: // ------------------------------------------------------------ INTERNAL
	void try_fetch_unnamed_objects_data_names();
	void update_objects_relevancy();

	void process_functions__clear();
	bool process_functions__execute();
	void process_functions__execute_scheduled_procedure();

	ObjectLocalId find_object_local_id(ObjectHandle p_app_object) const;

	ObjectData *get_object_data(ObjectLocalId p_id, bool p_expected = true);
	const ObjectData *get_object_data(ObjectLocalId p_id, bool p_expected = true) const;

	ObjectData *get_object_data(ObjectNetId p_id, bool p_expected = true);
	const ObjectData *get_object_data(ObjectNetId p_id, bool p_expected = true) const;

	PeerNetworkedController *get_local_authority_controller(bool p_expected = true);
	const PeerNetworkedController *get_local_authority_controller(bool p_expected = true) const;

	PeerNetworkedController *get_controller_for_peer(int p_peer, bool p_expected = true);
	const PeerNetworkedController *get_controller_for_peer(int p_peer, bool p_expected = true) const;

	int get_peer_controlling_object(ObjectLocalId Id) const;
	int get_peer_controlling_object(ObjectNetId Id) const;

	/// Return true if this object is controlled by the current peer.
	bool is_locally_controlled(ObjectLocalId Id) const;

	const std::map<int, PeerData> &get_peers() const;
	std::map<int, PeerData> &get_peers();
	PeerData *get_peer_data_for_controller(const PeerNetworkedController &p_controller, bool p_expected = true);
	const PeerData *get_peer_data_for_controller(const PeerNetworkedController &p_controller, bool p_expected = true) const;

	/// Returns the latest generated `ObjectNetId`.
	ObjectNetId get_biggest_object_id() const;

	void reset_controllers();
	void reset_controller(PeerNetworkedController &p_controller);

	/// Read the object variables and store the value if is different from the
	/// previous one and emits a signal.
	void pull_object_changes(ObjectData &p_object_data);

	void drop_object_data(ObjectData &p_object_data);

	void notify_object_data_net_id_changed(ObjectData &p_object_data);

	FrameIndex client_get_last_checked_frame_index() const;

	int fetch_sub_processes_count(float p_delta);

	void notify_undelivered_rpc(ObjectNetId p_id, std::uint8_t p_rpc_index, int p_sender_peer, const DataBuffer &p_db);
	void flush_undelivered_rpc_for(ObjectNetId p_id);

public:
	/// Returns true if this peer is server.
	bool is_server() const;
	/// Returns true if this peer is client.
	bool is_client() const;
	/// Returns true if there is no network.
	bool is_no_network() const;
	/// Returns true if network is enabled.
	bool is_networked() const;
};

class Synchronizer {
protected:
	SceneSynchronizerBase *scene_synchronizer;

public:
	Synchronizer(SceneSynchronizerBase *p_ss);
	virtual ~Synchronizer() = default;

	virtual void clear() = 0;

	virtual bool can_execute_scene_process() const = 0;

	virtual void process(float p_delta) = 0;

	virtual void on_peer_connected(int p_peer_id) {
	}

	virtual void on_peer_disconnected(int p_peer_id) {
	}

	virtual void on_object_data_added(ObjectData &p_object_data) {
	}

	virtual void on_object_data_removed(ObjectData &p_object_data) {
	}

	virtual void on_object_data_name_known(ObjectData &p_object_data) {
	}

	virtual void on_object_data_controller_changed(ObjectData &p_object_data, int p_previous_controlling_peer) {
	}

	virtual void on_variable_added(ObjectData *p_object_data, const std::string &p_var_name) {
	}

	virtual void on_variable_changed(ObjectData *p_object_data, VarId p_var_id, const VarData &p_old_value, int p_flag) {
	}

	virtual void on_controller_reset(PeerNetworkedController &p_controller) {
	}

	virtual const std::vector<ObjectData *> &get_active_objects() const = 0;

	SceneSynchronizerDebugger &get_debugger() const {
		return scene_synchronizer->get_debugger();
	}
};

class NoNetSynchronizer final : public Synchronizer {
	friend class SceneSynchronizerBase;

	bool enabled = true;
	uint32_t frame_count = 0;
	std::vector<ObjectData *> active_objects;

public:
	NoNetSynchronizer(SceneSynchronizerBase *p_ss);

	virtual void clear() override;

	virtual bool can_execute_scene_process() const override {
		return true;
	}

	virtual void process(float p_delta) override;
	virtual void on_object_data_added(NS::ObjectData &p_object_data) override;
	virtual void on_object_data_removed(NS::ObjectData &p_object_data) override;

	virtual const std::vector<ObjectData *> &get_active_objects() const override {
		return active_objects;
	}

	void set_enabled(bool p_enabled);
	bool is_enabled() const;
};

class ServerSynchronizer final : public Synchronizer {
	friend class SceneSynchronizerBase;

	std::map<int, NS::PeerServerData> peers_data;

	float objects_relevancy_update_timer = 0.0;
	uint32_t epoch = 0;
	/// This array contains a map between the peers and the relevant objects.
	std::vector<SyncGroup> sync_groups;
	std::vector<ObjectData *> active_objects;

	enum class SnapshotObjectGeneratorMode {
		/// The snapshot will include The NetId and the object name and all the changed variables.
		NORMAL,
		/// The snapshot will include The object name only.
		FORCE_NODE_PATH_ONLY,
		/// The snapshot will contain everything no matter what.
		FORCE_FULL,
	};

public:
	ServerSynchronizer(SceneSynchronizerBase *p_ss);

	virtual void clear() override;

	virtual bool can_execute_scene_process() const override {
		return true;
	}

	virtual void process(float p_delta) override;
	virtual void on_peer_connected(int p_peer_id) override;
	virtual void on_peer_disconnected(int p_peer_id) override;
	virtual void on_object_data_added(NS::ObjectData &p_object_data) override;
	virtual void on_object_data_removed(NS::ObjectData &p_object_data) override;
	virtual void on_object_data_name_known(ObjectData &p_object_data) override;
	virtual void on_object_data_controller_changed(ObjectData &p_object_data, int p_previous_controlling_peer) override;
	virtual void on_variable_added(NS::ObjectData *p_object_data, const std::string &p_var_name) override;
	virtual void on_variable_changed(NS::ObjectData *p_object_data, VarId p_var_id, const VarData &p_old_value, int p_flag) override;

	virtual const std::vector<ObjectData *> &get_active_objects() const override {
		return active_objects;
	}

	void notify_need_snapshot_asap(int p_peer);
	void notify_need_full_snapshot(int p_peer, bool p_notify_ASAP);

	SyncGroupId sync_group_create();
	/// IMPORTANT: The pointer returned is invalid at the end of the scope executing this function. Never store it.
	const SyncGroup *sync_group_get(SyncGroupId p_group_id) const;

	void sync_group_add_object(ObjectData *p_object_data, SyncGroupId p_group_id, bool p_realtime);
	void sync_group_remove_object(ObjectData *p_object_data, SyncGroupId p_group_id);
	void sync_group_fetch_object_grups(const ObjectData *p_object_data, std::vector<SyncGroupId> &r_simulated_groups, std::vector<SyncGroupId> &r_trickled_groups) const;
	void sync_group_fetch_object_simulating_peers(const ObjectData &p_object_data, std::vector<int> &r_simulating_peers) const;
	void sync_group_set_simulated_partial_update_timespan_seconds(const ObjectData &p_object_data, SyncGroupId p_group_id, bool p_partial_update_enabled, float p_update_timespan);
	bool sync_group_is_simulated_partial_updating(const ObjectData &p_object_data, SyncGroupId p_group_id) const;
	float sync_group_get_simulated_partial_update_timespan_seconds(const ObjectData &p_object_data, SyncGroupId p_group_id) const;
	void sync_group_replace_object(SyncGroupId p_group_id, std::vector<SyncGroup::SimulatedObjectInfo> &&p_new_realtime_nodes, std::vector<SyncGroup::TrickledObjectInfo> &&p_new_trickled_nodes);
	void sync_group_remove_all_objects(SyncGroupId p_group_id);
	void sync_group_move_peer_to(int p_peer_id, SyncGroupId p_group_id);
	void sync_group_update(int p_peer_id);
	const std::vector<int> *sync_group_get_listening_peers(SyncGroupId p_group_id) const;
	const std::vector<int> *sync_group_get_simulating_peers(SyncGroupId p_group_id) const;

	void set_peer_networking_enable(int p_peer, bool p_enable);

	void sync_group_set_trickled_update_rate(NS::ObjectData *p_object_data, SyncGroupId p_group_id, float p_update_rate);
	float sync_group_get_trickled_update_rate(const NS::ObjectData *p_object_data, SyncGroupId p_group_id) const;

	void sync_group_notify_scheduled_procedure_changed(ObjectData &p_object_data, ScheduledProcedureId p_scheduled_procedure_id);

	void sync_group_set_user_data(SyncGroupId p_group_id, uint64_t p_user_ptr);
	uint64_t sync_group_get_user_data(SyncGroupId p_group_id) const;

	void sync_group_debug_print();

	void process_snapshot_notificator();

	void generate_snapshot(
			bool p_force_full_snapshot,
			const SyncGroup &p_group,
			const std::vector<std::size_t> &p_partial_update_simulated_objects_info_indices,
			DataBuffer &r_snapshot_db) const;

	void generate_snapshot_object_data(
			const ObjectData &p_object_data,
			SnapshotObjectGeneratorMode p_mode,
			const SyncGroup::Change &p_change,
			DataBuffer &r_snapshot_db) const;

	void process_trickled_sync(float p_delta);
	void update_peers_net_statistics(float p_delta);
	void send_net_stat_to_peer(int p_peer, PeerData &p_peer_data);
};

class ClientSynchronizer final : public Synchronizer {
	friend class SceneSynchronizerBase;

public:
	float acceleration_fps_speed = 0.0;
	float acceleration_fps_timer = 0.0;
	float pretended_delta = 1.0;

	struct ClientParsingErrors {
		int objects = 0;
		int missing_object_names = 0;
	};

	int snapshot_parsing_failures = 0;
#ifdef NS_DEBUG_ENABLED
	std::uint64_t snapshot_parsing_failures_ever = 0;
#endif

	std::vector<SimulatedObjectInfo> simulated_objects;
	std::vector<ObjectData *> active_objects;
	PeerNetworkedController *player_controller = nullptr;
	std::map<ObjectNetId, std::string> objects_names;
	std::map<ObjectNetId, SchemeId> objects_schemes_id;
	std::map<ObjectNetId, std::vector<DataBuffer>> objects_pending_snapshots;

	RollingUpdateSnapshot last_received_snapshot;
	std::deque<Snapshot> client_snapshots;
	FrameIndex last_received_server_snapshot_index = FrameIndex::NONE;
	std::optional<Snapshot> last_received_server_snapshot;
	FrameIndex last_checked_input = FrameIndex::NONE;
	bool enabled = true;
	bool want_to_enable = false;

	bool need_full_snapshot_notified = false;

	struct EndSyncEvent {
		ObjectData *object_data = nullptr;
		VarId var_id = VarId::NONE;
		VarData old_value;

		EndSyncEvent() = default;

		EndSyncEvent(const EndSyncEvent &p_other) :
			EndSyncEvent(p_other.object_data, p_other.var_id, p_other.old_value) {
		}

		EndSyncEvent(
				ObjectData *p_object_data,
				VarId p_var_id,
				const VarData &p_old_value) :
			object_data(p_object_data),
			var_id(p_var_id) {
			old_value.copy(p_old_value);
		}

		EndSyncEvent &operator=(const EndSyncEvent &p_se) {
			object_data = p_se.object_data;
			var_id = p_se.var_id;
			old_value.copy(p_se.old_value);
			return *this;
		}

		bool operator==(const EndSyncEvent &p_other) const {
			return object_data == p_other.object_data &&
					var_id == p_other.var_id;
		}

		bool operator<(const EndSyncEvent &p_other) const {
			if (object_data->get_net_id() == p_other.object_data->get_net_id()) {
				return var_id < p_other.var_id;
			} else {
				return object_data->get_net_id().id < p_other.object_data->get_net_id().id;
			}
		}
	};

	std::vector<EndSyncEvent> sync_end_events;

	struct TrickledSyncInterpolationData {
		ObjectData *od = nullptr;
		DataBuffer past_epoch_buffer;
		DataBuffer future_epoch_buffer;

		uint32_t past_epoch = UINT32_MAX;
		uint32_t future_epoch = UINT32_MAX;
		float epochs_timespan = 1.0;
		float alpha = 0.0;

		TrickledSyncInterpolationData() = delete;

		TrickledSyncInterpolationData(const TrickledSyncInterpolationData &p_dss) :
			od(p_dss.od),
			past_epoch_buffer(p_dss.past_epoch_buffer),
			future_epoch_buffer(p_dss.future_epoch_buffer),
			past_epoch(p_dss.past_epoch),
			future_epoch(p_dss.future_epoch),
			epochs_timespan(p_dss.epochs_timespan),
			alpha(p_dss.alpha) {
		}

		TrickledSyncInterpolationData &operator=(const TrickledSyncInterpolationData &p_dss) {
			od = p_dss.od;
			past_epoch_buffer.copy(p_dss.past_epoch_buffer);
			future_epoch_buffer.copy(p_dss.future_epoch_buffer);
			past_epoch = p_dss.past_epoch;
			future_epoch = p_dss.future_epoch;
			epochs_timespan = p_dss.epochs_timespan;
			alpha = p_dss.alpha;
			return *this;
		}

		TrickledSyncInterpolationData(SceneSynchronizerDebugger &p_debugger) :
			past_epoch_buffer(p_debugger),
			future_epoch_buffer(p_debugger) {
		}

		TrickledSyncInterpolationData(ObjectData *p_nd, SceneSynchronizerDebugger &p_debugger) :
			od(p_nd),
			past_epoch_buffer(p_debugger),
			future_epoch_buffer(p_debugger) {
		}

		TrickledSyncInterpolationData(
				ObjectData *p_nd,
				DataBuffer p_past_epoch_buffer,
				DataBuffer p_future_epoch_buffer) :
			od(p_nd),
			past_epoch_buffer(p_past_epoch_buffer),
			future_epoch_buffer(p_future_epoch_buffer) {
		}

		bool operator==(const TrickledSyncInterpolationData &o) const {
			return od == o.od;
		}
	};

	std::vector<TrickledSyncInterpolationData> trickled_sync_array;

public:
	ClientSynchronizer(SceneSynchronizerBase *p_node);


	virtual void clear() override;

	virtual bool can_execute_scene_process() const override;
	virtual void process(float p_delta) override;
	virtual void on_object_data_added(ObjectData &p_object_data) override;
	virtual void on_object_data_removed(ObjectData &p_object_data) override;
	virtual void on_object_data_name_known(ObjectData &p_object_data) override;
	virtual void on_variable_changed(ObjectData *p_object_data, VarId p_var_id, const VarData &p_old_value, int p_flag) override;
	void signal_end_sync_changed_variables_events();
	virtual void on_controller_reset(PeerNetworkedController &p_controller) override;
	virtual const std::vector<ObjectData *> &get_active_objects() const override;

	void receive_snapshot(DataBuffer &p_snapshot);
	bool parse_sync_data(
			DataBuffer &p_snapshot,
			void *p_user_pointer,
			ClientParsingErrors &r_parsing_errors,
			void (*p_notify_parsing_failed_for_object)(void *p_user_pointer, ObjectData &p_object_data),
			void (*p_notify_update_mode)(void *p_user_pointer, bool p_is_partial_update),
			void (*p_parse_global_frame_index)(void *p_user_pointer, GlobalFrameIndex p_global_frame_index),
			void (*p_custom_data_parse)(void *p_user_pointer, VarData &&p_custom_data),
			void (*p_object_parse)(void *p_user_pointer, ObjectData *p_object_data),
			// NOTE: The frame index meta is not initialized by this function,
			// and it's up to the calling function doint it.
			bool (*p_peers_frame_index_parse)(void *p_user_pointer, std::map<int, FrameIndexWithMeta> &&p_frames_index),
			void (*p_variable_parse)(void *p_user_pointer, ObjectData &p_object_data, VarId p_var_id, VarData &&p_value),
			void (*p_scheduled_procedure_parse)(void *p_user_pointer, ObjectData &p_object_data, ScheduledProcedureId p_procedure_id, ScheduledProcedureSnapshot &&p_value),
			void (*p_simulated_object_add_or_remove_parse)(void *p_user_pointer, bool p_add, SimulatedObjectInfo &&p_simulated_objects),
			void (*p_simulated_objects_parse)(void *p_user_pointer, std::vector<SimulatedObjectInfo> &&p_simulated_objects));
	bool parse_sync_data_object_info(
			DataBuffer &p_snapshot,
			void *p_user_pointer,
			ObjectData &p_object_data,
			void (*p_variable_parse)(void *p_user_pointer, ObjectData &p_object_data, VarId p_var_id, VarData &&p_value),
			void (*p_scheduled_procedure_parse)(void *p_user_pointer, ObjectData &p_object_data, ScheduledProcedureId p_procedure_id, ScheduledProcedureSnapshot &&p_value));


	void set_enabled(bool p_enabled);

	void receive_trickled_sync_data(const std::vector<std::uint8_t> &p_data);
	void process_trickled_sync(float p_delta);

	void remove_object_from_trickled_sync(NS::ObjectData *p_object_data);

private:
	void try_fetch_pending_snapshot_objects();

	/// Store object data organized per controller.
	void store_snapshot();

	void store_controllers_snapshot(const RollingUpdateSnapshot &p_snapshot);

	void process_server_sync();
	void process_received_server_state();

	bool __pcr__fetch_recovery_info(
			const FrameIndex p_input_id,
			const int p_rewind_frame_count,
			const struct PlayerController &p_local_player_controller,
			Snapshot &r_no_rewind_recover);

	void __pcr__sync__rewind(
			FrameIndex p_last_checked_input_id,
			const int p_rewind_frame_count,
			const PlayerController &p_local_player_controller);

	void __pcr__rewind(
			const FrameIndex p_checkable_frame_index,
			const int p_rewind_frame_count,
			PeerNetworkedController *p_controller,
			PlayerController *p_player_controller);

	void __pcr__sync__no_rewind(
			const Snapshot &p_postponed_recover);

	void __pcr__no_rewind(
			const FrameIndex p_checkable_frame_index,
			PlayerController *p_player_controller);

	void process_paused_controller_recovery();

	/// Returns the amount of frames to process for this frame.
	int calculates_sub_ticks(const float p_delta);
	void process_simulation(float p_delta);

	bool parse_snapshot(DataBuffer &p_snapshot, bool p_is_server_snapshot);
	void finalize_object_data_synchronization(ObjectData &p_object_data);

	void notify_server_full_snapshot_is_needed();

	void update_client_snapshot(Snapshot &p_snapshot);
	void update_simulated_objects_list(const std::vector<SimulatedObjectInfo> &p_simulated_objects);

public:
	void apply_snapshot(
			const Snapshot &p_snapshot,
			const int p_flag,
			// The frames rewinded just after this function.
			const int p_frame_count_to_rewind,
			std::vector<std::string> *r_applied_data_info,
			const bool p_skip_custom_data = false,
			const bool p_skip_simulated_objects_update = false,
			const bool p_disable_apply_non_doll_controlled_only = false,
			const bool p_skip_snapshot_applied_event_broadcast = false,
			const bool p_skip_change_event = false,
			const bool p_skip_scheduled_procedures = false);
};

/// This is used to make sure we can safely convert any `BaseType` defined by
// the user to `void*`.
template <class BaseType, class NetInterfaceClass>
class SceneSynchronizer : public SceneSynchronizerBase {
	NetInterfaceClass custom_network_interface;

public:
	// Note: Pedantic checks is meant to be used with unit tests (mainly).
	// It does a bunch of extra checks that ensure write/read.
	// Eventually can be turned on to also verify everything works in game too.
	SceneSynchronizer(bool p_pedantic_checks = false, bool p_disable_client_sub_ticks = false) :
		SceneSynchronizerBase(&custom_network_interface, p_pedantic_checks, p_disable_client_sub_ticks) {
	}

	NetInterfaceClass &get_network_interface() {
		return custom_network_interface;
	}

	const NetInterfaceClass &get_network_interface() const {
		return custom_network_interface;
	}

	static ObjectHandle to_handle(const BaseType *p_app_object) {
		return ObjectHandle{ { reinterpret_cast<std::intptr_t>(p_app_object) } };
	}

	static BaseType *from_handle(ObjectHandle p_app_object_handle) {
		return reinterpret_cast<BaseType *>(p_app_object_handle.id);
	}
};

NS_NAMESPACE_END