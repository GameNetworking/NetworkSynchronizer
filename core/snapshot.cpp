#include "snapshot.h"

#include "../scene_synchronizer.h"

NS::Snapshot::operator std::string() const {
	std::string s;
	s += "Snapshot input ID: " + input_id;

	for (std::size_t net_node_id = 0; net_node_id < object_vars.size(); net_node_id += 1) {
		s += "\nObject Data: " + std::to_string(net_node_id);
		for (std::size_t i = 0; i < object_vars[net_node_id].size(); i += 1) {
			s += "\n|- Variable: ";
			s += object_vars[net_node_id][i].name;
			s += " = ";
			s += SceneSynchronizerBase::var_data_stringify(object_vars[net_node_id][i].value);
		}
	}
	s += "\nCUSTOM DATA:\n";
	s += " Size:  ?add support to custom data serialization?";
	return s;
}

bool compare_vars(
		const NS::ObjectData &p_object_data,
		const std::vector<NS::NameAndVar> &p_server_vars,
		const std::vector<NS::NameAndVar> &p_client_vars,
		NS::Snapshot *r_no_rewind_recover,
		std::vector<std::string> *r_differences_info) {
	const NS::NameAndVar *s_vars = p_server_vars.data();
	const NS::NameAndVar *c_vars = p_client_vars.data();

#ifdef DEBUG_ENABLED
	bool is_equal = true;
#endif

	for (uint32_t var_index = 0; var_index < uint32_t(p_client_vars.size()); var_index += 1) {
		if (uint32_t(p_server_vars.size()) <= var_index) {
			// This variable isn't defined into the server snapshot, so assuming it's correct.
			continue;
		}

		if (s_vars[var_index].name.empty()) {
			// This variable was not set, skip the check.
			continue;
		}

		// Compare.
		const bool different =
				// Make sure this variable is set.
				c_vars[var_index].name.empty() ||
				// Check if the value is different.
				!NS::SceneSynchronizerBase::var_data_compare(
						s_vars[var_index].value,
						c_vars[var_index].value);

		if (different) {
			if (p_object_data.vars[var_index].skip_rewinding) {
				// The vars are different, but we don't need to trigger a rewind.
				if (r_no_rewind_recover) {
					if (uint32_t(r_no_rewind_recover->object_vars.data()[p_object_data.get_net_id().id].size()) <= var_index) {
						r_no_rewind_recover->object_vars.data()[p_object_data.get_net_id().id].resize(var_index + 1);
					}
					r_no_rewind_recover->object_vars.data()[p_object_data.get_net_id().id].data()[var_index].copy(s_vars[var_index]);
					// Sets `input_id` to 0 to signal that this snapshot contains
					// no-rewind data.
					r_no_rewind_recover->input_id = NS::FrameIndex{ { 0 } };
				}

				if (r_differences_info) {
					r_differences_info->push_back(
							"[NO REWIND] Difference found on var #" + std::to_string(var_index) + " " + p_object_data.vars[var_index].var.name + " " +
							"Server value: `" + NS::SceneSynchronizerBase::var_data_stringify(s_vars[var_index].value) + "` " +
							"Client value: `" + NS::SceneSynchronizerBase::var_data_stringify(c_vars[var_index].value) + "`.    " +
							"[Server name: `" + s_vars[var_index].name + "` " +
							"Client name: `" + c_vars[var_index].name + "`].");
				}
			} else {
				// The vars are different.
				if (r_differences_info) {
					r_differences_info->push_back(
							"Difference found on var #" + std::to_string(var_index) + " " + p_object_data.vars[var_index].var.name + " " +
							"Server value: `" + NS::SceneSynchronizerBase::var_data_stringify(s_vars[var_index].value) + "` " +
							"Client value: `" + NS::SceneSynchronizerBase::var_data_stringify(c_vars[var_index].value) + "`.    " +
							"[Server name: `" + s_vars[var_index].name + "` " +
							"Client name: `" + c_vars[var_index].name + "`].");
				}
#ifdef DEBUG_ENABLED
				is_equal = false;
#else
				return false;
#endif
			}
		}
	}

#ifdef DEBUG_ENABLED
	return is_equal;
#else
	return true;
#endif
}

const std::vector<NS::NameAndVar> *NS::Snapshot::get_object_vars(ObjectNetId p_id) const {
	if (object_vars.size() > p_id.id) {
		return &object_vars[p_id.id];
	}
	return nullptr;
}

NS::Snapshot NS::Snapshot::make_copy(const Snapshot &p_other) {
	Snapshot s;
	s.copy(p_other);
	return s;
}

void NS::Snapshot::copy(const Snapshot &p_other) {
	input_id = p_other.input_id;
	simulated_objects = p_other.simulated_objects;
	peers_frames_index = p_other.peers_frames_index;
	object_vars.resize(p_other.object_vars.size());
	for (std::size_t i = 0; i < p_other.object_vars.size(); i++) {
		object_vars[i].resize(p_other.object_vars[i].size());
		for (std::size_t s = 0; s < p_other.object_vars[i].size(); s++) {
			object_vars[i][s].name = p_other.object_vars[i][s].name;
			object_vars[i][s].value.copy(p_other.object_vars[i][s].value);
		}
	}
	has_custom_data = p_other.has_custom_data;
	custom_data.copy(p_other.custom_data);
}

