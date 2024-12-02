#include "snapshot.h"

#include "../scene_synchronizer.h"

NS::Snapshot::operator std::string() const {
	std::string s;
	s += "Snapshot input ID: " + input_id;

	for (std::size_t net_node_id = 0; net_node_id < objects.size(); net_node_id += 1) {
		s += "\nObject Data: " + std::to_string(net_node_id);
		for (std::size_t i = 0; i < objects[net_node_id].vars.size(); i += 1) {
			s += "\n|- Variable index: ";
			s += std::to_string(i);
			s += " = ";
			s += objects[net_node_id].vars[i].has_value() ? SceneSynchronizerBase::var_data_stringify(objects[net_node_id].vars[i].value()) : "NO-VALUE";
		}
	}
	s += "\nCUSTOM DATA:\n";
	s += " Size:  ?add support to custom data serialization?";
	return s;
}

bool compare_vars(
		const NS::ObjectData &p_object_data,
		const std::vector<std::optional<NS::VarData>> &p_server_vars,
		const std::vector<std::optional<NS::VarData>> &p_client_vars,
		NS::Snapshot *r_no_rewind_recover,
		std::vector<std::string> *r_differences_info) {
	const std::optional<NS::VarData> *s_vars = p_server_vars.data();
	const std::optional<NS::VarData> *c_vars = p_client_vars.data();

#ifdef NS_DEBUG_ENABLED
	bool is_equal = true;
#endif

	for (uint32_t var_index = 0; var_index < uint32_t(p_client_vars.size()); var_index += 1) {
		if (uint32_t(p_server_vars.size()) <= var_index) {
			// This variable isn't defined into the server snapshot, so assuming it's correct.
			continue;
		}

		if (!s_vars[var_index].has_value()) {
			// This variable was not set, skip the check.
			continue;
		}

		// Compare.
		const bool different =
				// Make sure this variable is set.
				!c_vars[var_index].has_value() ||
				// Check if the value is different.
				!NS::SceneSynchronizerBase::var_data_compare(
						s_vars[var_index].value(),
						c_vars[var_index].value());

		if (different) {
			if (p_object_data.vars[var_index].skip_rewinding) {
				// The vars are different, but we don't need to trigger a rewind.
				if (r_no_rewind_recover) {
					if (uint32_t(r_no_rewind_recover->objects[p_object_data.get_net_id().id].vars.size()) <= var_index) {
						r_no_rewind_recover->objects[p_object_data.get_net_id().id].vars.resize(var_index + 1);
					}
					r_no_rewind_recover->objects[p_object_data.get_net_id().id].vars[var_index].emplace(NS::VarData::make_copy(s_vars[var_index].value()));
					// Sets `input_id` to 0 to signal that this snapshot contains
					// no-rewind data.
					r_no_rewind_recover->input_id = NS::FrameIndex{ { 0 } };

					// Also insert this object into the simulated objects to ensure it gets updated.
					NS::VecFunc::insert_unique(r_no_rewind_recover->simulated_objects, p_object_data.get_net_id());
				}

				if (r_differences_info) {
					r_differences_info->push_back(
							"[NO REWIND] Difference found on var #" + std::to_string(var_index) + " name `" + p_object_data.vars[var_index].var.name + "`. " +
							"Server value: `" + NS::SceneSynchronizerBase::var_data_stringify(s_vars[var_index].value()) + "` " +
							"Client value: `" + NS::SceneSynchronizerBase::var_data_stringify(c_vars[var_index].value()) + "`.");
				}
			} else {
				// The vars are different.
				if (r_differences_info) {
					r_differences_info->push_back(
							"Difference found on var #" + std::to_string(var_index) + " name `" + p_object_data.vars[var_index].var.name + "` " +
							"Server value: `" + NS::SceneSynchronizerBase::var_data_stringify(s_vars[var_index].value()) + "` " +
							"Client value: `" + NS::SceneSynchronizerBase::var_data_stringify(c_vars[var_index].value()) + "`.");
				}
#ifdef NS_DEBUG_ENABLED
				is_equal = false;
#else
				return false;
#endif
			}
		}
	}

#ifdef NS_DEBUG_ENABLED
	return is_equal;
#else
	return true;
#endif
}

