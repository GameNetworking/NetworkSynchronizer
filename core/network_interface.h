#pragma once

#include "core.h"
#include "ensure.h"
#include "network_codec.h"
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

NS_NAMESPACE_BEGIN

template <typename... ARGs>
class RpcHandle {
	friend class NetworkInterface;

	std::uint8_t index = std::numeric_limits<std::uint8_t>::max();
	RpcHandle(std::uint8_t p_index) :
			index(p_index) {}

public:
	RpcHandle() = default;

	std::uint8_t get_index() const { return index; }
	void reset() {
		index = std::numeric_limits<std::uint8_t>::max();
	}

	void rpc(class NetworkInterface &p_interface, int p_peer_id, ARGs... p_args) const;
};

class NetworkInterface {
	template <typename... ARGs>
	friend class RpcHandle;

public:
	struct RPCInfo {
		bool is_reliable = false;
		bool call_local = false;
		std::function<void(DataBuffer &p_db)> func;
	};

protected:
	std::vector<RPCInfo> rpcs_info;
	int rpc_last_sender = 0;

public:
	virtual ~NetworkInterface() = default;

public: // ---------------------------------------------------------------- APIs
	virtual void clear() {
		rpcs_info.clear();
		rpc_last_sender = 0;
	}

	virtual String get_name() const = 0;

	virtual int get_server_peer() const = 0;

	/// Call this function to start receiving events on peer connection / disconnection.
	virtual void start_listening_peer_connection(
			std::function<void(int /*p_peer*/)> p_on_peer_connected_callback,
			std::function<void(int /*p_peer*/)> p_on_peer_disconnected_callback) = 0;

	/// Call this function to stop receiving events on peer connection / disconnection.
	virtual void stop_listening_peer_connection() = 0;

	/// Fetch the current client peer_id
	virtual int fetch_local_peer_id() const = 0;

	/// Fetch the list with all the connected peers.
	virtual void fetch_connected_peers(std::vector<int> &p_connected_peers) const = 0;

	/// Get the peer id controlling this unit.
	virtual int get_unit_authority() const = 0;

	/// Can be used to verify if the local peer is connected to a server.
	virtual bool is_local_peer_networked() const = 0;
	/// Can be used to verify if the local peer is the server.
	virtual bool is_local_peer_server() const = 0;

	/// Can be used to verify if the local peer is the authority of this unit.
	virtual bool is_local_peer_authority_of_this_unit() const {
		return get_unit_authority() == fetch_local_peer_id();
	}

	/// Returns the peer that remotelly called the currently executed rpc function.
	/// Should be called always from an rpc function.
	int rpc_get_sender() const {
		return rpc_last_sender;
	}

	template <typename... ARGS>
	RpcHandle<ARGS...> rpc_config(std::function<void(ARGS...)> p_rpc_func, bool p_reliable, bool p_call_local) {
		// Stores the rpc info.

		// Create an intermediate lambda, which is easy to store, that is
		// responsible to execute the user rpc function.
		std::function<void(DataBuffer &)> func =
				[p_rpc_func](DataBuffer &p_db) {
					internal_call_rpc(p_rpc_func, p_db);
				};

		const std::uint8_t rpc_index = rpcs_info.size();
		rpcs_info.push_back({ p_reliable, p_call_local, func });
		return RpcHandle<ARGS...>(rpc_index);
	}

	/// Calls an rpc.
	//template <typename... ARGS>
	//void rpc(RpcHandle<ARGS...> p_rpc_id, int p_peer_id, ARGS... p_args);
	template <typename... H>
	void rpc(RpcHandle<void(H...)> p_rpc_id, int p_peer_id, typename RpcHandle<void(H...)>::TYPE p_args);

	void rpc(RpcHandle<void()> p_rpc_id, int p_peer_id);

	/// This function must be called by the `Network` manager when this unit receives an rpc.
	void rpc_receive(int p_sender_peer, DataBuffer &p_db) {
		rpc_last_sender = p_sender_peer;
		p_db.begin_read();
		std::uint8_t rpc_id;
		p_db.read(rpc_id);
		ENSURE_MSG(rpc_id < rpcs_info.size(), "The received rpc contains a broken RPC ID: `" + std::to_string(rpc_id) + "`, the `rpcs_info` size is `" + std::to_string(rpcs_info.size()) + "`.");
		// This can't be triggered because the rpc always points to a valid
		// function at this point.
		ASSERT_COND(rpcs_info[rpc_id].func);
		rpcs_info[rpc_id].func(p_db);
	}

	const RPCInfo *get_rpc_info(uint8_t p_rpc_id) const {
		ENSURE_V(p_rpc_id < rpcs_info.size(), nullptr);
		return &rpcs_info[p_rpc_id];
	}

protected:
	virtual void rpc_send(int p_peer_recipient, bool p_reliable, DataBuffer &&p_db) = 0;

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
void RpcHandle<ARGs...>::rpc(NetworkInterface &p_interface, int p_peer_id, ARGs... p_args) const {
	ENSURE(p_interface.rpcs_info.size() > index);

	DataBuffer db;
	db.begin_write(0);

	// Add the rpc id.
	db.add(index);

	// Encode the properties into a DataBuffer.
	encode_variables<0>(db, p_args...);

	db.dry();
	db.begin_read();

	if (p_interface.rpcs_info[index].call_local) {
		p_interface.rpc_receive(p_interface.fetch_local_peer_id(), db);
	}

	db.begin_read();
	p_interface.rpc_send(p_peer_id, p_interface.rpcs_info[index].is_reliable, std::move(db));
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