bool NS::Snapshot::compare(
		const NS::SceneSynchronizerBase &scene_synchronizer,
		const Snapshot &p_snap_A,
		const Snapshot &p_snap_B,
		const int p_skip_objects_not_controlled_by_peer,
		Snapshot *r_no_rewind_recover,
		std::vector<std::string> *r_differences_info
#ifdef DEBUG_ENABLED
		,
		std::vector<ObjectNetId> *r_different_node_data
#endif
) {
#ifdef DEBUG_ENABLED
	bool is_equal = true;
#endif

	// Compares the simualted object first.
	if (p_snap_A.simulated_objects.size() != p_snap_B.simulated_objects.size()) {
		if (r_differences_info) {
			r_differences_info->push_back("Difference detected: simulated_object count is different snapA: " + std::to_string(p_snap_A.simulated_objects.size()) + " snapB: " + std::to_string(p_snap_B.simulated_objects.size()) + ".");
		}
#ifdef DEBUG_ENABLED
		is_equal = false;
#else
		return false;
#endif
	} else {
		for (size_t i = 0; i < p_snap_A.simulated_objects.size(); i++) {
			if (p_snap_A.simulated_objects[i] != p_snap_B.simulated_objects[i]) {
				if (r_differences_info) {
					r_differences_info->push_back("Difference detected: simulated object index `" + std::to_string(i) + "` value is snapA `" + std::to_string(p_snap_A.simulated_objects[i].id) + "` snapB `" + std::to_string(p_snap_B.simulated_objects[i].id) + "`.");
				}
#ifdef DEBUG_ENABLED
				is_equal = false;
#else
				return false;
#endif
			}
		}
	}

	if (p_snap_A.has_custom_data != p_snap_B.has_custom_data) {
		if (r_differences_info) {
			r_differences_info->push_back("Difference detected: custom_data is not set on both snapshots.");
		}
#ifdef DEBUG_ENABLED
		is_equal = false;
#else
		return false;
#endif
	}

	if (p_snap_A.has_custom_data && !SceneSynchronizerBase::var_data_compare(p_snap_A.custom_data, p_snap_B.custom_data)) {
		if (r_differences_info) {
			r_differences_info->push_back("Difference detected: custom_data is different.");
		}
#ifdef DEBUG_ENABLED
		is_equal = false;
#else
		return false;
#endif
	}

	if (r_no_rewind_recover) {
		r_no_rewind_recover->object_vars.resize(std::max(p_snap_A.object_vars.size(), p_snap_B.object_vars.size()));
	}

	// TODO instead to iterate over all the object_vars, iterate over the simulated. This will make it save a bunch of time.
	for (ObjectNetId net_object_id = ObjectNetId{ { 0 } }; net_object_id < ObjectNetId{ { uint32_t(p_snap_A.object_vars.size()) } }; net_object_id += 1) {
		const NS::ObjectData *rew_object_data = scene_synchronizer.get_object_data(net_object_id);
		if (rew_object_data == nullptr || rew_object_data->realtime_sync_enabled_on_client == false) {
			continue;
		}

		if (rew_object_data->get_controlled_by_peer() > 0 && rew_object_data->get_controlled_by_peer() != p_skip_objects_not_controlled_by_peer) {
			// This object is being controlled by a doll, which mostly handles
			// the reconciliation. The doll will be asked if a rewind is needed
			// separately from this.
			// There is nothing more to do for this object at this time.
			continue;
		}

		bool are_nodes_different = false;
		if (net_object_id >= ObjectNetId{ { uint32_t(p_snap_B.object_vars.size()) } }) {
			if (r_differences_info) {
				r_differences_info->push_back("Difference detected: The B snapshot doesn't contain this node: " + rew_object_data->object_name);
			}
#ifdef DEBUG_ENABLED
			is_equal = false;
#else
			return false;
#endif
			are_nodes_different = true;
		} else {
			are_nodes_different = !compare_vars(
					*rew_object_data,
					p_snap_A.object_vars[net_object_id.id],
					p_snap_B.object_vars[net_object_id.id],
					r_no_rewind_recover,
					r_differences_info);

			if (are_nodes_different) {
				if (r_differences_info) {
					r_differences_info->push_back("Difference detected: The node status on snapshot B is different. NODE: " + rew_object_data->object_name);
				}
#ifdef DEBUG_ENABLED
				is_equal = false;
#else
				return false;
#endif
			}
		}

#ifdef DEBUG_ENABLED
		if (are_nodes_different && r_different_node_data) {
			r_different_node_data->push_back(net_object_id);
		}
#endif
	}

#ifdef DEBUG_ENABLED
	return is_equal;
#else
	return true;
#endif
}
