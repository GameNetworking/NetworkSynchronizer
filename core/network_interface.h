#pragma once

#include "core.h"
#include "core/string/string_name.h"
#include "core/variant/variant.h"
#include <functional>

NS_NAMESPACE_BEGIN

class NetworkInterface {
public:
	/// Must be called when this interface receives an RPC.
	std::function<void(int p_peer, const StringName &p_func, const Vector<Variant> &p_vars)> on_rpc_received;
	std::function<void(int /*p_peer*/)> on_peer_connected_callback;
	std::function<void(int /*p_peer*/)> on_peer_disconnected_callback;

public:
	virtual ~NetworkInterface() = default;

public: // ---------------------------------------------------------------- APIs
	virtual String get_name() const = 0;

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
	virtual bool is_local_peer_authority_of_this_unit() const = 0;

	/// Configures the rpc call.
	virtual void configure_rpc(
			const StringName &p_func,
			bool p_call_local,
			bool p_is_reliable) = 0;

	/// Returns the peer that remotelly called the currently executed rpc function.
	/// Should be called always from an rpc function.
	virtual int rpc_get_sender() const = 0;

	/// Calls an rpc.
	template <typename... VarArgs>
	void rpc(int p_peer_id, const StringName &p_method, VarArgs... p_args);

protected:
	/// This is just for internal usage.
	/// Implements the rpc send mechanism.
	virtual void rpc_array(
			int p_peer_id,
			const StringName &p_method,
			const Variant **p_arg,
			int p_argcount) = 0;
};

template <typename... VarArgs>
void NetworkInterface::rpc(int p_peer_id, const StringName &p_method, VarArgs... p_args) {
	Variant args[sizeof...(p_args) + 1] = { p_args..., Variant() }; // +1 makes sure zero sized arrays are also supported.
	const Variant *argptrs[sizeof...(p_args) + 1];
	for (uint32_t i = 0; i < sizeof...(p_args); i++) {
		argptrs[i] = &args[i];
	}

	rpc_array(
			p_peer_id,
			p_method,
			sizeof...(p_args) == 0 ? nullptr : (const Variant **)argptrs,
			sizeof...(p_args));
}

NS_NAMESPACE_END
