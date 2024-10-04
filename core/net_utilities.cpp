#include "net_utilities.h"

#include "../scene_synchronizer.h"
#include "ensure.h"
#include "object_data.h"
#include "peer_networked_controller.h"
#include "scene_synchronizer_debugger.h"

NS::SceneSynchronizerDebugger &NS::SyncGroup::get_debugger() const {
	return scene_sync->get_debugger();
}

void NS::SyncGroup::advance_timer_state_notifier(
		const float p_delta,
		const float p_frame_confirmation_timespan,
		const int p_max_objects_count_per_partial_update,
		bool &r_send_update,
		std::vector<std::size_t> &r_partial_update_simulated_objects_info_indices) {
	// Notify the state if needed
	state_notifier_timer += p_delta;
	r_send_update = state_notifier_timer >= p_frame_confirmation_timespan;
	if (r_send_update) {
		state_notifier_timer = 0.0;
	} else {
		// No state update, verify if this SyncGroup does partial updates.
		update_partial_update_list();

		for (std::size_t index : partial_update_simulated_sync_objects) {
			simulated_sync_objects[index].last_partial_update_timer += p_delta;
			if (
				simulated_sync_objects[index].last_partial_update_timer >= simulated_sync_objects[index].partial_update_timespan_sec
				&& r_partial_update_simulated_objects_info_indices.size() < p_max_objects_count_per_partial_update
				&& (simulated_sync_objects[index].change.unknown
					|| simulated_sync_objects[index].change.vars.size() > 0)) {
				r_partial_update_simulated_objects_info_indices.push_back(index);
				simulated_sync_objects[index].last_partial_update_timer = 0.0f;
			}
		}

		if (partial_update_simulated_sync_objects.size() > p_max_objects_count_per_partial_update) {
			// Now move all the added indices behind so the next frame we give them less priority.
			// This ensures that all the objects get updated at some point.
			for (std::size_t index : r_partial_update_simulated_objects_info_indices) {
				VecFunc::remove(partial_update_simulated_sync_objects, index);
				partial_update_simulated_sync_objects.push_back(index);
			}
		}

		r_send_update = r_partial_update_simulated_objects_info_indices.size() > 0;
	}
}

void NS::SyncGroup::force_state_notify() {
	// Sets a very big number and ensure the state notify is triggered on the next frame.
	// NOTE the -100 was added to make it very unlikely to cause a float overflow.
	state_notifier_timer = std::numeric_limits<float>::max() - 100.0f;
}

bool NS::SyncGroup::is_realtime_node_list_changed() const {
	return simulated_sync_objects_ADDED.size() > 0 || simulated_sync_objects_REMOVED.size() > 0;
}

bool NS::SyncGroup::is_trickled_node_list_changed() const {
	return trickled_sync_objects_list_changed;
}

const std::vector<int> NS::SyncGroup::get_peers_with_newly_calculated_latency() const {
	return peers_with_newly_calculated_latency;
}

const std::vector<NS::SyncGroup::SimulatedObjectInfo> &NS::SyncGroup::get_simulated_sync_objects() const {
	return simulated_sync_objects;
}

const std::vector<NS::SyncGroup::TrickledObjectInfo> &NS::SyncGroup::get_trickled_sync_objects() const {
	return trickled_sync_objects;
}

std::vector<NS::SyncGroup::TrickledObjectInfo> &NS::SyncGroup::get_trickled_sync_objects() {
	return trickled_sync_objects;
}

void NS::SyncGroup::mark_changes_as_notified(bool p_is_partial_update, const std::vector<std::size_t> &p_partial_update_simulated_objects_info_indices) {
	if (p_is_partial_update) {
		// When it's a partial update this array is always NOT empty
		NS_ASSERT_COND(p_partial_update_simulated_objects_info_indices.size() > 0);

		for (const std::size_t index : p_partial_update_simulated_objects_info_indices) {
			simulated_sync_objects[index].change.unknown = false;
			simulated_sync_objects[index].change.vars.clear();
		}
	} else {
		// When it isn't a partial update this array is always empty
		NS_ASSERT_COND(p_partial_update_simulated_objects_info_indices.size() <= 0);

		// Mark all the simulated objects as updated
		for (auto &sso : simulated_sync_objects) {
			sso.change.unknown = false;
			sso.change.vars.clear();
		}
	}

	// Mark all the trickled objects as known.
	for (auto &tso : trickled_sync_objects) {
		tso._unknown = false;
	}

	simulated_sync_objects_ADDED.clear();
	simulated_sync_objects_REMOVED.clear();
	trickled_sync_objects_list_changed = false;
	peers_with_newly_calculated_latency.clear();
}

