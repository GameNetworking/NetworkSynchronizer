#pragma once

#include "core/object_data.h"
#include "core/var_data.h"
#include "net_utilities.h"

namespace NS {
class SceneSynchronizerBase;

struct Snapshot {
	uint32_t input_id = UINT32_MAX;
	/// The Node variables in a particular frame. The order of this vector
	/// matters because the index is the `NetNodeId`.
	/// The variable array order also matter.
	Vector<Vector<NameAndVar>> node_vars;

	/// Custom variable specified by the user.
	/// NOTE: The user can specify a different variable depending on the passed GroupSync.
	VarData custom_data;

public:
	operator String() const;

	/// Copy the given snapshot.
	void copy(const Snapshot &p_other);

	static bool compare(
			NS::SceneSynchronizerBase &scene_synchronizer,
			const Snapshot &p_snap_A,
			const Snapshot &p_snap_B,
			Snapshot *r_no_rewind_recover,
			LocalVector<String> *r_differences_info
#ifdef DEBUG_ENABLED
			,
			LocalVector<ObjectNetId> *r_different_node_data);
#else
	);
#endif
};

}; //namespace NS