bool compare_procedures(
		const NS::ObjectData &p_object_data,
		const std::vector<NS::ScheduledProcedureSnapshot> &p_server_procedures,
		const std::vector<NS::ScheduledProcedureSnapshot> &p_client_procedures,
		NS::Snapshot *r_no_rewind_recover,
		std::vector<std::string> *r_differences_info) {
	// NOTICE: Since the scheduled procedures are an information that is necessary
	// to execute an operation in the future and is not important to validate 
	// whether the client predicted correctly the server, we can avoid checking
	// them.
	// Instead, what it does here is putting the server procedures into the
	// no_rewind_snapshot, when they are provided, to ensure the client
	// is updated with the missing procedures.
	// Once again, all this happens without triggering any rewinding.
	if (r_no_rewind_recover) {
		bool is_equal = true;

		const NS::ScheduledProcedureSnapshot *s_procedures = p_server_procedures.data();
		const NS::ScheduledProcedureSnapshot *c_procedures = p_client_procedures.data();

		for (uint32_t proc_index = 0; proc_index < uint32_t(p_client_procedures.size()); proc_index += 1) {
			if (uint32_t(p_server_procedures.size()) <= proc_index) {
				// This variable isn't defined into the server snapshot, so assuming it's correct.
				continue;
			}

			// Compare.
			const bool different =
					// Check if the value is different.
					s_procedures[proc_index] != c_procedures[proc_index];

			if (different) {
				// The vars are different.
				is_equal = false;
#ifndef NS_DEBUG_ENABLED
				break;
#endif
				if (r_differences_info) {
					r_differences_info->push_back(
							"Difference found on procedure #" + std::to_string(proc_index) +
							" Server value: `" + std::string(s_procedures[proc_index]) + "` " +
							" Client value: `" + std::string(c_procedures[proc_index]) + "`.");
				} else {
					break;
				}
			}
		}

		if (!is_equal) {
			if (uint32_t(r_no_rewind_recover->objects[p_object_data.get_net_id().id].procedures.size()) <= p_server_procedures.size()) {
				r_no_rewind_recover->objects[p_object_data.get_net_id().id].procedures.resize(p_server_procedures.size());
			}

			for (uint32_t proc_index = 0; proc_index < uint32_t(p_server_procedures.size()); proc_index += 1) {
				r_no_rewind_recover->objects[p_object_data.get_net_id().id].procedures[proc_index] = p_server_procedures[proc_index];

				// Sets `input_id` to 0 to signal that this snapshot contains
				// no-rewind data.
				r_no_rewind_recover->input_id = NS::FrameIndex{ { 0 } };
				// Also insert this object into the simulated objects to ensure it gets updated.
				NS::VecFunc::insert_unique(r_no_rewind_recover->simulated_objects, p_object_data.get_net_id());
			}
		}
	}

	return true;
}

const std::vector<std::optional<NS::VarData>> *NS::Snapshot::get_object_vars(ObjectNetId p_id) const {
	if (objects.size() > p_id.id) {
		return &objects[p_id.id].vars;
	}
	return nullptr;
}