void NS::SyncGroup::add_listening_peer(int p_peer) {
	VecFunc::insert_unique(listening_peers, p_peer);
	notify_simulating_peers_about_listener_status(p_peer, true);
}

void NS::SyncGroup::remove_listening_peer(int p_peer) {
	VecFunc::remove_unordered(listening_peers, p_peer);
	notify_simulating_peers_about_listener_status(p_peer, false);
}

std::size_t NS::SyncGroup::add_new_sync_object(ObjectData *p_object_data, bool p_is_simulated) {
	if (p_is_simulated) {
		// Make sure the node is not contained into the trickled sync.

		const std::size_t tso_index = find_trickled(*p_object_data);
		if (tso_index != VecFunc::index_none()) {
			remove_sync_object(tso_index, false);
		}
	} else {
		// Make sure the node is not contained into the realtime sync.
		const std::size_t rsn_index = find_simulated(*p_object_data);
		if (rsn_index != VecFunc::index_none()) {
			remove_sync_object(rsn_index, true);
		}
	}

	if (p_object_data->get_controlled_by_peer() > 0) {
		// This is a controller with an associated peer, update the networked_peer list.
		// Regardless if it's simulated or not.
		const int peer = p_object_data->get_controlled_by_peer();
		if (VecFunc::insert_unique(networked_peers, peer)) {
			VecFunc::insert_unique(peers_with_newly_calculated_latency, peer);
		}
	}

	if (p_is_simulated) {
		// Add it into the realtime sync list
		std::size_t index = find_simulated(*p_object_data);

		if (index == VecFunc::index_none()) {
			index = simulated_sync_objects.size();
			simulated_sync_objects.push_back(p_object_data);
			VecFunc::insert_unique(simulated_sync_objects_ADDED, p_object_data->get_net_id());
			VecFunc::remove_unordered(simulated_sync_objects_REMOVED, p_object_data->get_net_id());
			partial_update_simulated_sync_objects_changed = true;

			SimulatedObjectInfo &info = simulated_sync_objects[index];

			info.change.unknown = true;

			for (VarId::IdType i = 0; i < VarId::IdType(p_object_data->vars.size()); ++i) {
				notify_new_variable(p_object_data, { i });
			}

			if (p_object_data->get_controlled_by_peer() > 0) {
				VecFunc::insert_unique(simulating_peers, p_object_data->get_controlled_by_peer());
				update_listeners_to_simulating_peer(p_object_data->get_controlled_by_peer(), true);
			}
		}

		return index;
	} else {
		// Add it into the trickled sync nodes
		std::size_t index = find_trickled(*p_object_data);

		if (index == VecFunc::index_none()) {
			index = trickled_sync_objects.size();
			trickled_sync_objects.push_back(p_object_data);
			trickled_sync_objects[index]._unknown = true;
			trickled_sync_objects_list_changed = true;
		}

		return index;
	}
}

void NS::SyncGroup::notify_sync_object_name_is_known(ObjectData &p_object_data) {
	// Notify simulated that the object name changed.
	{
		const std::size_t index = find_simulated(p_object_data);
		if (index != VecFunc::index_none()) {
			SimulatedObjectInfo &info = simulated_sync_objects[index];
			info.change.unknown = true;
		}
	}

	// Notify trickled that the object name changed.
	{
		const std::size_t index = find_trickled(p_object_data);
		if (index != VecFunc::index_none()) {
			trickled_sync_objects[index]._unknown = true;
		}
	}
}

