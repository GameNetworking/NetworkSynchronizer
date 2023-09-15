#pragma once

#include "core/object/object.h"
#include "modules/network_synchronizer/core/network_interface.h"

class GdNetworkInterface : public NS::NetworkInterface, public Object {
public:
	class Node *owner = nullptr;

public:
	GdNetworkInterface();
	virtual ~GdNetworkInterface();

public: // ---------------------------------------------------------------- APIs
	/// Call this function to start receiving events on peer connection / disconnection.
	virtual void start_listening_peer_connection(
			std::function<void(int /*p_peer*/)> p_on_peer_connected_callback,
			std::function<void(int /*p_peer*/)> p_on_peer_disconnected_callback) override;

	/// Emitted when a new peers connects.
	void on_peer_connected(int p_peer);
	static void __on_peer_connected(GdNetworkInterface *p_ni, int p_peer);

	/// Emitted when a peers disconnects.
	void on_peer_disconnected(int p_peer);

	/// Call this function to stop receiving events on peer connection / disconnection.
	virtual void stop_listening_peer_connection() override;

	/// Fetch the current client peer_id
	virtual int fetch_local_peer_id() const override;

	/// Fetch the list with all the connected peers.
	virtual Vector<int> fetch_connected_peers() const override;

	/// Get the peer id controlling this unit.
	virtual int get_unit_authority() const override;

	/// Can be used to verify if the local peer is connected to a server.
	virtual bool is_local_peer_networked() const override;

	/// Can be used to verify if the local peer is the server.
	virtual bool is_local_peer_server() const override;

	/// Can be used to verify if the local peer is the authority of this unit.
	virtual bool is_local_peer_authority_of_this_unit() const override;

	/// Configures the rpc call.
	virtual void configure_rpc(
			const StringName &p_func,
			bool p_call_local,
			bool p_is_reliable) override;

	/// Returns the peer that remotelly called the currently executed rpc function.
	/// Should be called always from an rpc function.
	virtual int rpc_get_sender() const override;

protected:
	/// This is just for internal usage.
	virtual void rpc_array(
			int p_peer_id,
			const StringName &p_method,
			const Variant **p_arg,
			int p_argcount) override;
};