#pragma once

#include "network_interface_define.h"
#include "ensure.h"
#include "network_codec.h"
#include "net_utilities.h"
#include "scene_synchronizer_debugger.h"
#include "peer_data.h"

#include <vector>

NS_NAMESPACE_BEGIN
template <typename... ARGs>
class RpcHandle {
	friend class NetworkInterface;

	mutable class SceneSynchronizerDebugger *debugger = nullptr;
	std::uint8_t index = std::numeric_limits<std::uint8_t>::max();
	// This can be optional, if not specified the rpc destination is the SceneSynchronizer.
	ObjectLocalId target_object_id = ObjectLocalId::NONE;

	RpcHandle(std::uint8_t p_index, ObjectLocalId p_target = ObjectLocalId::NONE) :
		debugger(nullptr),
		index(p_index),
		target_object_id(p_target) {
	}

public:
	RpcHandle() = default;

	SceneSynchronizerDebugger &get_debugger() const {
		return *debugger;
	}

	ObjectLocalId get_target_id() const {
		return target_object_id;
	}

	std::uint8_t get_index() const {
		return index;
	}

	void reset() {
		index = std::numeric_limits<std::uint8_t>::max();
	}

	void rpc(class NetworkInterface &p_network_interface, int p_peer_id, ARGs... p_args) const;
	void rpc(class NetworkInterface &p_network_interface, const std::vector<int> &p_peers_recipients, ARGs... p_args) const;
};

class NetworkInterface {
	template <typename... ARGs>
	friend class RpcHandle;

protected:
	mutable SceneSynchronizerDebugger debugger;
	class SceneSynchronizerBase *scene_synchronizer = nullptr;
	std::vector<RPCInfo> rpcs_info;
	int rpc_last_sender = 0;

public:
	virtual ~NetworkInterface() = default;

public: // ----------------------------------------------------------- Interface
	void set_scene_synchronizer(class SceneSynchronizerBase *p_scene_sync);

	SceneSynchronizerDebugger &get_debugger() const;

	virtual void reset() {
		rpcs_info.clear();
		rpc_last_sender = 0;
	}

	virtual std::string get_owner_name() const = 0;

	virtual int get_server_peer() const = 0;

	/// Call this function to start receiving events on peer connection / disconnection.
	virtual void start_listening_peer_connection(
			std::function<void(int /*p_peer*/)> p_on_peer_connected_callback,
			std::function<void(int /*p_peer*/)> p_on_peer_disconnected_callback) = 0;

	/// Call this function to stop receiving events on peer connection / disconnection.
	virtual void stop_listening_peer_connection() = 0;

	/// Fetch the current client peer_id
	virtual int get_local_peer_id() const = 0;

	/// Fetch the list with all the connected peers.
	virtual void fetch_connected_peers(std::vector<int> &p_connected_peers) const = 0;

	/// Can be used to verify if the local peer is connected to a server.
	virtual bool is_local_peer_networked() const = 0;
	/// Can be used to verify if the local peer is the server.
	virtual bool is_local_peer_server() const = 0;

	/// This function is called by the `SceneSynchronizer` to update
	/// the network `stats` for the given peer.
	/// NOTE: This function is called only on the server.
	virtual void server_update_net_stats(int p_peer, PeerData &r_peer_data) const = 0;

public: // ---------------------------------------------------------------- APIs
	/// Returns the peer that remotelly called the currently executed rpc function.
	/// Should be called always from an rpc function.
	int rpc_get_sender() const {
		return rpc_last_sender;
	}

