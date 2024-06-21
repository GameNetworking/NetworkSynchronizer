#pragma once

#include "core/object/object.h"
#include "modules/network_synchronizer/core/data_buffer.h"
#include "modules/network_synchronizer/core/processor.h"
#include "modules/network_synchronizer/core/var_data.h"
#include "modules/network_synchronizer/godot4/gd_network_interface.h"
#include "modules/network_synchronizer/scene_synchronizer.h"
#include "scene/main/node.h"

#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/core/net_utilities.h"
#include "modules/network_synchronizer/core/scene_synchronizer_debugger.h"

class GdFileSystem : public NS::FileSystem {
	virtual std::string get_base_dir() const override;
	virtual std::string get_date() const override;
	virtual std::string get_time() const override;
	virtual bool make_dir_recursive(const std::string &p_dir_path, bool p_erase_content) const override;
	virtual bool store_file_string(const std::string &p_path, const std::string &p_string_file) const override;
	virtual bool store_file_buffer(const std::string &p_path, const std::uint8_t *p_src, uint64_t p_length) const override;
	virtual bool file_exists(const std::string &p_path) const override;
};

class GdSceneSynchronizer : public Node, public NS::SynchronizerManager {
	GDCLASS(GdSceneSynchronizer, Node);

public:
	static const uint32_t GLOBAL_SYNC_GROUP_ID;

public:
	static void _bind_methods();

	GDVIRTUAL0(_update_nodes_relevancy);

	typedef NS::SceneSynchronizer<Node, GdNetworkInterface> SyncClass;
	SyncClass scene_synchronizer;

	// Just used to detect when the low level peer change.
	void *low_level_peer = nullptr;

	NS::PHandler event_handler_sync_started = NS::NullPHandler;
	NS::PHandler event_handler_sync_paused = NS::NullPHandler;
	NS::PHandler event_handler_peer_status_updated = NS::NullPHandler;
	NS::PHandler event_handler_state_validated = NS::NullPHandler;
	NS::PHandler event_handler_rewind_frame_begin = NS::NullPHandler;
	NS::PHandler event_handler_desync_detected = NS::NullPHandler;

public:
	GdSceneSynchronizer();
	~GdSceneSynchronizer();

	void _notification(int p_what);

public: // ---------------------------------------------------------- Properties
	void set_netstats_update_interval_sec(float p_delay_in_ms);
	float get_netstats_update_interval_sec() const;

	void set_max_fps_acceleration_percentage(double p_acceleration);
	double get_max_fps_acceleration_percentage() const;

	void set_max_trickled_nodes_per_update(int p_rate);
	int get_max_trickled_nodes_per_update() const;

	void set_frame_confirmation_timespan(real_t p_interval);
	real_t get_frame_confirmation_timespan() const;

	void set_nodes_relevancy_update_time(real_t p_time);
	real_t get_nodes_relevancy_update_time() const;

	void set_frames_per_seconds(int p_fps);
	int get_frames_per_seconds() const;

public: // ---------------------------------------- Scene Synchronizer Interface
	virtual void on_init_synchronizer(bool p_was_generating_ids) override;
	virtual void on_uninit_synchronizer() override;

#ifdef NS_DEBUG_ENABLED
	virtual void debug_only_validate_objects() override;
	virtual uint64_t debug_only_get_object_id(NS::ObjectHandle p_app_object_handle) const override;
#endif

	virtual void on_add_object_data(NS::ObjectData &p_object_data) override;

	virtual void update_objects_relevancy() override;

	virtual NS::ObjectHandle fetch_app_object(const std::string &p_object_name) override;
	virtual std::string get_object_name(NS::ObjectHandle p_app_object_handle) const override;
	virtual void setup_synchronizer_for(NS::ObjectHandle p_app_object_handle, NS::ObjectLocalId p_id) override;

public: // ------------------------------------------------------- RPC Interface
	// This funtion is used to sync data betweend the server and the client.
	void _rpc_net_sync_reliable(const Vector<uint8_t> &p_args);
	// This funtion is used to sync data betweend the server and the client.
	void _rpc_net_sync_unreliable(const Vector<uint8_t> &p_args);

public: // ---------------------------------------------------------------- APIs
	virtual void reset_synchronizer_mode();
	void clear();

	/// Register a new node and returns its `NodeData`.
	NS::ObjectLocalId register_node(Node *p_node);
	uint32_t register_node_gdscript(Node *p_node);
	void setup_controller(
			Node *p_node,
			int p_peer,
			const Callable &p_collect_input_func,
			const Callable &p_are_inputs_different_func,
			const Callable &p_process_func);
	void unregister_node(Node *p_node);

	/// Returns the node ID.
	/// This may return `UINT32_MAX` in various cases:
	/// - The node is not registered.
	/// - The client doesn't know the ID yet.
	uint32_t get_node_id(Node *p_node);
	Node *get_node_from_id(uint32_t p_id, bool p_expected = true);
	const Node *get_node_from_id_const(uint32_t p_id, bool p_expected = true) const;

	void register_variable(Node *p_node, const StringName &p_variable);
	void unregister_variable(Node *p_node, const StringName &p_variable);

