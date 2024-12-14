#pragma once

#include <string>
#include <cstdint>

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
	SERVER_UPDATE = 1 << 1,

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
	SYNC = SERVER_UPDATE | SYNC_RESET | SYNC_REWIND,
	ALWAYS = CHANGE | SERVER_UPDATE | SYNC_RESET | SYNC_REWIND | END_SYNC
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

enum class RpcRecipientFetch {
	// Send the rpc if the local peer is the authority of the object to the server.
	PLAYER_TO_SERVER,
	// Send the rpc if the local peer is NOT the authority of the object to the server.
	DOLL_TO_SERVER,
	// Send the rpc to the server.
	ALL_TO_SERVER,
	// Send the rpc to the player if local peer is server.
	SERVER_TO_PLAYER,
	// Send the rpc to the dolls if local peer is server.
	SERVER_TO_DOLL,
	// Send the rpc to all if local peer is server.
	SERVER_TO_ALL,
};

enum class RpcAllowedSender {
	PLAYER,
	DOLL,
	SERVER,
	ALL,
};

template <typename T, typename TheIdType>
struct IdMaker {
	using IdType = TheIdType;

	TheIdType id;

	bool operator==(const T &p_o) const {
		return id == p_o.id;
	}

	bool operator!=(const T &p_o) const {
		return !(operator==(p_o));
	}

	bool operator<(const T &p_o) const {
		return id < p_o.id;
	}

	bool operator<=(const T &p_o) const {
		return operator<(p_o) || operator==(p_o);
	}

	bool operator>=(const T &p_o) const {
		return (!operator<(p_o));
	}

	bool operator>(const T &p_o) const {
		return (!operator<(p_o)) && operator!=(p_o);
	}

	T operator+(const T &p_o) const {
		return T{ static_cast<TheIdType>(id + p_o.id) };
	}

	T operator+(TheIdType p_id) const {
		return T{ static_cast<TheIdType>(id + p_id) };
	}

	T &operator+=(const T &p_o) {
		id += p_o.id;
		return *static_cast<T *>(this);
	}

	T &operator+=(TheIdType p_id) {
		id += p_id;
		return *static_cast<T *>(this);
	}

	T operator-(const T &p_o) const {
		return T{ static_cast<TheIdType>(id - p_o.id) };
	}

	T operator-(TheIdType p_id) const {
		return T{ static_cast<TheIdType>(id - p_id) };
	}

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

struct GlobalFrameIndex : public IdMaker<GlobalFrameIndex, std::uint32_t> {
	static const GlobalFrameIndex NONE;
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

struct ScheduledProcedureId : public IdMaker<ScheduledProcedureId, std::uint8_t> {
	static const ScheduledProcedureId NONE;
};

struct ObjectNetId : public IdMaker<ObjectNetId, std::uint16_t> {
	static const ObjectNetId NONE;
};

struct ObjectLocalId : public IdMaker<ObjectLocalId, uint32_t> {
	// TODO use `int` instead?
	static const ObjectLocalId NONE;
};

struct ObjectHandle : public IdMaker<ObjectHandle, std::intptr_t> {
	static const ObjectHandle NONE;
};

enum class ScheduledProcedurePhase : std::uint8_t {
	/// The procedure is called with in this phase only on the server when collecting the arguments.
	COLLECTING_ARGUMENTS = 0,
	/// This is executed on the client when the procedure is received. In some case this is not executed, so don't count on this too much.
	RECEIVED = 1,
	/// The scheduled procedure time is over and the execute is triggered. Here the procedure can do its normal job.
	EXECUTING = 2,
};

template <typename T>
constexpr const T sign(const T m_v) {
	return m_v == 0 ? 0.0f : (m_v < 0 ? -1.0f : +1.0f);
}

#define NS_ScheduledProcedureFunc std::function<void(const class SynchronizerManager &p_synchronizer_manager, NS::ObjectHandle p_app_object_handle, ScheduledProcedurePhase p_phase, NS::DataBuffer& p_buffer)>

NS_NAMESPACE_END