void NS::SyncGroup::remove_sync_object(std::size_t p_index, bool p_is_simulated) {
	int associted_peer = 0;

	if (p_is_simulated) {
		if (simulated_sync_objects[p_index].od->get_controlled_by_peer() > 0) {
			associted_peer = simulated_sync_objects[p_index].od->get_controlled_by_peer();
		}
	} else {
		if (trickled_sync_objects[p_index].od->get_controlled_by_peer() > 0) {
			associted_peer = trickled_sync_objects[p_index].od->get_controlled_by_peer();
		}
	}

	if (p_is_simulated) {
		VecFunc::remove_unordered(simulated_sync_objects_ADDED, simulated_sync_objects[p_index].od->get_net_id());
		VecFunc::insert_unique(simulated_sync_objects_REMOVED, simulated_sync_objects[p_index].od->get_net_id());
		partial_update_simulated_sync_objects_changed = true;
		VecFunc::remove_at_unordered(simulated_sync_objects, p_index);
	} else {
		VecFunc::remove_at_unordered(trickled_sync_objects, p_index);
		trickled_sync_objects_list_changed = true;
	}

	validate_peer_association(associted_peer);
}

void NS::SyncGroup::remove_sync_object(const ObjectData &p_object_data) {
	{
		const std::size_t index = find_simulated(p_object_data);
		if (index != VecFunc::index_none()) {
			remove_sync_object(index, true);
			// No need to check the trickled array. Nodes can be in 1 single array.
			return;
		}
	}

	{
		const std::size_t index = find_trickled(p_object_data);
		if (index != VecFunc::index_none()) {
			remove_sync_object(index, false);
		}
	}
}

template <class T>
void replace_nodes_impl(
		NS::SyncGroup &p_sync_group,
		std::vector<T> &&p_nodes_to_add,
		bool p_is_simulated,
		std::vector<T> &r_sync_group_nodes) {
	for (int i = int(r_sync_group_nodes.size()) - 1; i >= 0; i--) {
		const std::size_t nta_index = NS::VecFunc::find_index(p_nodes_to_add, r_sync_group_nodes[i].od);
		if (nta_index == NS::VecFunc::index_none()) {
			// This node is not part of this sync group, remove it.
			p_sync_group.remove_sync_object(i, p_is_simulated);
		} else {
			// This node is still part of this SyncGroup.
			// Update the existing one.
			r_sync_group_nodes[i].update_from(p_nodes_to_add[nta_index]);

			// Then, make sure not to add again:
			NS::VecFunc::remove_at_unordered(p_nodes_to_add, nta_index);

#ifdef NS_DEBUG_ENABLED
			// Make sure there are no duplicates:
			NS_ASSERT_COND_MSG(!NS::VecFunc::has(p_nodes_to_add, r_sync_group_nodes[i].od), "The function `replace_nodes` must receive unique nodes on each array. Make sure not to add duplicates.");
#endif
		}
	}

	// Add the missing objects now.
	for (int i = 0; i < int(p_nodes_to_add.size()); i++) {
		NS::ObjectData *od = p_nodes_to_add[i].od;

#ifdef NS_DEBUG_ENABLED
		NS_ASSERT_COND_MSG(!NS::VecFunc::has(r_sync_group_nodes, T(od)), "[FATAL] This is impossible to trigger, because the above loop cleaned this.");
#endif

		const std::size_t index = p_sync_group.add_new_sync_object(od, p_is_simulated);
		r_sync_group_nodes[index].update_from(p_nodes_to_add[i]);
	}
}

void NS::SyncGroup::replace_objects(std::vector<SimulatedObjectInfo> &&p_new_simulated_objects, std::vector<TrickledObjectInfo> &&p_new_trickled_nodes) {
	replace_nodes_impl(
			*this,
			std::move(p_new_simulated_objects),
			true,
			simulated_sync_objects);
	partial_update_simulated_sync_objects_changed = true;

	replace_nodes_impl(
			*this,
			std::move(p_new_trickled_nodes),
			false,
			trickled_sync_objects);
}