	/// Returns the variable ID relative to the `Node`.
	/// This may return `UINT32_MAX` in various cases:
	/// - The node is not registered.
	/// - The variable is not registered.
	/// - The client doesn't know the ID yet.
	uint32_t get_variable_id(Node *p_node, const StringName &p_variable);

	void set_skip_rewinding(Node *p_node, const StringName &p_variable, bool p_skip_rewinding);

	uint64_t track_variable_changes(Array p_nodes, Array p_vars, const Callable &p_callable, NetEventFlag p_flags = NetEventFlag::DEFAULT);
	void untrack_variable_changes(uint64_t p_handle);

	/// You can use the macro `callable_mp()` to register custom C++ function.
	uint64_t register_process(Node *p_node, ProcessPhase p_phase, const Callable &p_callable);
	void unregister_process(Node *p_node, ProcessPhase p_phase, uint64_t p_handler);

	void setup_simulated_sync(
			Node *p_node,
			const Callable &p_collect,
			const Callable &p_get_size,
			const Callable &p_are_equals,
			const Callable &p_process);

	/// Setup the trickled sync method for this specific node.
	/// The trickled-sync is different from the realtime-sync because the data
	/// is streamed and not simulated.
	void setup_trickled_sync(Node *p_node, const Callable &p_collect_epoch_func, const Callable &p_apply_epoch_func);

	Array local_controller_get_controlled_nodes() const;

	int get_peer_latency_ms(int p_peer) const;
	int get_peer_latency_jitter_ms(int p_peer) const;
	float get_peer_packet_loss_percentage(int p_peer) const;

	bool client_is_object_simulating(Node *p_node) const;
	bool client_is_object_simulating(NS::ObjectLocalId p_id) const;
	bool client_is_object_simulating(NS::ObjectNetId p_id) const;

	/// Creates a realtime sync group containing a list of nodes.
	/// The Peers listening to this group will receive the updates only
	/// from the nodes within this group.
	virtual uint32_t sync_group_create();
	const NS::SyncGroup *sync_group_get(uint32_t p_group_id) const;

	void sync_group_add_node_by_id(uint32_t p_net_id, uint32_t p_group_id, bool p_realtime);
	void sync_group_add_node(NS::ObjectData *p_object_data, uint32_t p_group_id, bool p_realtime);
	void sync_group_remove_node_by_id(uint32_t p_net_id, uint32_t p_group_id);
	void sync_group_remove_node(NS::ObjectData *p_object_data, uint32_t p_group_id);

	/// Use `std::move()` to transfer `p_new_realtime_nodes` and `p_new_trickled_nodes`.
	void sync_group_replace_nodes(uint32_t p_group_id, std::vector<NS::SyncGroup::SimulatedObjectInfo> &&p_new_realtime_nodes, std::vector<NS::SyncGroup::TrickledObjectInfo> &&p_new_trickled_nodes);

	void sync_group_remove_all_nodes(uint32_t p_group_id);
	void sync_group_move_peer_to(int p_peer_id, uint32_t p_group_id);
	uint32_t sync_group_get_peer_group(int p_peer_id) const;
	const std::vector<int> *sync_group_get_listening_peers(uint32_t p_group_id) const;

	void sync_group_set_trickled_update_rate_by_id(uint32_t p_node_id, uint32_t p_group_id, real_t p_update_rate);
	void sync_group_set_trickled_update_rate(NS::ObjectData *p_object_data, uint32_t p_group_id, real_t p_update_rate);
	real_t sync_group_get_trickled_update_rate_by_id(uint32_t p_node_id, uint32_t p_group_id) const;
	real_t sync_group_get_trickled_update_rate(const NS::ObjectData *p_object_data, uint32_t p_group_id) const;

	void sync_group_set_user_data(uint32_t p_group_id, uint64_t p_user_ptr);
	uint64_t sync_group_get_user_data(uint32_t p_group_id) const;

	bool is_recovered() const;
	bool is_resetted() const;
	bool is_rewinding() const;
	bool is_end_sync() const;

	void force_state_notify(uint32_t p_sync_group_id);
	void force_state_notify_all();

	void set_enabled(bool p_enable);

	void set_peer_networking_enable(int p_peer, bool p_enable);
	bool is_peer_networking_enabled(int p_peer) const;

	/// Returns true if this peer is server.
	bool is_server() const;
	/// Returns true if this peer is client.
	bool is_client() const;
	/// Returns true if there is no network.
	bool is_no_network() const;
	/// Returns true if network is enabled.
	bool is_networked() const;

	static void encode(NS::DataBuffer &r_buffer, const NS::VarData &p_val);
	static void decode(NS::VarData &r_val, NS::DataBuffer &p_buffer, std::uint8_t p_variable_type);

	static void convert(Variant &r_variant, const NS::VarData &p_vd);
	static void convert(NS::VarData &r_vd, const Variant &p_variant);

	static bool compare(const NS::VarData &p_A, const NS::VarData &p_B);

	static std::string stringify(const NS::VarData &p_var_data, bool p_verbose);
};

VARIANT_ENUM_CAST(NetEventFlag)
VARIANT_ENUM_CAST(ProcessPhase)