	template <typename... ARGS>
	RpcHandle<ARGS...> rpc_config(std::function<void(ARGS...)> p_rpc_func, bool p_reliable, bool p_call_local, ObjectLocalId p_rpc_owner = ObjectLocalId::NONE, std::vector<RPCInfo> *r_object_data_rpc_info = nullptr) {
		if (r_object_data_rpc_info) {
			NS_ASSERT_COND(p_rpc_owner != ObjectLocalId::NONE);
		}

		return __rpc_config(
				p_rpc_func,
				r_object_data_rpc_info ? p_rpc_owner : ObjectLocalId::NONE,
				r_object_data_rpc_info ? *r_object_data_rpc_info : rpcs_info,
				p_reliable,
				p_call_local);
	}

private:
	template <typename... ARGS>
	RpcHandle<ARGS...> __rpc_config(
			std::function<void(ARGS...)> p_rpc_func,
			ObjectLocalId p_target_id,
			std::vector<RPCInfo> &r_rpcs_info,
			bool p_reliable,
			bool p_call_local) {
		// Stores the rpc info.

		// Create an intermediate lambda, which is easy to store, that is
		// responsible to execute the user rpc function.
		std::function<void(DataBuffer &)> func =
				[p_rpc_func](DataBuffer &p_db) {
			internal_call_rpc(p_rpc_func, p_db);
		};

		const std::uint8_t rpc_index = std::uint8_t(r_rpcs_info.size());
		r_rpcs_info.push_back({ p_reliable, p_call_local, func });
		return RpcHandle<ARGS...>(rpc_index, p_target_id);
	}

public:
	/// This function must be called by the `Network` manager when this unit receives an rpc.
	void rpc_receive(int p_sender_peer, DataBuffer &p_db);

	const RPCInfo *get_rpc_info(uint8_t p_rpc_id) const {
		NS_ENSURE_V(p_rpc_id < rpcs_info.size(), nullptr);
		return &rpcs_info[p_rpc_id];
	}

protected:
	virtual void rpc_send(int p_peer_recipient, bool p_reliable, DataBuffer &&p_db) = 0;

	void __fetch_rpc_info_from_object(ObjectLocalId p_id, int p_rpc_index, ObjectNetId &r_net_id, RPCInfo *&r_rpc_info) const;

private: // ------------------------------------------------------- RPC internal
	template <typename... ARGS>
	static void internal_call_rpc(std::function<void()> p_func, DataBuffer &p_buffer);

	template <typename A1>
	static void internal_call_rpc(std::function<void(A1)> p_func, DataBuffer &p_buffer);

	template <typename A1, typename A2>
	static void internal_call_rpc(std::function<void(A1, A2)> p_func, DataBuffer &p_buffer);

	template <typename A1, typename A2, typename A3>
	static void internal_call_rpc(std::function<void(A1, A2, A3)> p_func, DataBuffer &p_buffer);

	template <typename A1, typename A2, typename A3, typename A4>
	static void internal_call_rpc(std::function<void(A1, A2, A3, A4)> p_func, DataBuffer &p_buffer);

	template <typename A1, typename A2, typename A3, typename A4, typename A5>
	static void internal_call_rpc(std::function<void(A1, A2, A3, A4, A5)> p_func, DataBuffer &p_buffer);

	template <typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
	static void internal_call_rpc(std::function<void(A1, A2, A3, A4, A5, A6)> p_func, DataBuffer &p_buffer);
};

template <typename... ARGs>
void RpcHandle<ARGs...>::rpc(NetworkInterface &p_network_interface, int p_peer_id, ARGs... p_args) const {
	rpc(p_network_interface, std::vector<int>(1, p_peer_id), p_args...);
}

template <typename... ARGs>
void RpcHandle<ARGs...>::rpc(NetworkInterface &p_network_interface, const std::vector<int> &p_peers_recipients, ARGs... p_args) const {
	debugger = &p_network_interface.get_debugger();

	DataBuffer db(get_debugger());
	db.begin_write(get_debugger(), 0);

	RPCInfo *rpc_info = nullptr;
	if (target_object_id != ObjectLocalId::NONE) {
		// Fetch the ObjectData
		ObjectNetId net_id;
		p_network_interface.__fetch_rpc_info_from_object(target_object_id, index, net_id, rpc_info);
		NS_ENSURE(net_id!=ObjectNetId::NONE);
		NS_ENSURE(rpc_info!=nullptr);

		db.add(true);
		db.add(net_id.id);
	} else {
		db.add(false);
		NS_ENSURE(p_network_interface.rpcs_info.size() > index);
		rpc_info = &p_network_interface.rpcs_info[index];
	}

	// Add the rpc id.
	db.add(index);

	// Encode the properties into a DataBuffer.
	encode_variables<0>(db, p_args...);

	db.dry();
	db.begin_read(get_debugger());

	bool called_locally = false;
	for (int peer : p_peers_recipients) {
		db.begin_read(get_debugger());
		if (p_network_interface.get_local_peer_id() == peer) {
			// This rpc goes directly to self
			p_network_interface.rpc_receive(p_network_interface.get_local_peer_id(), db);
			called_locally = true;
		} else {
			p_network_interface.rpc_send(peer, rpc_info->is_reliable, std::move(db));
		}
	}

	if (rpc_info->call_local && !called_locally) {
		db.begin_read(get_debugger());
		p_network_interface.rpc_receive(p_network_interface.get_local_peer_id(), db);
	}

	debugger = nullptr;
}

