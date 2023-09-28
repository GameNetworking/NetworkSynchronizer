#pragma once

#include <cstdint>

#define NS_NAMESPACE_BEGIN \
	namespace NS {

// End the JPH namespace
#define NS_NAMESPACE_END \
	}

/// Flags used to control when an event is executed.
enum NetEventFlag {

	// ~~ Flags ~~ //
	EMPTY = 0,

	/// Called at the end of the frame, if the value is different.
	/// It's also called when a variable is modified by the
	/// `apply_scene_changes` function.
	CHANGE = 1 << 0,

	/// Called when the variable is modified by the `NetworkSynchronizer`
	/// because not in sync with the server.
	SYNC_RECOVER = 1 << 1,

	/// Called when the variable is modified by the `NetworkSynchronizer`
	/// because it's preparing the node for the rewinding.
	SYNC_RESET = 1 << 2,

	/// Called when the variable is modified during the rewinding phase.
	SYNC_REWIND = 1 << 3,

	/// Called at the end of the recovering phase, if the value was modified
	/// during the rewinding.
	END_SYNC = 1 << 4,

	// ~~ Preconfigured ~~ //

	DEFAULT = CHANGE | END_SYNC,
	SYNC = SYNC_RECOVER | SYNC_RESET | SYNC_REWIND,
	ALWAYS = CHANGE | SYNC_RECOVER | SYNC_RESET | SYNC_REWIND | END_SYNC
};

enum ProcessPhase {
	PROCESSPHASE_EARLY = 0,
	PROCESSPHASE_PRE,
	PROCESSPHASE_PROCESS,
	PROCESSPHASE_POST,
	PROCESSPHASE_LATE,
	PROCESSPHASE_COUNT
};

static const char *ProcessPhaseName[PROCESSPHASE_COUNT] = {
	"EARLY PROCESS",
	"PRE PROCESS",
	"PROCESS",
	"POST PROCESS",
	"LATE PROCESS"
};

typedef uint32_t NetVarId;
typedef uint32_t ObjectNetId;

NS_NAMESPACE_BEGIN

struct ObjectLocalId {
	uint32_t id;
	bool operator==(const ObjectLocalId &p_o) const { return id == p_o.id; }

	static const ObjectLocalId ID_NONE;
};

struct ObjectHandle {
	std::intptr_t intptr;
	bool operator==(const ObjectHandle &p_o) const { return intptr == p_o.intptr; }

	static const ObjectHandle NONE;
};

NS_NAMESPACE_END