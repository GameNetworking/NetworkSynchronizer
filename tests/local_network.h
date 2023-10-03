#pragma once

#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/core/network_interface.h"
#include "modules/network_synchronizer/core/processor.h"
#include "modules/network_synchronizer/data_buffer.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

NS_NAMESPACE_BEGIN
class LocalNetworkInterface;

struct LocalNetworkProps {
	// Ping in seconds.
	float rtt_seconds = 0.0;

	// From 0.0 to 1.0
	float reorder = 0.0;

	// From 0.0 to 1.0
	float packet_loss = 0.0;
};

struct PendingPacket {
	// -1 means unreliable
	int reliable_packet_index = -1;
	float delay = 0.0;
	int peer_recipient = -1;
	String object_name;
	DataBuffer data_buffer;
};

class LocalNetwork {
	bool is_server = false;
	int this_peer = 0;

	int peer_counter = 2;
	std::map<int, LocalNetwork *> connected_peers;

	std::map<String, LocalNetworkInterface *> registered_objects;

	std::vector<std::shared_ptr<PendingPacket>> sending_packets;

public:
	LocalNetworkProps *network_properties = nullptr;

	NS::Processor<int> connected_event;
	NS::Processor<int> disconnected_event;

public:
	int get_peer() const;

	const std::map<int, LocalNetwork *> &get_connected_peers() const;
	void start_as_server();

	void start_as_client(LocalNetwork &p_server_network);

	void register_object(LocalNetworkInterface &p_interface);

	void rpc_send(String p_object_name, int p_peer_recipient, bool p_reliable, DataBuffer &&p_data_buffer);

	void process(float p_delta);

private:
	void rpc_send_internal(const std::shared_ptr<PendingPacket> &p_packet);
	void rpc_receive_internal(int p_peer_sender, const std::shared_ptr<PendingPacket> &p_packet);
};

class LocalNetworkInterface : public NS::NetworkInterface {
	std::string name;
	LocalNetwork *network = nullptr;

public:
	int authoritative_peer_id = 0;
	NS::PHandler processor_handler_connected = NS::NullPHandler;
	NS::PHandler processor_handler_disconnected = NS::NullPHandler;

	void init(LocalNetwork &p_network, const std::string &p_unique_name, int p_authoritative_peer);

	std::vector<RPCInfo> &get_rpcs_info() { return rpcs_info; }
	virtual String get_name() const override { return String(name.c_str()); }

	virtual int get_server_peer() const override { return 1; }

	/// Call this function to start receiving events on peer connection / disconnection.
	virtual void start_listening_peer_connection(
			std::function<void(int /*p_peer*/)> p_on_peer_connected_callback,
			std::function<void(int /*p_peer*/)> p_on_peer_disconnected_callback) override;

	/// Call this function to stop receiving events on peer connection / disconnection.
	virtual void stop_listening_peer_connection() override;

	/// Fetch the current client peer_id
	virtual int fetch_local_peer_id() const override;

	/// Fetch the list with all the connected peers.
	virtual void fetch_connected_peers(std::vector<int> &p_connected_peers) const override;

	/// Get the peer id controlling this unit.
	virtual int get_unit_authority() const override;

	/// Can be used to verify if the local peer is connected to a server.
	virtual bool is_local_peer_networked() const override;

	/// Can be used to verify if the local peer is the server.
	virtual bool is_local_peer_server() const override;

	virtual void encode(DataBuffer &r_buffer, const NS::VarData &p_val) const override;
	virtual void decode(NS::VarData &r_val, DataBuffer &p_buffer) const override;

	virtual void rpc_send(int p_peer_recipient, bool p_reliable, DataBuffer &&p_data_buffer) override;
};

NS_NAMESPACE_END

namespace NS_Test {
void test_local_network();
};
