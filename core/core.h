#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

std::string operator+(const char *p_chr, const std::string &p_str);

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

const char *get_process_phase_name(ProcessPhase pp);

NS_NAMESPACE_BEGIN

enum class PrintMessageType {
	INFO,
	WARNING,
	ERROR
};

template <typename T, typename TheIdType>
struct IdMaker {
	typedef TheIdType IdType;

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

	T operator-(const T &p_o) const { return { id - p_o.id }; }
	T operator-(int p_id) const { return { id - p_id }; }
	T operator-(uint32_t p_id) const { return { id - p_id }; }
	T &operator-=(const T &p_o) {
		id -= p_o.id;
		return *static_cast<T *>(this);
	}
	T &operator-=(uint32_t p_id) {
		id -= p_id;
		return *static_cast<T *>(this);
	}
	T &operator-=(int p_id) {
		id -= p_id;
		return *static_cast<T *>(this);
	}

	operator std::string() {
		return "`" + std::to_string(id) + "`";
	}
};

struct FrameIndex : public IdMaker<FrameIndex, std::uint32_t> {
	static const FrameIndex NONE;
};
struct SyncGroupId : public IdMaker<SyncGroupId, std::uint32_t> {
	static const SyncGroupId NONE;
	/// This SyncGroup contains ALL the registered ObjectData.
	static const SyncGroupId GLOBAL;
};
struct VarId : public IdMaker<VarId, uint32_t> { // TODO use `uint8_t` instead?
	static const VarId NONE;
};
struct ObjectNetId : public IdMaker<ObjectNetId, uint32_t> { // TODO use `uint16_t` instead.
	static const ObjectNetId NONE;
};
struct ObjectLocalId : public IdMaker<ObjectLocalId, uint32_t> { // TODO use `int` instead?
	static const ObjectLocalId NONE;
};
struct ObjectHandle : public IdMaker<ObjectHandle, std::intptr_t> {
	static const ObjectHandle NONE;
};

template <typename T>
constexpr const T sign(const T m_v) {
	return m_v == 0 ? 0.0f : (m_v < 0 ? -1.0f : +1.0f);
}

NS_NAMESPACE_END
