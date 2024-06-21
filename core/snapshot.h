#pragma once

#include "core.h"
#include "object_data.h"
#include <map>

namespace NS {
class SceneSynchronizerBase;

struct SimulatedObjectInfo {
	ObjectNetId net_id;
	int controlled_by_peer;
	
	SimulatedObjectInfo() = default;
	SimulatedObjectInfo(const ObjectNetId& p_id) : net_id(p_id), controlled_by_peer(-1) {}
	SimulatedObjectInfo(const ObjectNetId& p_id, int p_controlled_by_peer) : net_id(p_id), controlled_by_peer(p_controlled_by_peer) {}
	bool operator==(const SimulatedObjectInfo& p_other) const { return net_id == p_other.net_id; }
};

struct Snapshot final {
	FrameIndex input_id = FrameIndex::NONE;
	std::vector<SimulatedObjectInfo> simulated_objects;
	/// The Node variables in a particular frame. The order of this vector
	/// matters because the index is the `ObjectNetId`.
	/// The variable array order also matter.
	std::vector<std::vector<NameAndVar>> object_vars;

	/// The executed FrameIndex for the simulating peers.
	/// NOTE: Due to the nature of the doll simulation, when comparing the
	///       server snapshot with the client snapshot this map is never checked.
	///       This map is used by the Doll-controller's reconciliation algorithm.
	std::map<int, FrameIndex> peers_frames_index;

	bool has_custom_data = false;

	/// Custom variable specified by the user.
	/// NOTE: The user can specify a different variable depending on the passed GroupSync.
	VarData custom_data;

public:
	operator std::string() const;

	const std::vector<NameAndVar> *get_object_vars(ObjectNetId p_id) const;

	/// Copy the given snapshot.
	static Snapshot make_copy(const Snapshot &p_other);
	void copy(const Snapshot &p_other);

	static bool compare(
			const NS::SceneSynchronizerBase &scene_synchronizer,
			const Snapshot &p_snap_A,
			const Snapshot &p_snap_B,
			const int p_skip_objects_not_controlled_by_peer,
			Snapshot *r_no_rewind_recover,
			std::vector<std::string> *r_differences_info
#ifdef NS_DEBUG_ENABLED
			,
			std::vector<ObjectNetId> *r_different_node_data);
#else
	);
#endif
};

}; //namespace NS
