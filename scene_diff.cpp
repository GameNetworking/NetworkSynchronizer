#include "scene_diff.h"

#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/net_utilities.h"
#include "scene/main/node.h"
#include "scene_synchronizer.h"

void SceneDiff::start_tracking_scene_changes(
		const NS::SceneSynchronizerBase *p_synchronizer,
		const LocalVector<NS::ObjectData *> &p_nodes) {
	start_tracking_count += 1;
	if (start_tracking_count > 1) {
		// Nothing to do, the tracking is already started.
		return;
	}

	tracking.resize(p_nodes.size());

	for (uint32_t i = 0; i < p_nodes.size(); i += 1) {
		if (p_nodes[i] == nullptr) {
			tracking[i].clear();
			continue;
		}

#ifdef DEBUG_ENABLED
		// This is never triggered because we always pass the `organized_node_data`
		// array.
		CRASH_COND(p_nodes[i]->get_net_id().id != i);
		// This is never triggered because when the node is invalid the node data
		// is destroyed.
		CRASH_COND(p_nodes[i]->app_object_handle == NS::ObjectHandle::NONE);
#endif

		tracking[i].resize(p_nodes[i]->vars.size());

		for (uint32_t v = 0; v < p_nodes[i]->vars.size(); v += 1) {
			// Take the current variable value and store it.
			if (p_nodes[i]->vars[v].enabled && p_nodes[i]->vars[v].id != NS::VarId::NONE) {
				// Note: Taking the value using `get` so to take the most updated
				// value.
				p_synchronizer->get_synchronizer_manager().get_variable(
						p_nodes[i]->app_object_handle,
						String(p_nodes[i]->vars[v].var.name.c_str()).utf8(),
						tracking[i][v]);
			} else {
				tracking[i][v] = Variant();
			}
		}
	}
}

void SceneDiff::stop_tracking_scene_changes(const NS::SceneSynchronizerBase *p_synchronizer) {
	ERR_FAIL_COND_MSG(
			start_tracking_count == 0,
			"The tracking is not yet started on this SceneDiff, so can't be end.");

	start_tracking_count -= 1;
	if (start_tracking_count > 0) {
		// Nothing to do, the tracking is still ongoing.
		return;
	}

	if (p_synchronizer->get_biggest_node_id() == NS::ObjectNetId::NONE) {
		// No nodes to track.
		tracking.clear();
		return;
	}

	if (tracking.size() > (p_synchronizer->get_biggest_node_id().id + 1)) {
		NET_DEBUG_ERR("[BUG] The tracked nodes are exceeding the sync nodes. Probably the sync is different or it has reset?");
		tracking.clear();
		return;
	}

	if (diff.size() < tracking.size()) {
		// Make sure the diff has room to store the needed info.
		diff.resize(tracking.size());
	}

	for (NS::ObjectNetId i = { 0 }; i < NS::ObjectNetId{ tracking.size() }; i += 1) {
		const NS::ObjectData *nd = p_synchronizer->get_object_data({ i });
		if (nd == nullptr) {
			continue;
		}

#ifdef DEBUG_ENABLED
		// This is never triggered because we always pass the `organized_node_data`
		// array.
		CRASH_COND(nd->get_net_id() != i);
		// This is never triggered because when the object is invalid the node data
		// is destroyed.
		CRASH_COND(nd->app_object_handle == NS::ObjectHandle::NONE);
#endif

		if (nd->vars.size() != tracking[i.id].size()) {
			// These two arrays are different because the node was null
			// during the start. So we can assume we are not tracking it.
			continue;
		}

		if (diff[i.id].size() < tracking[i.id].size()) {
			// Make sure the diff has room to store the variable info.
			diff[i.id].resize(tracking[i.id].size());
		}

		for (uint32_t v = 0; v < tracking[i.id].size(); v += 1) {
			if (nd->vars[v].id == NS::VarId::NONE || nd->vars[v].enabled == false) {
				continue;
			}

			// Take the current variable value.
			Variant current_value;
			p_synchronizer->get_synchronizer_manager().get_variable(
					nd->app_object_handle,
					String(nd->vars[v].var.name.c_str()).utf8(),
					current_value);

			// Compare the current value with the one taken during the start.
			if (p_synchronizer->compare(
						tracking[i.id][v],
						current_value) == false) {
				diff[i.id][v].is_different = true;
				diff[i.id][v].value = current_value;
			}
		}
	}

	tracking.clear();
}

bool SceneDiff::is_tracking_in_progress() const {
	return start_tracking_count > 0;
}
