#pragma once

#include "core.h"
#include "processor.h"
#include "var_data.h"
#include <algorithm>
#include <map>

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

#define NS_PROFILE_SET_INFO(str) \
	ZoneText(str.c_str(), str.size());

#else

#define NS_PROFILE
#define NS_PROFILE_WITH_INFO(str)
#define NS_PROFILE_NAMED(name)
#define NS_PROFILE_NAMED_WITH_INFO(name, str)
#define NS_PROFILE_SET_INFO(str)

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

/// Insert or update the `p_val` into the map at index `p_key`.
template <class K, class V>
void assign(std::map<K, V> &p_map, const K &p_key, const V &p_val) {
	p_map.insert_or_assign(p_key, p_val);
}

/// Insert or update the `p_val` into the map at index `p_key`.
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

/// Insert `p_val` only if `p_key` doesn't exists. With move.
template <class K, class V>
typename std::map<K, V>::iterator insert_if_new(std::map<K, V> &p_map, const K &p_key, V &&p_val) {
	return p_map.insert(std::make_pair(p_key, std::move(p_val))).first;
}
}; //namespace MapFunc

namespace VecFunc {
inline std::size_t index_none() {
	return std::numeric_limits<std::size_t>::max();
}

template <class V, typename T>
std::size_t find_index(const std::vector<V> &p_vec, const T &p_val) {
	const auto it = std::find(p_vec.begin(), p_vec.end(), p_val);
	return it == p_vec.end() ? index_none() : std::distance(p_vec.begin(), it);
}

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
V &insert_unique_and_get(std::vector<V> &r_vec, const T &p_val) {
	auto it = find(r_vec, p_val);
	if (it == r_vec.end()) {
		r_vec.push_back(p_val);
		return r_vec[r_vec.size() - 1];
	}
	return *it;
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

template <class V, typename T>
V &get_or_expand(std::vector<V> &r_vec, std::size_t p_index, const T &p_default) {
	if (r_vec.size() <= p_index) {
		insert_at_position_expand(r_vec, p_index, p_default, p_default);
	}

	return r_vec[p_index];
}

// Return the value at index or default if not set.
template <class V>
const V &at(const std::vector<V> &p_vec, const std::size_t p_index, const V &p_default) {
	if (p_vec.size() <= p_index) {
		return p_default;
	}
	return p_vec[p_index];
}

// This function is a specialized version that handles the `std::vector<bool>` which
// `operator[]` and `at()` functions returns a proxy value, unlikely all the other
// STL `std::vector<*>` that return a reference. This version returns a non ref
// to avoid any memory corruptions.
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

// Removes the element without changing order.
template <class V>
void remove_at(std::vector<V> &r_vec, std::size_t p_index) {
	if (r_vec.size() <= p_index) {
		return;
	}

	r_vec.erase(r_vec.begin() + p_index);
}

// Swap the element at position with the last one, then removes it.
template <class V>
void remove_at_unordered(std::vector<V> &r_vec, std::size_t p_index) {
	if (r_vec.size() <= p_index) {
		return;
	}

	std::iter_swap(r_vec.begin() + p_index, r_vec.rbegin());
	r_vec.pop_back();
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

	bool operator==(const ListenerHandle &p_o) const {
		return id == p_o.id;
	}

	static const ChangesListener *from_handle(ListenerHandle p_handle) {
		return reinterpret_cast<const ChangesListener *>(p_handle.id);
	}

	static ListenerHandle to_handle(const ChangesListener *p_listener) {
		return ListenerHandle{ reinterpret_cast<std::intptr_t>(p_listener) };
	}
};

inline static const ListenerHandle nulllistenerhandle = { 0 };

struct PeerServerData {
	// For new peers notify the state as soon as possible.
	bool force_notify_snapshot = true;

	// For new peers a full snapshot is needed.
	bool need_full_snapshot = true;

	// How much time (seconds) from the latest latency update sent via snapshot.
	float latency_update_via_snapshot_sec = 0.0;

	// How much time (seconds) from the latest update sent to the client.
	float netstats_peer_update_sec = 0.0;
};

struct SyncGroup {
public:
	struct Change {
		bool unknown = false;
		std::vector<VarId> uknown_vars;
		std::vector<VarId> vars;
	};

	struct SimulatedObjectInfo {
		struct ObjectData *od = nullptr;
		Change change;

		SimulatedObjectInfo() = default;
		SimulatedObjectInfo(const SimulatedObjectInfo &) = default;
		SimulatedObjectInfo &operator=(const SimulatedObjectInfo &) = default;
		SimulatedObjectInfo &operator=(SimulatedObjectInfo &&) = default;

		SimulatedObjectInfo(struct ObjectData *p_nd) :
			od(p_nd) {
		}

		bool operator==(const SimulatedObjectInfo &p_other) const {
			return od == p_other.od;
		}

		void update_from(const SimulatedObjectInfo &p_other) {
		}
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
			od(p_nd) {
		}

		bool operator==(const TrickledObjectInfo &p_other) const {
			return od == p_other.od;
		}

		void update_from(const TrickledObjectInfo &p_other) {
			update_rate = p_other.update_rate;
		}
	};

public:
	SyncGroupId group_id = SyncGroupId::NONE;
	class SceneSynchronizerBase *scene_sync = nullptr;

private:
	bool simulated_sync_objects_list_changed = false;
	std::vector<SimulatedObjectInfo> simulated_sync_objects;

	bool trickled_sync_objects_list_changed = false;
	std::vector<TrickledObjectInfo> trickled_sync_objects;

	/// Contains the list of peers being networked by this sync group.
	/// In other terms, if an object controller by peer 76 is contained into
	/// the simulated or trickled list that peer is into this array.
	std::vector<int> networked_peers;
	/// The list of peers that have at least one objects into the simulated
	/// vector.
	std::vector<int> simulating_peers;
	/// The peers for which the server measured the latency.
	std::vector<int> peers_with_newly_calculated_latency;

	std::vector<int> listening_peers;

public:
	uint64_t user_data = 0;

	float state_notifier_timer = 0.0;

public:
	bool is_realtime_node_list_changed() const;
	bool is_trickled_node_list_changed() const;
	const std::vector<int> get_peers_with_newly_calculated_latency() const;

	const std::vector<NS::SyncGroup::SimulatedObjectInfo> &get_simulated_sync_objects() const;
	const std::vector<NS::SyncGroup::TrickledObjectInfo> &get_trickled_sync_objects() const;
	std::vector<NS::SyncGroup::TrickledObjectInfo> &get_trickled_sync_objects();

	void mark_changes_as_notified(bool p_only_non_state_update);

	void add_listening_peer(int p_peer);
	void remove_listening_peer(int p_peer);

	const std::vector<int> &get_listening_peers() const {
		return listening_peers;
	};

	const std::vector<int> &get_networked_peers() const {
		return networked_peers;
	}

	const std::vector<int> &get_simulating_peers() const {
		return simulating_peers;
	}

	/// Returns the `index` or `UINT32_MAX` on error.
	std::size_t add_new_sync_object(struct ObjectData *p_object_data, bool p_is_simulated);
	void notify_sync_object_name_is_known(struct ObjectData &p_object_data);
	void remove_sync_object(std::size_t p_index, bool p_is_simulated);
	void remove_sync_object(const struct ObjectData &p_object_data);
	void replace_objects(std::vector<SimulatedObjectInfo> &&p_new_simulated_objects, std::vector<TrickledObjectInfo> &&p_new_trickled_objects);
	void remove_all_nodes();

	void notify_new_variable(struct ObjectData *p_object_data, VarId p_var_id);
	void notify_variable_changed(struct ObjectData *p_object_data, VarId p_var_id);

	void set_trickled_update_rate(struct ObjectData *p_object_data, float p_update_rate);
	float get_trickled_update_rate(const struct ObjectData *p_object_data) const;

	void sort_trickled_node_by_update_priority();

	void notify_peer_has_newly_calculated_latency(int p_peer);

	void notify_controller_changed(NS::ObjectData *p_object_data, int p_previous_controlling_peer);

	// Used when a new listener is added or removed.
	void notify_simulating_peers_about_listener_status(int p_peer_listener, bool p_simulating);
	// Used when a new simulated object (which is controlled) is added, to notify
	// such controller about the listeners.
	void update_listeners_to_simulating_peer(int p_simulating_peer, bool p_simulating);

	// Removes the peer from this sync group if it's not associated to any object
	// data into this group.
	void validate_peer_association(int p_peer);

	bool has_simulated(const struct ObjectData &p_object_data) const;
	bool has_trickled(const struct ObjectData &p_object_data) const;

private:
	std::size_t find_simulated(const struct ObjectData &p_object_data) const;
	std::size_t find_trickled(const struct ObjectData &p_object_data) const;
};

NS_NAMESPACE_END