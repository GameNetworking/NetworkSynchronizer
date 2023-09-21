#pragma once

#include "core.h"
#include "core/string/string_name.h"
#include "core/variant/variant.h"
#include <functional>
#include <vector>

NS_NAMESPACE_BEGIN

class NetworkInterface {
protected:
	struct RPCInfo {
		bool is_reliable = false;
		bool call_local = false;
		std::function<void(const Variant *, int)> func;
	};

protected:
	std::vector<RPCInfo> rpcs_info;
	int rpc_last_sender = 0;

public:
	virtual ~NetworkInterface() = default;

public: // ---------------------------------------------------------------- APIs
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
	virtual Vector<int> fetch_connected_peers() const = 0;

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
	uint8_t rpc_config(std::function<void(ARGS...)> p_rpc_func, bool p_reliable, bool p_call_local) {
		// Stores the rpc info.

		// Create an intermediate lambda, which is easy to store, that is
		// responsible to execute the user rpc function.
		std::function<void(const Variant *, int)> func =
				[p_rpc_func](const Variant *p_args, int p_count) {
					// Unless there is a bug into the rpc mechanism, this is never triggered.
					CRASH_COND(p_count != sizeof...(ARGS));
					//internal_call_rpc<ARGS...>(p_rpc_func, p_args);
					internal_call_rpc(p_rpc_func, (const Variant *)p_args);
				};

		const uint8_t rpc_index = rpcs_info.size();
		rpcs_info.push_back({ p_reliable, p_call_local, func });
		return rpc_index;
	}

	/// Calls an rpc.
	template <typename... ARGS>
	void rpc(uint8_t p_rpc_id, int p_peer_id, ARGS... p_args);

	/// This function must be called by the `Network` manager when this unit receives an rpc.
	void rpc_receive(uint8_t p_rpc_id, int p_sender_peer, const Variant *p_args, int p_count) {
		ERR_FAIL_COND_MSG(rpcs_info.size() <= p_rpc_id, "The received rpc `" + itos(p_rpc_id) + "` doesn't exists.");
		rpc_last_sender = p_sender_peer;
		rpcs_info[p_rpc_id].func(p_args, p_count);
	}

protected:
	virtual void rpc_send(uint8_t p_rpc_id, int p_peer_recipient, const Variant *p_args, int p_count) = 0;

private: // ------------------------------------------------------- RPC internal
	template <typename... ARGS>
	static void internal_call_rpc(std::function<void()> &p_func, const Variant *p_args);

	template <typename A1>
	static void internal_call_rpc(std::function<void(A1)> p_func, const Variant *p_args);

	template <typename A1, typename A2>
	static void internal_call_rpc(std::function<void(A1, A2)> &p_func, const Variant *p_args);

	template <typename A1, typename A2, typename A3>
	static void internal_call_rpc(std::function<void(A1, A2, A3)> &p_func, const Variant *p_args);

	template <int n, typename A1, typename A2, typename A3, typename A4>
	static void internal_call_rpc(std::function<void(A1, A2, A3, A4)> &p_func, const Variant *p_args);

	template <int n, typename A1, typename A2, typename A3, typename A4, typename A5>
	static void internal_call_rpc(std::function<void(A1, A2, A3, A4, A5)> &p_func, const Variant *p_args);

	template <int n, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
	static void internal_call_rpc(std::function<void(A1, A2, A3, A4, A5, A6)> &p_func, const Variant *p_args);
};

template <typename... ARGS>
void NetworkInterface::rpc(uint8_t p_rpc_id, int p_peer_id, ARGS... p_args) {
	// Convert the raw properties in an array of Variants.
	Variant args[sizeof...(p_args) + 1] = { p_args..., Variant() }; // +1 makes sure zero sized arrays are also supported.

	rpc_send(
			p_rpc_id,
			p_peer_id,
			sizeof...(p_args) == 0 ? nullptr : args,
			sizeof...(p_args));
}

//template <int n>
template <typename... ARGS>
void NetworkInterface::internal_call_rpc(std::function<void()> &p_func, const Variant *p_args) {
	p_func();
}

template <typename A1>
void NetworkInterface::internal_call_rpc(std::function<void(A1)> p_func, const Variant *p_args) {
	p_func(p_args[0]);
}

template <typename A1, typename A2>
void NetworkInterface::internal_call_rpc(std::function<void(A1, A2)> &p_func, const Variant *p_args) {
	p_func(p_args[0], p_args[1]);
}

template <typename A1, typename A2, typename A3>
void NetworkInterface::internal_call_rpc(std::function<void(A1, A2, A3)> &p_func, const Variant *p_args) {
	p_func(p_args[0], p_args[1], p_args[2]);
}

template <int n, typename A1, typename A2, typename A3, typename A4>
void NetworkInterface::internal_call_rpc(std::function<void(A1, A2, A3, A4)> &p_func, const Variant *p_args) {
	p_func(p_args[0], p_args[1], p_args[2], p_args[3]);
}

template <int n, typename A1, typename A2, typename A3, typename A4, typename A5>
void NetworkInterface::internal_call_rpc(std::function<void(A1, A2, A3, A4, A5)> &p_func, const Variant *p_args) {
	p_func(p_args[0], p_args[1], p_args[2], p_args[3], p_args[4]);
}

template <int n, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
void NetworkInterface::internal_call_rpc(std::function<void(A1, A2, A3, A4, A5, A6)> &p_func, const Variant *p_args) {
	p_func(p_args[0], p_args[1], p_args[2], p_args[3], p_args[4], p_args[5]);
}

NS_NAMESPACE_END
