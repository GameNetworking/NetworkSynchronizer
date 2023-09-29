#pragma once

#include "core/object/object.h"
#include "modules/network_synchronizer/core/processor.h"
#include "modules/network_synchronizer/godot4/gd_network_interface.h"
#include "modules/network_synchronizer/scene_synchronizer.h"
#include "scene/main/node.h"

#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/net_utilities.h"

class GdSceneSynchronizer : public Node, public NS::SynchronizerManager {
	GDCLASS(GdSceneSynchronizer, Node);

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
	void set_max_deferred_nodes_per_update(int p_rate);
	int get_max_deferred_nodes_per_update() const;

	void set_server_notify_state_interval(real_t p_interval);
	real_t get_server_notify_state_interval() const;

	void set_comparison_float_tolerance(real_t p_tolerance);
	real_t get_comparison_float_tolerance() const;

	void set_nodes_relevancy_update_time(real_t p_time);
	real_t get_nodes_relevancy_update_time() const;

public: // ---------------------------------------- Scene Synchronizer Interface
	virtual void on_init_synchronizer(bool p_was_generating_ids) override;
	virtual void on_uninit_synchronizer() override;

	virtual void debug_only_validate_nodes() override;

	virtual void on_add_object_data(NS::ObjectData &p_object_data) override;

	virtual void update_nodes_relevancy() override;

	virtual void snapshot_add_custom_data(const NS::SyncGroup *p_group, Vector<Variant> &r_snapshot_data) override {}
	virtual bool snapshot_extract_custom_data(const Vector<Variant> &p_snapshot_data, uint32_t p_snap_data_index, LocalVector<const Variant *> &r_out) const override { return true; }
	virtual void snapshot_apply_custom_data(const Vector<Variant> &p_custom_data) override {}

	virtual NS::ObjectHandle fetch_app_object(const std::string &p_object_name) override;
	virtual uint64_t get_object_id(NS::ObjectHandle p_app_object_handle) const override;
	virtual std::string get_object_name(NS::ObjectHandle p_app_object_handle) const override;
	virtual void setup_synchronizer_for(NS::ObjectHandle p_app_object_handle) override;
	virtual void set_variable(NS::ObjectHandle p_app_object_handle, const char *p_name, const Variant &p_val) override;
	virtual bool get_variable(NS::ObjectHandle p_app_object_handle, const char *p_name, Variant &p_val) const override;

	virtual NS::NetworkedControllerBase *extract_network_controller(NS::ObjectHandle p_app_object_handle) override;
	virtual const NS::NetworkedControllerBase *extract_network_controller(NS::ObjectHandle p_app_object_handle) const override;

public: // ------------------------------------------------------- RPC Interface
	// This funtion is used to sync data betweend the server and the client.
	void _rpc_net_sync_reliable(const Vector<Variant> &p_args);
	// This funtion is used to sync data betweend the server and the client.
	void _rpc_net_sync_unreliable(const Vector<Variant> &p_args);

public: // ---------------------------------------------------------------- APIs
	virtual void reset_synchronizer_mode();
	void clear();

	/// Register a new node and returns its `NodeData`.
	NS::ObjectData *register_node(Node *p_node);
	uint32_t register_node_gdscript(Node *p_node);
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

	/// Setup the deferred sync method for this specific node.
	/// The deferred-sync is different from the realtime-sync because the data
	/// is streamed and not simulated.
	void setup_deferred_sync(Node *p_node, const Callable &p_collect_epoch_func, const Callable &p_apply_epoch_func);

	/// Creates a realtime sync group containing a list of nodes.
	/// The Peers listening to this group will receive the updates only
	/// from the nodes within this group.
	virtual SyncGroupId sync_group_create();
	const NS::SyncGroup *sync_group_get(SyncGroupId p_group_id) const;

	void sync_group_add_node_by_id(uint32_t p_net_id, SyncGroupId p_group_id, bool p_realtime);
	void sync_group_add_node(NS::ObjectData *p_object_data, SyncGroupId p_group_id, bool p_realtime);
	void sync_group_remove_node_by_id(uint32_t p_net_id, SyncGroupId p_group_id);
	void sync_group_remove_node(NS::ObjectData *p_object_data, SyncGroupId p_group_id);

	/// Use `std::move()` to transfer `p_new_realtime_nodes` and `p_new_deferred_nodes`.
	void sync_group_replace_nodes(SyncGroupId p_group_id, LocalVector<NS::SyncGroup::RealtimeNodeInfo> &&p_new_realtime_nodes, LocalVector<NS::SyncGroup::DeferredNodeInfo> &&p_new_deferred_nodes);

	void sync_group_remove_all_nodes(SyncGroupId p_group_id);
	void sync_group_move_peer_to(int p_peer_id, SyncGroupId p_group_id);
	SyncGroupId sync_group_get_peer_group(int p_peer_id) const;
	const LocalVector<int> *sync_group_get_peers(SyncGroupId p_group_id) const;

	void sync_group_set_deferred_update_rate_by_id(uint32_t p_node_id, SyncGroupId p_group_id, real_t p_update_rate);
	void sync_group_set_deferred_update_rate(NS::ObjectData *p_object_data, SyncGroupId p_group_id, real_t p_update_rate);
	real_t sync_group_get_deferred_update_rate_by_id(uint32_t p_node_id, SyncGroupId p_group_id) const;
	real_t sync_group_get_deferred_update_rate(const NS::ObjectData *p_object_data, SyncGroupId p_group_id) const;

	void sync_group_set_user_data(SyncGroupId p_group_id, uint64_t p_user_ptr);
	uint64_t sync_group_get_user_data(SyncGroupId p_group_id) const;

	void start_tracking_scene_changes(Object *p_diff_handle) const;
	void stop_tracking_scene_changes(Object *p_diff_handle) const;
	Variant pop_scene_changes(Object *p_diff_handle) const;
	void apply_scene_changes(const Variant &p_sync_data);

	bool is_recovered() const;
	bool is_resetted() const;
	bool is_rewinding() const;
	bool is_end_sync() const;

	void force_state_notify(SyncGroupId p_sync_group_id);
	void force_state_notify_all();

	void set_enabled(bool p_enable);

	void set_peer_networking_enable(int p_peer, bool p_enable);
	bool is_peer_networking_enable(int p_peer) const;

	/// Returns true if this peer is server.
	bool is_server() const;
	/// Returns true if this peer is client.
	bool is_client() const;
	/// Returns true if there is no network.
	bool is_no_network() const;
	/// Returns true if network is enabled.
	bool is_networked() const;
};

VARIANT_ENUM_CAST(NetEventFlag)
VARIANT_ENUM_CAST(ProcessPhase)