template <typename... ARGS>
void NetworkInterface::internal_call_rpc(std::function<void()> p_func, DataBuffer &p_buffer) {
	p_func();
}

template <typename A1>
void NetworkInterface::internal_call_rpc(std::function<void(A1)> p_func, DataBuffer &p_buffer) {
	typename std::remove_const<typename std::remove_reference<A1>::type>::type p1;
	decode_variable(p1, p_buffer);
	p_func(p1);
}

template <typename A1, typename A2>
void NetworkInterface::internal_call_rpc(std::function<void(A1, A2)> p_func, DataBuffer &p_buffer) {
	typename std::remove_const<typename std::remove_reference<A1>::type>::type p1;
	decode_variable(p1, p_buffer);

	typename std::remove_const<typename std::remove_reference<A2>::type>::type p2;
	decode_variable(p2, p_buffer);

	p_func(p1, p2);
}

template <typename A1, typename A2, typename A3>
void NetworkInterface::internal_call_rpc(std::function<void(A1, A2, A3)> p_func, DataBuffer &p_buffer) {
	typename std::remove_const<typename std::remove_reference<A1>::type>::type p1;
	decode_variable(p1, p_buffer);

	typename std::remove_const<typename std::remove_reference<A2>::type>::type p2;
	decode_variable(p2, p_buffer);

	typename std::remove_const<typename std::remove_reference<A3>::type>::type p3;
	decode_variable(p3, p_buffer);

	p_func(p1, p2, p3);
}

template <typename A1, typename A2, typename A3, typename A4>
void NetworkInterface::internal_call_rpc(std::function<void(A1, A2, A3, A4)> p_func, DataBuffer &p_buffer) {
	typename std::remove_const<typename std::remove_reference<A1>::type>::type p1;
	decode_variable(p1, p_buffer);

	typename std::remove_const<typename std::remove_reference<A2>::type>::type p2;
	decode_variable(p2, p_buffer);

	typename std::remove_const<typename std::remove_reference<A3>::type>::type p3;
	decode_variable(p3, p_buffer);

	typename std::remove_const<typename std::remove_reference<A4>::type>::type p4;
	decode_variable(p4, p_buffer);

	p_func(p1, p2, p3, p4);
}

template <typename A1, typename A2, typename A3, typename A4, typename A5>
void NetworkInterface::internal_call_rpc(std::function<void(A1, A2, A3, A4, A5)> p_func, DataBuffer &p_buffer) {
	typename std::remove_const<typename std::remove_reference<A1>::type>::type p1;
	decode_variable(p1, p_buffer);

	typename std::remove_const<typename std::remove_reference<A2>::type>::type p2;
	decode_variable(p2, p_buffer);

	typename std::remove_const<typename std::remove_reference<A3>::type>::type p3;
	decode_variable(p3, p_buffer);

	typename std::remove_const<typename std::remove_reference<A4>::type>::type p4;
	decode_variable(p4, p_buffer);

	typename std::remove_const<typename std::remove_reference<A5>::type>::type p5;
	decode_variable(p5, p_buffer);

	p_func(p1, p2, p3, p4, p5);
}

template <typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
void NetworkInterface::internal_call_rpc(std::function<void(A1, A2, A3, A4, A5, A6)> p_func, DataBuffer &p_buffer) {
	typename std::remove_const<typename std::remove_reference<A1>::type>::type p1;
	decode_variable(p1, p_buffer);

	typename std::remove_const<typename std::remove_reference<A2>::type>::type p2;
	decode_variable(p2, p_buffer);

	typename std::remove_const<typename std::remove_reference<A3>::type>::type p3;
	decode_variable(p3, p_buffer);

	typename std::remove_const<typename std::remove_reference<A4>::type>::type p4;
	decode_variable(p4, p_buffer);

	typename std::remove_const<typename std::remove_reference<A5>::type>::type p5;
	decode_variable(p5, p_buffer);

	typename std::remove_const<typename std::remove_reference<A6>::type>::type p6;
	decode_variable(p6, p_buffer);

	p_func(p1, p2, p3, p4, p5, p6);
}

NS_NAMESPACE_END