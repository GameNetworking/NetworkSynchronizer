#pragma once

#include "core/config/project_settings.h"
#include "core/core.h"
#include "core/math/math_funcs.h"
#include "core/processor.h"
#include "core/templates/local_vector.h"
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

typedef uint32_t SyncGroupId;

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

NS_NAMESPACE_BEGIN

class NetworkedControllerBase;

namespace MapFunc {

template <class K, class V>
V *at(std::map<K, V> &p_map, const K &p_key) {
	try {
		return &p_map.at(p_key);
	} catch (std::out_of_range &e) {
		return nullptr;
	}
}

template <class K, class V>
const V *at(const std::map<K, V> &p_map, const K &p_key) {
	try {
		return &p_map.at(p_key);
	} catch (std::out_of_range &e) {
		return nullptr;
	}
}
}; //namespace MapFunc

#define ns_find(vec, val) std::find(vec.begin(), vec.end(), val)

/// Specific node listener. Alone this doesn't do much, but allows the
/// `ChangeListener` to know and keep track of the node events.
struct ListeningVariable {
	struct ObjectData *node_data = nullptr;
	VarId var_id = VarId::NONE;
	bool old_set = false;
};

/// This can track the changes of many nodes and variables. It's dispatched
/// if one or more tracked variable change during the tracked phase, specified
/// by the `NetEventFlag`.
struct ChangesListener {
	std::function<void(const std::vector<Variant> &p_old_vars)> listener_func;
	NetEventFlag flag;

	std::vector<ListeningVariable> watching_vars;
	std::vector<Variant> old_values;
	bool emitted = true;
};

struct ListenerHandle {
	std::intptr_t id;
	bool operator==(const ListenerHandle &p_o) const { return id == p_o.id; }

	static const ChangesListener *from_handle(ListenerHandle p_handle) {
		return reinterpret_cast<const ChangesListener *>(p_handle.id);
	}

	static ListenerHandle to_handle(const ChangesListener *p_listener) {
		return { reinterpret_cast<std::intptr_t>(p_listener) };
	}
};
inline static const ListenerHandle nulllistenerhandle = { 0 };

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

// This was needed to optimize the godot stringify for byte arrays.. it was slowing down perfs.
String stringify_byte_array_fast(const Vector<uint8_t> &p_array);
String stringify_fast(const Variant &p_var);

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

struct PeerData {
	ObjectNetId controller_id = ObjectNetId::NONE;
	// For new peers notify the state as soon as possible.
	bool force_notify_snapshot = true;
	// For new peers a full snapshot is needed.
	bool need_full_snapshot = true;
	// Used to know if the peer is enabled.
	bool enabled = true;
	// The Sync group this peer is in.
	SyncGroupId sync_group_id;
};

struct SyncGroup {
public:
	struct Change {
		bool unknown = false;
		RBSet<std::string> uknown_vars;
		RBSet<std::string> vars;
	};

	struct SimulatedObjectInfo {
		struct ObjectData *od = nullptr;
		Change change;

		SimulatedObjectInfo() = default;
		SimulatedObjectInfo(const SimulatedObjectInfo &) = default;
		SimulatedObjectInfo &operator=(const SimulatedObjectInfo &) = default;
		SimulatedObjectInfo &operator=(SimulatedObjectInfo &&) = default;
		SimulatedObjectInfo(struct ObjectData *p_nd) :
				od(p_nd) {}
		bool operator==(const SimulatedObjectInfo &p_other) { return od == p_other.od; }

		void update_from(const SimulatedObjectInfo &p_other) {}
	};

	struct TrickledObjectInfo {
		struct ObjectData *od = nullptr;

		/// The node update rate, relative to the godot physics processing rate.
		/// With the godot physics processing rate set to 60Hz, 0.5 means 30Hz.
		float update_rate = 0.5;

		/// INTERNAL: The update priority is calculated and updated each frame
		///           by the `ServerSynchronizer` based on the `update_rate`:
		///           the nodes with higher priority get sync.
		float _update_priority = 0.0;

		/// INTERNAL
		bool _unknown = false;

		TrickledObjectInfo() = default;
		TrickledObjectInfo(const TrickledObjectInfo &) = default;
		TrickledObjectInfo &operator=(const TrickledObjectInfo &) = default;
		TrickledObjectInfo &operator=(TrickledObjectInfo &&) = default;
		TrickledObjectInfo(struct ObjectData *p_nd) :
				od(p_nd) {}
		bool operator==(const TrickledObjectInfo &p_other) { return od == p_other.od; }

		void update_from(const TrickledObjectInfo &p_other) {
			update_rate = p_other.update_rate;
		}
	};

private:
	bool realtime_sync_nodes_list_changed = false;
	LocalVector<SimulatedObjectInfo> realtime_sync_nodes;

	bool trickled_sync_nodes_list_changed = false;
	LocalVector<TrickledObjectInfo> trickled_sync_nodes;

public:
	uint64_t user_data = 0;
	LocalVector<int> peers;

	real_t state_notifier_timer = 0.0;

public:
	bool is_realtime_node_list_changed() const;
	bool is_trickled_node_list_changed() const;

	const LocalVector<NS::SyncGroup::SimulatedObjectInfo> &get_realtime_sync_nodes() const;
	const LocalVector<NS::SyncGroup::TrickledObjectInfo> &get_trickled_sync_nodes() const;
	LocalVector<NS::SyncGroup::TrickledObjectInfo> &get_trickled_sync_nodes();

	void mark_changes_as_notified();

	/// Returns the `index` or `UINT32_MAX` on error.
	uint32_t add_new_node(struct ObjectData *p_object_data, bool p_realtime);
	void remove_node(struct ObjectData *p_object_data);
	void replace_nodes(LocalVector<SimulatedObjectInfo> &&p_new_realtime_nodes, LocalVector<TrickledObjectInfo> &&p_new_trickled_nodes);
	void remove_all_nodes();

	void notify_new_variable(struct ObjectData *p_object_data, const std::string &p_var_name);
	void notify_variable_changed(struct ObjectData *p_object_data, const std::string &p_var_name);

	void set_trickled_update_rate(struct ObjectData *p_object_data, real_t p_update_rate);
	real_t get_trickled_update_rate(const struct ObjectData *p_object_data) const;

	void sort_trickled_node_by_update_priority();
};

NS_NAMESPACE_END
