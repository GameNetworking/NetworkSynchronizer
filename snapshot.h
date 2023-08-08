#pragma once

#include "net_utilities.h"

class SceneSynchronizer;

namespace NetUtility {

struct Snapshot {
	uint32_t input_id = UINT32_MAX;
	/// The Node variables in a particular frame. The order of this vector
	/// matters because the index is the `NetNodeId`.
	/// The variable array order also matter.
	Vector<Vector<Var>> node_vars;
	/// Game specific data.
	Vector<Variant> custom_data;

	operator String() const;

	static bool compare(
			SceneSynchronizer &scene_synchronizer,
			const Snapshot &p_snap_A,
			const Snapshot &p_snap_B,
			Snapshot *r_no_rewind_recover,
			LocalVector<String> *r_differences_info
#ifdef DEBUG_ENABLED
			,
			LocalVector<NetNodeId> *r_different_node_data);
#else
	);
#endif
};

}; //namespace NetUtility
