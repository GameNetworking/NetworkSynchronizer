#pragma once

#include "core/string/string_name.h"
#include "scene/main/node.h"

class NetworkedUnit : public Node {
	GDCLASS(NetworkedUnit, Node);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	NetworkedUnit();
	virtual ~NetworkedUnit();

public: // ---------------------------------------------------------------- APIs
	/// Call this function to start receiving events on peer connection / disconnection.
	void ns_start_listening_peer_connection();
	/// Call this function to stop receiving events on peer connection / disconnection.
	void ns_stop_listening_peer_connection();

	/// Fetch the current client peer_id
	int ns_fetch_local_peer_id() const;

	/// Fetch the list with all the connected peers.
	Vector<int> ns_fetch_connected_peers() const;

	/// Get the peer id controlling this unit.
	int ns_get_unit_authority() const;

	/// Can be used to verify if the local peer is connected to a server.
	bool ns_is_local_peer_networked() const;
	/// Can be used to verify if the local peer is the server.
	bool ns_is_local_peer_server() const;
	/// Can be used to verify if the local peer is the authority of this unit.
	bool ns_is_local_peer_authority_of_this_unit() const;

	/// Configures the rpc call.
	void ns_configure_rpc(
			const StringName &p_func,
			bool p_call_local,
			bool p_is_reliable);

	/// Returns the peer that remotelly called the currently executed rpc function.
	/// Should be called always from an rpc function.
	int ns_rpc_get_sender() const;

	/// Calls an rpc.
	template <typename... VarArgs>
	void ns_rpc(int p_peer_id, const StringName &p_method, VarArgs... p_args);

private:
	/// This is just for internal usage.
	void ns_rpcp(
			int p_peer_id,
			const StringName &p_method,
			const Variant **p_arg,
			int p_argcount);

public: // -------------------------------------------------------------- Events
	/// Emitted when a new peers connects.
	virtual void on_peer_connected(int p_peer) {}
	/// Emitted when a peers disconnects.
	virtual void on_peer_disconnected(int p_peer) {}
};

template <typename... VarArgs>
void NetworkedUnit::ns_rpc(int p_peer_id, const StringName &p_method, VarArgs... p_args) {
	Variant args[sizeof...(p_args) + 1] = { p_args..., Variant() }; // +1 makes sure zero sized arrays are also supported.
	const Variant *argptrs[sizeof...(p_args) + 1];
	for (uint32_t i = 0; i < sizeof...(p_args); i++) {
		argptrs[i] = &args[i];
	}

	ns_rpcp(
			p_peer_id,
			p_method,
			sizeof...(p_args) == 0 ? nullptr : (const Variant **)argptrs,
			sizeof...(p_args));
}
