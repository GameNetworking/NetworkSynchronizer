#pragma once

#include "core/object/class_db.h"
#include "net_utilities.h"

namespace NS {
class SceneSynchronizerBase;
}; //namespace NS

struct VarDiff {
	bool is_different = false;
	Variant value;
};

/// This class is used to track the scene changes during a particular period of
/// the frame. You can use it to generate partial FrameSnapshot that contains
/// only portion of a change.
class SceneDiff : public Object {
	friend NS::SceneSynchronizerBase;

	uint32_t start_tracking_count = 0;
	LocalVector<LocalVector<Variant>> tracking;
	LocalVector<LocalVector<VarDiff>> diff;

public:
	SceneDiff() = default;

	void start_tracking_scene_changes(const NS::SceneSynchronizerBase *p_synchronizer, const LocalVector<NS::ObjectData *> &p_nodes);
	void stop_tracking_scene_changes(const NS::SceneSynchronizerBase *p_synchronizer);

	bool is_tracking_in_progress() const;
};