const std::vector<NS::ScheduledProcedureSnapshot> *NS::Snapshot::get_object_procedures(ObjectNetId p_id) const {
	if (objects.size() > p_id.id) {
		return &objects[p_id.id].procedures;
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
	global_frame_index = p_other.global_frame_index;
	simulated_objects = p_other.simulated_objects;
	peers_frames_index = p_other.peers_frames_index;
	objects.resize(p_other.objects.size());
	for (std::size_t i = 0; i < p_other.objects.size(); i++) {
		objects[i].vars.resize(p_other.objects[i].vars.size());
		for (std::size_t s = 0; s < p_other.objects[i].vars.size(); s++) {
			if (p_other.objects[i].vars[s].has_value()) {
				objects[i].vars[s].emplace(VarData::make_copy(p_other.objects[i].vars[s].value()));
			} else {
				objects[i].vars[s].reset();
			}
		}
		objects[i].procedures = p_other.objects[i].procedures;
	}
	has_custom_data = p_other.has_custom_data;
	custom_data.copy(p_other.custom_data);
}

bool NS::Snapshot::compare(
		const SceneSynchronizerBase &scene_synchronizer,
		const Snapshot &p_snap_A,
		const Snapshot &p_snap_B,
		const int p_skip_objects_not_controlled_by_peer,
		Snapshot *r_no_rewind_recover,
		std::vector<std::string> *r_differences_info
#ifdef NS_DEBUG_ENABLED
		,
		std::vector<ObjectNetId> *r_different_node_data
#endif
		) {
#ifdef NS_DEBUG_ENABLED
	bool is_equal = true;
#endif

	if (p_snap_A.global_frame_index != p_snap_B.global_frame_index) {
		if (r_differences_info) {
			r_differences_info->push_back("Difference detected: global frame index in snapshot A `" + std::to_string(p_snap_A.global_frame_index.id) + "` is different in snap B `" + std::to_string(p_snap_B.global_frame_index.id) + "`.");
		}
#ifdef NS_DEBUG_ENABLED
		is_equal = false;
#else
		return false;
#endif
	}

	// Compares the simualated object first.
	if (p_snap_A.simulated_objects.size() != p_snap_B.simulated_objects.size()) {
		if (r_differences_info) {
			r_differences_info->push_back("Difference detected: simulated_object count is different snapA: " + std::to_string(p_snap_A.simulated_objects.size()) + " snapB: " + std::to_string(p_snap_B.simulated_objects.size()) + ".");
		}
#ifdef NS_DEBUG_ENABLED
		is_equal = false;
#else
		return false;
#endif
	} else {
		for (size_t i = 0; i < p_snap_A.simulated_objects.size(); i++) {
			if (p_snap_A.simulated_objects[i].net_id != p_snap_B.simulated_objects[i].net_id || p_snap_A.simulated_objects[i].controlled_by_peer != p_snap_B.simulated_objects[i].controlled_by_peer) {
				if (r_differences_info) {
					r_differences_info->push_back("Difference detected: simulated object index `" + std::to_string(i) + "` value is snapA `" + std::to_string(p_snap_A.simulated_objects[i].net_id.id) + "` snapB `" + std::to_string(p_snap_B.simulated_objects[i].net_id.id) + "`.");
				}
#ifdef NS_DEBUG_ENABLED
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
#ifdef NS_DEBUG_ENABLED
		is_equal = false;
#else
		return false;
#endif
	}

	if (p_snap_A.has_custom_data && !SceneSynchronizerBase::var_data_compare(p_snap_A.custom_data, p_snap_B.custom_data)) {
		if (r_differences_info) {
			r_differences_info->push_back("Difference detected: custom_data is different.");
		}
#ifdef NS_DEBUG_ENABLED
		is_equal = false;
#else
		return false;
#endif
	}

	if (r_no_rewind_recover) {
		r_no_rewind_recover->objects.resize(std::max(p_snap_A.objects.size(), p_snap_B.objects.size()));
	}

	// TODO instead to iterate over all the object_vars, iterate over the simulated. This will make it save a bunch of time.
	for (ObjectNetId net_object_id = ObjectNetId{ { 0 } }; net_object_id < ObjectNetId{ { ObjectNetId::IdType(p_snap_A.objects.size()) } }; net_object_id += 1) {
		const ObjectData *rew_object_data = scene_synchronizer.get_object_data(net_object_id);
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
		if (net_object_id >= ObjectNetId{ { ObjectNetId::IdType(p_snap_B.objects.size()) } }) {
			if (r_differences_info) {
				r_differences_info->push_back("Difference detected because the snapshot B doesn't contain this object: " + rew_object_data->get_object_name());
			}
#ifdef NS_DEBUG_ENABLED
			is_equal = false;
#else
			return false;
#endif
			are_nodes_different = true;
		} else {
			are_nodes_different = !compare_vars(
					*rew_object_data,
					p_snap_A.objects[net_object_id.id].vars,
					p_snap_B.objects[net_object_id.id].vars,
					r_no_rewind_recover,
					r_differences_info);

			if (are_nodes_different) {
				if (r_differences_info) {
					r_differences_info->push_back("Difference detected on snapshot B. OBJECT NAME: " + rew_object_data->get_object_name());
				}
#ifdef NS_DEBUG_ENABLED
				is_equal = false;
#else
				return false;
#endif
			}

			if (!are_nodes_different) {
				are_nodes_different = !compare_procedures(
						*rew_object_data,
						p_snap_A.objects[net_object_id.id].procedures,
						p_snap_B.objects[net_object_id.id].procedures,
						r_no_rewind_recover,
						r_differences_info);
				if (are_nodes_different) {
					if (r_differences_info) {
						r_differences_info->push_back("Difference detected on snapshot B. OBJECT NAME: " + rew_object_data->get_object_name());
					}
#ifdef NS_DEBUG_ENABLED
					is_equal = false;
#else
				return false;
#endif
				}
			}
		}

#ifdef NS_DEBUG_ENABLED
		if (are_nodes_different && r_different_node_data) {
			r_different_node_data->push_back(net_object_id);
		}
#endif
	}

#ifdef NS_DEBUG_ENABLED
	return is_equal;
#else
	return true;
#endif
}