void NS::SyncGroup::remove_all_nodes() {
	if (!simulated_sync_objects.empty()) {
		simulated_sync_objects_ADDED.clear();
		for (const SimulatedObjectInfo &soi : simulated_sync_objects) {
			VecFunc::insert_unique(simulated_sync_objects_REMOVED, soi.od->get_net_id());
		}
		simulated_sync_objects.clear();
		partial_update_simulated_sync_objects_changed = true;
	}

	if (!trickled_sync_objects.empty()) {
		trickled_sync_objects.clear();
		trickled_sync_objects_list_changed = true;
	}
}

void NS::SyncGroup::notify_new_variable(ObjectData *p_object_data, VarId p_var_id) {
	const std::size_t index = find_simulated(*p_object_data);
	if (index != VecFunc::index_none()) {
		VecFunc::insert_unique(simulated_sync_objects[index].change.vars, p_var_id);
	}
}

void NS::SyncGroup::notify_variable_changed(ObjectData *p_object_data, VarId p_var_id) {
	const std::size_t index = find_simulated(*p_object_data);
	if (index != VecFunc::index_none()) {
		VecFunc::insert_unique(simulated_sync_objects[index].change.vars, p_var_id);
	}
}

void NS::SyncGroup::set_simulated_partial_update_timespan_seconds(const ObjectData &p_object_data, bool p_partial_update_enabled, float p_update_timespan) {
	const std::size_t index = find_simulated(p_object_data);
	if (index != VecFunc::index_none()) {
		simulated_sync_objects[index].partial_update_timespan_sec = p_partial_update_enabled ? std::max(p_update_timespan, 0.0f) : -1.0f;

		if (simulated_sync_objects[index].partial_update_timespan_sec < 0.0f) {
			// The partial update is disabled, so reset the timer.
			simulated_sync_objects[index].last_partial_update_timer = 0.0f;
		}

		partial_update_simulated_sync_objects_changed = true;
	}
}

bool NS::SyncGroup::is_simulated_partial_updating(const ObjectData &p_object_data) const {
	const std::size_t index = find_simulated(p_object_data);
	if (index != VecFunc::index_none()) {
		return simulated_sync_objects[index].partial_update_timespan_sec >= 0.0f;
	}
	return false;
}

float NS::SyncGroup::get_simulated_partial_update_timespan_seconds(const ObjectData &p_object_data) const {
	const std::size_t index = find_simulated(p_object_data);
	if (index != VecFunc::index_none()) {
		return simulated_sync_objects[index].partial_update_timespan_sec;
	}
	return -1.0;
}

void NS::SyncGroup::update_partial_update_list() {
	if (!partial_update_simulated_sync_objects_changed) {
		return;
	}
	partial_update_simulated_sync_objects_changed = false;

	partial_update_simulated_sync_objects.clear();
	for (std::size_t i = 0; i < simulated_sync_objects.size(); i++) {
		if (simulated_sync_objects[i].partial_update_timespan_sec >= 0.0f) {
			partial_update_simulated_sync_objects.push_back(i);
		}
	}
}

void NS::SyncGroup::set_trickled_update_rate(NS::ObjectData *p_object_data, float p_update_rate) {
	const std::size_t index = find_trickled(*p_object_data);
	NS_ENSURE(index != VecFunc::index_none());
	trickled_sync_objects[index].update_rate = p_update_rate;
}

float NS::SyncGroup::get_trickled_update_rate(const NS::ObjectData *p_object_data) const {
	for (int i = 0; i < int(trickled_sync_objects.size()); ++i) {
		if (trickled_sync_objects[i].od == p_object_data) {
			return trickled_sync_objects[i].update_rate;
		}
	}
	get_debugger().print(ERROR, "NodeData " + p_object_data->get_object_name() + " not found into `trickled_sync_objects`.");
	return 0.0;
}

void NS::SyncGroup::sort_trickled_node_by_update_priority() {
	std::sort(
			trickled_sync_objects.begin(),
			trickled_sync_objects.end(),
			[](const TrickledObjectInfo &a, const TrickledObjectInfo &b) {
				return a._update_priority > b._update_priority;
			});
}

void NS::SyncGroup::notify_peer_has_newly_calculated_latency(int p_peer) {
	if (VecFunc::has(networked_peers, p_peer)) {
		VecFunc::insert_unique(peers_with_newly_calculated_latency, p_peer);
	}
}

