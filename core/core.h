#pragma once

#include <string>

std::string operator+(const char *p_chr, const std::string &p_str);

#if __cplusplus > 201703L
// C++20 supports likely and unlikely
#define make_likely(cond) (cond) [[likely]]
#define make_unlikely(cond) (cond) [[unlikely]]
#else
#define make_likely(cond) (cond)
#define make_unlikely(cond) (cond)
#endif

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
	PROCESS_PHASE_EARLY = 0,
	PROCESS_PHASE_PRE,
	PROCESS_PHASE_PROCESS,
	PROCESS_PHASE_POST,
	PROCESS_PHASE_LATE,
	PROCESS_PHASE_COUNT
};

const char *get_process_phase_name(ProcessPhase pp);

NS_NAMESPACE_BEGIN

enum PrintMessageType : std::uint8_t {
	VERBOSE = 0,
	INFO = 1,
	WARNING = 2,
	ERROR = 3,
};

std::string get_log_level_txt(NS::PrintMessageType p_level);

template <typename T, typename TheIdType>
struct IdMaker {
	using IdType = TheIdType;

	TheIdType id;

	bool operator==(const T &p_o) const { return id == p_o.id; }
	bool operator!=(const T &p_o) const { return !(operator==(p_o)); }
	bool operator<(const T &p_o) const { return id < p_o.id; }
	bool operator<=(const T &p_o) const { return operator<(p_o) || operator==(p_o); }
	bool operator>=(const T &p_o) const { return (!operator<(p_o)); }
	bool operator>(const T &p_o) const { return (!operator<(p_o)) && operator!=(p_o); }

	T operator+(const T &p_o) const { return T{ static_cast<TheIdType>(id + p_o.id) }; }
	T operator+(TheIdType p_id) const { return T{ static_cast<TheIdType>(id + p_id) }; }
	T &operator+=(const T &p_o) {
		id += p_o.id;
		return *static_cast<T *>(this);
	}
	T &operator+=(TheIdType p_id) {
		id += p_id;
		return *static_cast<T *>(this);
	}

	T operator-(const T &p_o) const { return T{ static_cast<TheIdType>( id - p_o.id ) }; }
	T operator-(TheIdType p_id) const { return T{ static_cast<TheIdType>(id - p_id) }; }
	T &operator-=(const T &p_o) {
		id -= p_o.id;
		return *static_cast<T *>(this);
	}
	T &operator-=(TheIdType p_id) {
		id -= p_id;
		return *static_cast<T *>(this);
	}
	
	operator std::string() const {
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
struct VarId : public IdMaker<VarId, std::uint8_t> {
	static const VarId NONE;
};
struct ObjectNetId : public IdMaker<ObjectNetId, std::uint16_t> {
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
