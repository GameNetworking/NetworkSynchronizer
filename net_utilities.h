/*************************************************************************/
/*  net_utilities.h                                                      */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2021 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2021 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

/**
	@author AndreaCatania
*/

#ifndef NET_UTILITIES_H
#define NET_UTILITIES_H

#include "core/config/project_settings.h"
#include "core/math/math_funcs.h"
#include "core/templates/local_vector.h"
#include "core/variant/variant.h"

class Node;

#ifdef DEBUG_ENABLED
#define NET_DEBUG_PRINT(msg)                                                                                  \
	if (ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/log_debug_warnings_and_messages")) \
	print_line(String("[Net] ") + msg)
#define NET_DEBUG_WARN(msg)                                                                                   \
	if (ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/log_debug_warnings_and_messages")) \
	WARN_PRINT(String("[Net] ") + msg)
#define NET_DEBUG_ERR(msg) \
	ERR_PRINT(String("[Net] ") + msg)
#else
#define NET_DEBUG_PRINT(msg)
#define NET_DEBUG_WARN(msg)
#define NET_DEBUG_ERR(msg)
#endif

extern const uint32_t NetID_NONE;
typedef uint32_t NetNodeId;
typedef uint32_t NetVarId;
typedef uint32_t SyncGroupId;

#ifdef TRACY_ENABLE

#include "godot_tracy/profiler.h"

#define PROFILE \
	ZoneScoped;

#define PROFILE_NODE                                        \
	ZoneScoped;                                             \
	CharString c = String(get_path()).utf8();               \
	if (c.size() >= std::numeric_limits<uint16_t>::max()) { \
		c.resize(std::numeric_limits<uint16_t>::max() - 1); \
	}                                                       \
	ZoneText(c.ptr(), c.size());

#else

#define PROFILE
#define PROFILE_NODE

#endif

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

static const String ProcessPhaseName[PROCESSPHASE_COUNT] = {
	String("EARLY PROCESS"),
	String("PRE PROCESS"),
	String("PROCESS"),
	String("POST PROCESS"),
	String("LATE PROCESS")
};

namespace NetUtility {
template <class T>
class StatisticalRingBuffer {
	LocalVector<T> data;
	uint32_t index = 0;

	T avg_sum = 0;

public:
	StatisticalRingBuffer(uint32_t p_size, T p_default);
	void resize(uint32_t p_size, T p_default);
	void reset(T p_default);

	void push(T p_value);

	/// Maximum value.
	T max() const;

	/// Minumum value.
	T min(uint32_t p_consider_last = UINT32_MAX) const;

	/// Median value.
	T average() const;
	T average_rounded() const;

	T get_deviation(T p_mean) const;

private:
	// Used to avoid accumulate precision loss.
	void force_recompute_avg_sum();
};

template <class T>
StatisticalRingBuffer<T>::StatisticalRingBuffer(uint32_t p_size, T p_default) {
	resize(p_size, p_default);
}

template <class T>
void StatisticalRingBuffer<T>::resize(uint32_t p_size, T p_default) {
	data.resize(p_size);

	reset(p_default);
}

template <class T>
void StatisticalRingBuffer<T>::reset(T p_default) {
	for (uint32_t i = 0; i < data.size(); i += 1) {
		data[i] = p_default;
	}

	index = 0;
	force_recompute_avg_sum();
}

template <class T>
void StatisticalRingBuffer<T>::push(T p_value) {
	avg_sum -= data[index];
	avg_sum += p_value;
	data[index] = p_value;

	index = (index + 1) % data.size();
	if (index == 0) {
		// Each cycle recompute the sum.
		force_recompute_avg_sum();
	}
}

template <class T>
T StatisticalRingBuffer<T>::max() const {
	CRASH_COND(data.size() == 0);

	T a = data[0];
	for (uint32_t i = 1; i < data.size(); i += 1) {
		a = MAX(a, data[i]);
	}
	return a;
}

template <class T>
T StatisticalRingBuffer<T>::min(uint32_t p_consider_last) const {
	CRASH_COND(data.size() == 0);
	p_consider_last = MIN(p_consider_last, data.size());

	const uint32_t youngest = (index == 0 ? data.size() : index) - 1;
	const uint32_t oldest = (index + (data.size() - p_consider_last)) % data.size();

	T a = data[oldest];

	uint32_t i = oldest;
	do {
		i = (i + 1) % data.size();
		a = MIN(a, data[i]);
	} while (i != youngest);

	return a;
}

template <class T>
T StatisticalRingBuffer<T>::average() const {
	CRASH_COND(data.size() == 0);

#ifdef DEBUG_ENABLED
	T a = data[0];
	for (uint32_t i = 1; i < data.size(); i += 1) {
		a += data[i];
	}
	a = a / T(data.size());
	T b = avg_sum / T(data.size());
	const T difference = a > b ? a - b : b - a;
	ERR_FAIL_COND_V_MSG(difference > (CMP_EPSILON * 4.0), b, "The `avg_sum` accumulated a sensible precision loss: " + rtos(difference));
	return b;
#else
	// Divide it by the buffer size is wrong when the buffer is not yet fully
	// initialized. However, this is wrong just for the first run.
	// I'm leaving it as is because solve it mean do more operations. All this
	// just to get the right value for the first few frames.
	return avg_sum / T(data.size());
#endif
}

template <class T>
T StatisticalRingBuffer<T>::average_rounded() const {
	CRASH_COND(data.size() == 0);

#ifdef DEBUG_ENABLED
	T a = data[0];
	for (uint32_t i = 1; i < data.size(); i += 1) {
		a += data[i];
	}
	a = Math::round(double(a) / double(data.size()));
	T b = Math::round(double(avg_sum) / double(data.size()));
	const T difference = a > b ? a - b : b - a;
	ERR_FAIL_COND_V_MSG(difference > (CMP_EPSILON * 4.0), b, "The `avg_sum` accumulated a sensible precision loss: " + rtos(difference));
	return b;
#else
	// Divide it by the buffer size is wrong when the buffer is not yet fully
	// initialized. However, this is wrong just for the first run.
	// I'm leaving it as is because solve it mean do more operations. All this
	// just to get the right value for the first few frames.
	return Math::round(double(avg_sum) / double(data.size()));
#endif
}

template <class T>
T StatisticalRingBuffer<T>::get_deviation(T p_mean) const {
	if (data.size() <= 0) {
		return T();
	}

	double r = 0;
	for (uint32_t i = 0; i < data.size(); i += 1) {
		r += Math::pow(double(data[i]) - double(p_mean), 2.0);
	}

	return Math::sqrt(r / double(data.size()));
}

template <class T>
void StatisticalRingBuffer<T>::force_recompute_avg_sum() {
#ifdef DEBUG_ENABLED
	// This class is not supposed to be used with 0 size.
	CRASH_COND(data.size() <= 0);
#endif
	avg_sum = data[0];
	for (uint32_t i = 1; i < data.size(); i += 1) {
		avg_sum += data[i];
	}
}

/// Specific node listener. Alone this doesn't do much, but allows the
/// `ChangeListener` to know and keep track of the node events.
struct NodeChangeListener {
	struct NodeData *node_data = nullptr;
	NetVarId var_id = UINT32_MAX;

	bool old_set = false;
	Variant old_value;

	bool operator==(const NodeChangeListener &p_other) const;
};

/// Change listener that rapresents a pair of Object and Method.
/// This can track the changes of many nodes and variables. It's dispatched
/// if one or more tracked variable change during the tracked phase, specified
/// by the flag.
struct ChangeListener {
	// TODO use a callable instead??
	ObjectID object_id = ObjectID();
	StringName method;
	uint32_t method_argument_count;
	NetEventFlag flag;

	LocalVector<NodeChangeListener> watching_vars;
	LocalVector<Variant> old_values;
	bool emitted = true;

	bool operator==(const ChangeListener &p_other) const;
};

struct Var {
	StringName name;
	Variant value;
};

struct VarData {
	NetVarId id = UINT32_MAX;
	Var var;
	bool skip_rewinding = false;
	bool enabled = false;
	Vector<uint32_t> change_listeners;

	VarData() = default;
	VarData(const StringName &p_name);
	VarData(NetVarId p_id, const StringName &p_name, const Variant &p_val, bool p_skip_rewinding, bool p_enabled);

	bool operator==(const VarData &p_other) const;
	bool operator<(const VarData &p_other) const;
};

struct NodeData {
	// ID used to reference this Node in the networked calls.
	uint32_t id = 0;
	ObjectID instance_id = ObjectID();

	bool realtime_sync_enabled_on_client = false;

	bool is_controller = false;

	/// The sync variables of this node. The order of this vector matters
	/// because the index is the `NetVarId`.
	LocalVector<VarData> vars;
	LocalVector<Callable> functions[PROCESSPHASE_COUNT];

	// func _collect_epoch_data(buffer: DataBuffer):
	Callable collect_epoch_func;

	// func _apply_epoch(delta: float, interpolation_alpha: float, past_buffer: DataBuffer, future_buffer: DataBuffer):
	Callable apply_epoch_func;

	// This is valid to use only inside the process function.
	Node *node = nullptr;

	NodeData() = default;

	bool has_registered_process_functions() const;
	bool can_deferred_sync() const;
};

struct PeerData {
	NetNodeId controller_id = UINT32_MAX;
	// For new peers notify the state as soon as possible.
	bool force_notify_snapshot = true;
	// For new peers a full snapshot is needed.
	bool need_full_snapshot = true;
	// Used to know if the peer is enabled.
	bool enabled = true;
};

struct Snapshot {
	uint32_t input_id;
	/// The Node variables in a particular frame. The order of this vector
	/// matters because the index is the `NetNodeId`.
	/// The variable array order also matter.
	Vector<Vector<Var>> node_vars;

	operator String() const;
};

struct NoRewindRecover {
	NodeData *node_data = nullptr;
	Vector<Var> vars;
};

struct SyncGroup {
public:
	struct Change {
		bool not_known_before = false;
		RBSet<StringName> uknown_vars;
		RBSet<StringName> vars;
	};

	struct RealtimeNodeInfo {
		NetUtility::NodeData *nd = nullptr;
		Change change;

		RealtimeNodeInfo() = default;
		RealtimeNodeInfo(NetUtility::NodeData *p_nd) :
				nd(p_nd) {}
		bool operator==(const RealtimeNodeInfo &p_other) { return nd == p_other.nd; }
	};

	struct DeferredNodeInfo {
		NetUtility::NodeData *nd = nullptr;
		float update_rate = 0.5;
		float update_priority = 0.0;

		DeferredNodeInfo() = default;
		DeferredNodeInfo(NetUtility::NodeData *p_nd) :
				nd(p_nd) {}
		bool operator==(const DeferredNodeInfo &p_other) { return nd == p_other.nd; }
	};

private:
	bool realtime_sync_nodes_list_changed = false;
	LocalVector<RealtimeNodeInfo> realtime_sync_nodes;

	LocalVector<DeferredNodeInfo> deferred_sync_nodes;

public:
	LocalVector<int> peers;

	real_t state_notifier_timer = 0.0;

public:
	bool is_node_list_changed() const;

	const LocalVector<NetUtility::SyncGroup::RealtimeNodeInfo> &get_realtime_sync_nodes() const;
	const LocalVector<NetUtility::SyncGroup::DeferredNodeInfo> &get_deferred_sync_nodes() const;
	LocalVector<NetUtility::SyncGroup::DeferredNodeInfo> &get_deferred_sync_nodes();

	void mark_changes_as_notified();

	void add_new_node(NodeData *p_node_data, bool p_realtime);
	void remove_node(NodeData *p_node_data);

	void notify_new_variable(NodeData *p_node_data, const StringName &p_var_name);
	void notify_variable_changed(NodeData *p_node_data, const StringName &p_var_name);

	void set_deferred_update_rate(NetUtility::NodeData *p_node_data, real_t p_update_rate);
	real_t get_deferred_update_rate(const NetUtility::NodeData *p_node_data) const;
	void sort_deferred_node_by_update_priority();
};

} // namespace NetUtility

#endif
