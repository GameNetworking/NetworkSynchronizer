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

NS_NAMESPACE_BEGIN

template <typename T, typename IdType, IdType NoneVal>
struct IdMaker {
	IdType id;
	bool operator==(const T &p_o) const { return id == p_o.id; }
	bool operator!=(const T &p_o) const { return !(operator==(p_o)); }
	bool operator<(const T &p_o) const { return id < p_o.id; }
	bool operator<=(const T &p_o) const { return operator<(p_o) || operator==(p_o); }
	bool operator>=(const T &p_o) const { return (!operator<(p_o)); }
	bool operator>(const T &p_o) const { return (!operator<(p_o)) && operator!=(p_o); }
	T operator+(const T &p_o) const { return { id + p_o.id }; }
	T operator+(int p_id) const { return { id + p_id }; }
	T operator+(uint32_t p_id) const { return { id + p_id }; }
	T &operator+=(const T &p_o) {
		id += p_o.id;
		return *static_cast<T *>(this);
	}
	T &operator+=(uint32_t p_id) {
		id += p_id;
		return *static_cast<T *>(this);
	}
	T &operator+=(int p_id) {
		id += p_id;
		return *static_cast<T *>(this);
	}

	inline static const T NONE = { NoneVal };
};

struct VarId : public IdMaker<VarId, uint32_t, UINT32_MAX> {};
struct ObjectNetId : public IdMaker<ObjectNetId, uint32_t, UINT32_MAX> {};
struct ObjectLocalId : public IdMaker<ObjectLocalId, uint32_t, UINT32_MAX> {};
struct ObjectHandle : public IdMaker<ObjectHandle, std::intptr_t, 0> {};

NS_NAMESPACE_END