void NS::SyncGroup::notify_controller_changed(ObjectData &p_object_data, int p_previous_controlling_peer) {
	if (p_object_data.get_controlled_by_peer() == p_previous_controlling_peer) {
		return;
	}

	bool is_in_this_sync_group = false;
	bool is_simulated = false;
	if (find_simulated(p_object_data) != VecFunc::index_none()) {
		is_in_this_sync_group = true;
		is_simulated = true;
	} else if (find_trickled(p_object_data) != VecFunc::index_none()) {
		is_in_this_sync_group = true;
	}

	if (is_in_this_sync_group) {
		validate_peer_association(p_previous_controlling_peer);

		if (p_object_data.get_controlled_by_peer() > 0) {
			const int peer = p_object_data.get_controlled_by_peer();

			if (is_simulated) {
				VecFunc::insert_unique(simulating_peers, peer);
				update_listeners_to_simulating_peer(peer, true);
			}

			if (VecFunc::insert_unique(networked_peers, peer)) {
				VecFunc::insert_unique(peers_with_newly_calculated_latency, peer);
			}
		}

		if (is_simulated) {
			// Mark this net ID as added so on the next state update it's included
			// in the snapshot and the client is updated about the new controlling peer.
			VecFunc::insert_unique(simulated_sync_objects_ADDED, p_object_data.get_net_id());
		}
	}
}

void NS::SyncGroup::notify_simulating_peers_about_listener_status(int p_peer_listener, bool p_simulating) {
	for (int peer : simulating_peers) {
		PeerNetworkedController *controller = scene_sync->get_controller_for_peer(peer);
		if (controller) {
			controller->server_set_peer_simulating_this_controller(p_peer_listener, p_simulating);
		}
	}
}

void NS::SyncGroup::update_listeners_to_simulating_peer(int p_simulating_peer, bool p_simulating) {
	PeerNetworkedController *controller = scene_sync->get_controller_for_peer(p_simulating_peer);
	if (controller) {
		for (int peer : listening_peers) {
			controller->server_set_peer_simulating_this_controller(peer, p_simulating);
		}
	}
}

void NS::SyncGroup::validate_peer_association(int p_peer) {
	if (p_peer <= 0) {
		return;
	}

	// If no other simulated objects controlled by `associated_peer` remove it from
	bool is_simulating = false;
	bool is_networking = false;

	for (auto &soi : simulated_sync_objects) {
		if (soi.od->get_controlled_by_peer() == p_peer) {
			is_networking = true;
			is_simulating = true;
			break;
		}
	}

	if (!is_networking) {
		for (auto &toi : trickled_sync_objects) {
			if (toi.od->get_controlled_by_peer() == p_peer) {
				is_networking = true;
				break;
			}
		}
	}

	if (!is_simulating) {
		// No other objects associated to this peer are simulated, remove from simulating peers.
		VecFunc::remove_unordered(simulating_peers, p_peer);
		update_listeners_to_simulating_peer(p_peer, false);
	}

	if (!is_networking) {
		VecFunc::remove_unordered(networked_peers, p_peer);
		VecFunc::remove_unordered(peers_with_newly_calculated_latency, p_peer);
	}
}

bool NS::SyncGroup::has_simulated(const ObjectData &p_object_data) const {
	return find_simulated(p_object_data) != VecFunc::index_none();
}

bool NS::SyncGroup::has_trickled(const ObjectData &p_object_data) const {
	return find_trickled(p_object_data) != VecFunc::index_none();
}

std::size_t NS::SyncGroup::find_simulated(const ObjectData &p_object_data) const {
	std::size_t i = 0;
	for (auto sso : simulated_sync_objects) {
		if (sso.od == &p_object_data) {
			return i;
		}
		i++;
	}
	return VecFunc::index_none();
}

std::size_t NS::SyncGroup::find_trickled(const ObjectData &p_object_data) const {
	std::size_t i = 0;
	for (auto toi : trickled_sync_objects) {
		if (toi.od == &p_object_data) {
			return i;
		}
		i++;
	}
	return VecFunc::index_none();
}