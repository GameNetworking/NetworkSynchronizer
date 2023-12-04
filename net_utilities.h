#pragma once

#include "core/config/project_settings.h"
#include "core/templates/local_vector.h"

#include "core/core.h"
#include "core/processor.h"
#include "core/var_data.h"
#include <chrono>
#include <map>
#include <string>
#include <vector>

#ifdef TRACY_ENABLE

#include "modules/godot_tracy/profiler.h"

#define NS_PROFILING_ENABLED

#define NS_PROFILE \
	ZoneScoped;

#define NS_PROFILE_WITH_INFO(str) \
	ZoneScoped;                   \
	ZoneText(str.c_str(), str.size());

#define NS_PROFILE_NAMED(name) \
	ZoneScopedN(name);

#define NS_PROFILE_NAMED_WITH_INFO(name, str) \
	ZoneScopedN(name);                        \
	ZoneText(str.c_str(), str.size());

#define NS_PROFILE_NODE                                          \
	ZoneScoped;                                                  \
	CharString c = String(get_path()).utf8();                    \
	if (c.size() >= std::numeric_limits<std::uint16_t>::max()) { \
		c.resize(std::numeric_limits<std::uint16_t>::max() - 1); \
	}                                                            \
	ZoneText(c.ptr(), c.size());

#define NS_PROFILE_SET_INFO(str) \
	ZoneText(str.c_str(), str.size());

#else

#define NS_PROFILE
#define NS_PROFILE_WITH_INFO(str)
#define NS_PROFILE_NAMED(name)
#define NS_PROFILE_NAMED_WITH_INFO(name, str)
#define NS_PROFILE_NODE
#define NS_PROFILE_SET_INFO(str)

#endif

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

namespace MapFunc {

template <class K, class V>
V *get_or_null(std::map<K, V> &p_map, const K &p_key) {
	typename std::map<K, V>::iterator it = p_map.find(p_key);
	if (it != p_map.end()) {
		return &(it->second);
	} else {
		return nullptr;
	}
}

template <class K, class V>
const V *get_or_null(const std::map<K, V> &p_map, const K &p_key) {
	typename std::map<K, V>::const_iterator it = p_map.find(p_key);
	if (it != p_map.end()) {
		return &(it->second);
	} else {
		return nullptr;
	}
}

template <class K, class V>
const V &at(const std::map<K, V> &p_map, const K &p_key, const V &p_default) {
	typename std::map<K, V>::const_iterator it = p_map.find(p_key);
	if (it != p_map.end()) {
		return it->second;
	} else {
		return p_default;
	}
}

/// Insert or assign the `p_val` into the map at index `p_key`.
template <class K, class V>
void assign(std::map<K, V> &p_map, const K &p_key, const V &p_val) {
	p_map.insert_or_assign(p_key, p_val);
}

/// Insert or assign the `p_val` into the map at index `p_key`.
template <class K, class V>
void assign(std::map<K, V> &p_map, const K &p_key, V &&p_val) {
	p_map.insert_or_assign(p_key, std::move(p_val));
}

/// Insert `p_val` only if `p_key` doesn't exists.
template <class K, class V>
typename std::map<K, V>::iterator insert_if_new(std::map<K, V> &p_map, const K &p_key, const V &p_val) {
	auto res = p_map.insert(std::make_pair(p_key, p_val));
	auto it = res.first;
	//const bool inserted = res.second;
	return it;
}
}; //namespace MapFunc

