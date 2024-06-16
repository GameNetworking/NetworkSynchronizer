#pragma once

#include "core/object/object.h"
#include "modules/network_synchronizer/core/network_interface.h"

#define FROM_GSTRING(str) std::string(str.utf8().ptr())

class GdNetworkInterface final : public NS::NetworkInterface,
								 public Object {
public:
	class Node *owner = nullptr;
	std::function<void(int /*p_peer*/)> on_peer_connected_callback;
	std::function<void(int /*p_peer*/)> on_peer_disconnected_callback;

public:
	GdNetworkInterface();
	virtual ~GdNetworkInterface();

public: // ---------------------------------------------------------------- APIs
	virtual std::string get_owner_name() const override;

	virtual int get_server_peer() const override { return 1; }

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
	virtual int get_local_peer_id() const override;

	/// Fetch the list with all the connected peers.
	virtual void fetch_connected_peers(std::vector<int> &p_connected_peers) const override;

	/// Get the peer id controlling this unit.
	virtual int get_unit_authority() const override;

	/// Can be used to verify if the local peer is connected to a server.
	virtual bool is_local_peer_networked() const override;

	/// Can be used to verify if the local peer is the server.
	virtual bool is_local_peer_server() const override;

	/// Can be used to verify if the local peer is the authority of this unit.
	virtual bool is_local_peer_authority_of_this_unit() const override;

	virtual void rpc_send(int p_peer_recipient, bool p_reliable, NS::DataBuffer &&p_buffer) override;
	void gd_rpc_receive(const Vector<uint8_t> &p_args);

	virtual void server_update_net_stats(int p_peer, NS::PeerData &r_peer_data) const override;
};

namespace NS_GD_Test {
void test_var_data_conversin();
};