namespace VecFunc {

template <class V, typename T>
typename std::vector<V>::const_iterator find(const std::vector<V> &p_vec, const T &p_val) {
	return std::find(p_vec.begin(), p_vec.end(), p_val);
}

template <class V, typename T>
typename std::vector<V>::iterator find(std::vector<V> &r_vec, const T &p_val) {
	return std::find(r_vec.begin(), r_vec.end(), p_val);
}

template <class V, typename T>
bool has(const std::vector<V> &p_vec, const T &p_val) {
	return std::find(p_vec.begin(), p_vec.end(), p_val) != p_vec.end();
}

template <class V, typename T>
bool insert_unique(std::vector<V> &r_vec, const T &p_val) {
	if (!has(r_vec, p_val)) {
		r_vec.push_back(p_val);
		return true;
	}
	return false;
}

template <class V, typename T>
void insert_at_position_expand(std::vector<V> &r_vec, std::size_t p_index, const T &p_val, const T &p_default) {
	if (r_vec.size() <= p_index) {
		const std::size_t initial_size = r_vec.size();
		r_vec.resize(p_index + 1);
		for (std::size_t i = initial_size; i < r_vec.size(); i++) {
			r_vec[i] = p_default;
		}
	}

	r_vec[p_index] = p_val;
}

// Return the value at index or default if not set.
template <class V>
const V &at(const std::vector<V> &p_vec, const std::size_t p_index, const V &p_default) {
	if (p_vec.size() <= p_index) {
		return p_default;
	}
	return p_vec[p_index];
}

// This function is a specialized version for `std::vector<bool>` which
// `opeartor[]` and `at()` functions returns a proxy value, so we can't just
// return a reference to the internal data as we can do with the other types.
// DOC: https://en.cppreference.com/w/cpp/container/vector_bool
template <class... T>
bool at(const std::vector<bool> &p_vec, const std::size_t p_index, const bool p_default) {
	if (p_vec.size() <= p_index) {
		return p_default;
	}
	return p_vec[p_index];
}

template <class V, typename T>
void remove(std::vector<V> &r_vec, const T &p_val) {
	auto it = find(r_vec, p_val);
	if (it != r_vec.end()) {
		r_vec.erase(it);
	}
}

// Swap the element with the last one, then removes it.
template <class V, typename T>
void remove_unordered(std::vector<V> &r_vec, const T &p_val) {
	auto it = find(r_vec, p_val);
	if (it != r_vec.end()) {
		std::iter_swap(it, r_vec.rbegin());
		r_vec.pop_back();
	}
}

// Swap the element at position with the last one, then removes it.
template <class V, typename T>
void remove_at(std::vector<V> &r_vec, std::size_t p_index) {
	if (r_vec.size() >= p_index) {
		return;
	}

	remove(r_vec, r_vec.begin() + p_index);
}

// Swap the element at position with the last one, then removes it.
template <class V, typename T>
void remove_at_unordered(std::vector<V> &r_vec, std::size_t p_index) {
	if (r_vec.size() >= p_index) {
		return;
	}

	remove_unordered(r_vec, r_vec.begin() + p_index);
}
} //namespace VecFunc

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
	std::function<void(const std::vector<VarData> &p_old_vars)> listener_func;
	NetEventFlag flag;

	std::vector<ListeningVariable> watching_vars;
	std::vector<VarData> old_values;
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
	SyncGroupId sync_group_id = SyncGroupId::GLOBAL;

	// The ping between this peer and the server in ms.
	std::chrono::high_resolution_clock::time_point ping_timestamp;
	bool ping_calculation_in_progress = false;

	void set_ping(int p_ping);
	int get_ping() const;
	void set_compressed_ping(std::uint8_t p_compressed_ping) { compressed_ping = p_compressed_ping; }
	std::uint8_t get_compressed_ping() const { return compressed_ping; }

private:
	std::uint8_t compressed_ping = 0;
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
	bool simulated_sync_objects_list_changed = false;
	LocalVector<SimulatedObjectInfo> simulated_sync_objects;

	bool trickled_sync_objects_list_changed = false;
	LocalVector<TrickledObjectInfo> trickled_sync_objects;

	std::vector<int> networked_peers;
	std::vector<int> peers_with_newly_calculated_ping;

	std::vector<int> listening_peers;

public:
	uint64_t user_data = 0;

	real_t state_notifier_timer = 0.0;

public:
	bool is_realtime_node_list_changed() const;
	bool is_trickled_node_list_changed() const;
	const std::vector<int> get_peers_with_newly_calculated_ping() const;

	const LocalVector<NS::SyncGroup::SimulatedObjectInfo> &get_simulated_sync_objects() const;
	const LocalVector<NS::SyncGroup::TrickledObjectInfo> &get_trickled_sync_objects() const;
	LocalVector<NS::SyncGroup::TrickledObjectInfo> &get_trickled_sync_objects();

	void mark_changes_as_notified();

	void add_listening_peer(int p_peer);
	void remove_listening_peer(int p_peer);
	const std::vector<int> &get_listening_peers() const { return listening_peers; };

	/// Returns the `index` or `UINT32_MAX` on error.
	uint32_t add_new_sync_object(struct ObjectData *p_object_data, bool p_is_simulated);
	void remove_sync_object(std::size_t p_index, bool p_is_simulated);
	void remove_sync_object(const struct ObjectData &p_object_data);
	void replace_objects(LocalVector<SimulatedObjectInfo> &&p_new_simulated_objects, LocalVector<TrickledObjectInfo> &&p_new_trickled_objects);
	void remove_all_nodes();

	void notify_new_variable(struct ObjectData *p_object_data, const std::string &p_var_name);
	void notify_variable_changed(struct ObjectData *p_object_data, const std::string &p_var_name);

	void set_trickled_update_rate(struct ObjectData *p_object_data, real_t p_update_rate);
	real_t get_trickled_update_rate(const struct ObjectData *p_object_data) const;

	void sort_trickled_node_by_update_priority();

	void notify_peer_has_newly_calculated_ping(int p_peer);

private:
	void notify_controller_about_simulating_peers(struct ObjectData *p_object_data, bool p_simulating);
	void notify_controllers_about_simulating_peer(int p_peer, bool p_simulating);

	std::size_t find_simulated(const struct ObjectData &p_object_data) const;
	std::size_t find_trickled(const struct ObjectData &p_object_data) const;
};

NS_NAMESPACE_END
