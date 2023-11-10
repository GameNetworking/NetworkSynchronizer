
#include "local_network.h"

#include "core/error/error_macros.h"
#include "core/math/vector3.h"
#include "modules/network_synchronizer/core/network_interface.h"
#include "modules/network_synchronizer/core/processor.h"
#include "modules/network_synchronizer/core/var_data.h"
#include "modules/network_synchronizer/networked_controller.h"
#include "modules/network_synchronizer/scene_synchronizer.h"
#include <functional>
#include <map>
#include <memory>
#include <vector>

NS_NAMESPACE_BEGIN
float frand() {
	return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
}

int LocalNetwork::get_peer() const {
	return this_peer;
}

const std::map<int, LocalNetwork *> &LocalNetwork::get_connected_peers() const {
	return connected_peers;
}

void LocalNetwork::start_as_server() {
	is_server = true;
	this_peer = 1;
}

void LocalNetwork::start_as_client(LocalNetwork &p_server_network) {
	CRASH_COND(!p_server_network.is_server);
	is_server = false;

	const int peer = p_server_network.peer_counter;
	this_peer = peer;
	p_server_network.peer_counter += 1;

	p_server_network.connected_peers[peer] = this;
	connected_peers[1] = &p_server_network;

	p_server_network.connected_event.broadcast(peer);
	connected_event.broadcast(1);
}

void LocalNetwork::register_object(LocalNetworkInterface &p_interface) {
	CRASH_COND(registered_objects.find(p_interface.get_name()) != registered_objects.end());
	registered_objects.insert(std::make_pair(p_interface.get_name(), &p_interface));
}

void LocalNetwork::rpc_send(String p_object_name, int p_peer_recipient, bool p_reliable, DataBuffer &&p_data_buffer) {
	auto object_map_it = registered_objects.find(p_object_name);
	CRASH_COND(object_map_it == registered_objects.end());

	LocalNetworkInterface *object_net_interface = object_map_it->second;
	CRASH_COND(object_net_interface == nullptr);

	if (!p_reliable && network_properties && network_properties->packet_loss > frand()) {
		// Simulating packet loss by dropping this packet right away.
		return;
	}

	std::shared_ptr<PendingPacket> packet = std::make_shared<PendingPacket>();

	if (network_properties) {
		packet->delay = network_properties->rtt_seconds;
		if (!p_reliable && network_properties->reorder > frand()) {
			const float reorder_delay = 0.5;
			packet->delay += reorder_delay * ((frand() - 0.5) / 0.5);
		}
	} else {
		packet->delay = 0.0;
	}

	packet->peer_recipient = p_peer_recipient;
	packet->object_name = p_object_name;
	// packet->data_buffer = std::move(p_data_buffer); // TODO use move here.
	packet->data_buffer.copy(std::move(p_data_buffer));

	sending_packets.push_back(packet);
}

void LocalNetwork::process(float p_delta) {
	// Process send logic
	std::vector<std::shared_ptr<PendingPacket>> packets;
	for (auto p : sending_packets) {
		p->delay -= p_delta;
		if (p->delay <= 0.0) {
			// send
			rpc_send_internal(p);
		} else {
			packets.push_back(p);
		}
	}
	sending_packets = packets;
}

void LocalNetwork::rpc_send_internal(const std::shared_ptr<PendingPacket> &p_packet) {
	auto recipient = connected_peers.find(p_packet->peer_recipient);
	CRASH_COND(recipient == connected_peers.end());

	auto object_map_it = registered_objects.find(p_packet->object_name);
	CRASH_COND(object_map_it == registered_objects.end());

	LocalNetworkInterface *object_net_interface = object_map_it->second;
	CRASH_COND(object_net_interface == nullptr);

	recipient->second->rpc_receive_internal(this_peer, p_packet);
}

void LocalNetwork::rpc_receive_internal(int p_peer_sender, const std::shared_ptr<PendingPacket> &p_packet) {
	auto object_map_it = registered_objects.find(p_packet->object_name);
	CRASH_COND(object_map_it == registered_objects.end());

	LocalNetworkInterface *object_net_interface = object_map_it->second;
	CRASH_COND(object_net_interface == nullptr);

	object_net_interface->rpc_receive(
			p_peer_sender,
			p_packet->data_buffer);
}

void LocalNetworkInterface::init(LocalNetwork &p_network, const std::string &p_unique_name, int p_authoritative_peer) {
	network = &p_network;
	name = p_unique_name;
	authoritative_peer_id = p_authoritative_peer;
	network->register_object(*this);
}

void LocalNetworkInterface::start_listening_peer_connection(
		std::function<void(int /*p_peer*/)> p_on_peer_connected_callback,
		std::function<void(int /*p_peer*/)> p_on_peer_disconnected_callback) {
	processor_handler_connected =
			network->connected_event.bind(p_on_peer_connected_callback);

	processor_handler_disconnected =
			network->disconnected_event.bind(p_on_peer_disconnected_callback);
}

void LocalNetworkInterface::stop_listening_peer_connection() {
	network->connected_event.unbind(processor_handler_connected);
	network->disconnected_event.unbind(processor_handler_disconnected);
	processor_handler_connected = NS::NullPHandler;
	processor_handler_disconnected = NS::NullPHandler;
}

int LocalNetworkInterface::fetch_local_peer_id() const {
	return network->get_peer();
}

void LocalNetworkInterface::fetch_connected_peers(std::vector<int> &p_connected_peers) const {
	p_connected_peers.clear();
	for (const auto &[peer_id, _] : network->get_connected_peers()) {
		p_connected_peers.push_back(peer_id);
	}
}

int LocalNetworkInterface::get_unit_authority() const {
	return authoritative_peer_id;
}

bool LocalNetworkInterface::is_local_peer_networked() const {
	return network->get_peer() != 0;
}

bool LocalNetworkInterface::is_local_peer_server() const {
	return network->get_peer() == 1;
}

void LocalNetworkInterface::rpc_send(int p_peer_recipient, bool p_reliable, DataBuffer &&p_data_buffer) {
	ERR_FAIL_COND(!network);
	network->rpc_send(get_name(), p_peer_recipient, p_reliable, std::move(p_data_buffer));
}

NS_NAMESPACE_END

/// Test that the LocalNetwork is able to sync stuff.
void NS_Test::test_local_network() {
	NS::LocalNetworkProps network_properties;

	NS::LocalNetwork server;
	server.network_properties = &network_properties;

	NS::LocalNetwork peer_1;
	peer_1.network_properties = &network_properties;

	NS::LocalNetwork peer_2;
	peer_2.network_properties = &network_properties;

	NS::LocalNetworkInterface server_obj_1;
	server_obj_1.init(server, "object_1", 0);

	std::vector<int> server_rpc_executed_by;
	const auto rpc_handle_server = server_obj_1.rpc_config(
			std::function<void(bool, int, float, const NS::VarData &, const Vector<uint8_t> &)>([&server_rpc_executed_by, &server_obj_1](bool a, int b, float c, const NS::VarData &d, const Vector<uint8_t> &e) {
				server_rpc_executed_by.push_back(server_obj_1.rpc_get_sender());
				CRASH_COND(a != true);
				CRASH_COND(b != 22);
				CRASH_COND(c != 44.0);
				CRASH_COND(Vector3(d.data.vec.x, d.data.vec.y, d.data.vec.z).distance_to(Vector3(1, 2, 3)) > 0.001);
				CRASH_COND(e.size() != 3);
				CRASH_COND(e[0] != 1);
				CRASH_COND(e[1] != 2);
				CRASH_COND(e[2] != 3);
			}),
			false,
			false);

	NS::LocalNetworkInterface peer_1_obj_1;
	peer_1_obj_1.init(peer_1, "object_1", 0);

	std::vector<int> peer_1_rpc_executed_by;
	const auto rpc_handle_1_obj_1 = peer_1_obj_1.rpc_config(
			std::function<void(bool, int, float, const NS::VarData &, const Vector<uint8_t> &)>([&peer_1_rpc_executed_by, &peer_1_obj_1](bool a, int b, float c, const NS::VarData &d, const Vector<uint8_t> &e) {
				peer_1_rpc_executed_by.push_back(peer_1_obj_1.rpc_get_sender());
				CRASH_COND(a != true);
				CRASH_COND(b != 22);
				CRASH_COND(c != 44.0);
				CRASH_COND(Vector3(d.data.vec.x, d.data.vec.y, d.data.vec.z).distance_to(Vector3(1, 2, 3)) > 0.001);
				CRASH_COND(e.size() != 3);
				CRASH_COND(e[0] != 1);
				CRASH_COND(e[1] != 2);
				CRASH_COND(e[2] != 3);
			}),
			false,
			false);

	NS::LocalNetworkInterface peer_2_obj_1;
	peer_2_obj_1.init(peer_2, "object_1", 0);

	std::vector<int> peer_2_rpc_executed_by;
	std::vector<int> peer_2_rpc_b_values_by_exec_order;
	const auto rpc_handle_2_obj_1 = peer_2_obj_1.rpc_config(
			std::function<void(bool, int, float, const NS::VarData &, const Vector<uint8_t> &)>([&peer_2_rpc_executed_by, &peer_2_obj_1, &peer_2_rpc_b_values_by_exec_order](bool a, int b, float c, const NS::VarData &d, const Vector<uint8_t> &e) {
				peer_2_rpc_executed_by.push_back(peer_2_obj_1.rpc_get_sender());
				peer_2_rpc_b_values_by_exec_order.push_back(b);
			}),
			false,
			false);

	CRASH_COND(rpc_handle_server.get_index() != rpc_handle_1_obj_1.get_index());
	CRASH_COND(rpc_handle_2_obj_1.get_index() != rpc_handle_1_obj_1.get_index());

	std::vector<int> server_connection_event;
	server_obj_1.start_listening_peer_connection(
			[&server_connection_event](int p_peer) {
				server_connection_event.push_back(p_peer);
			},
			[](int p_peer) {
			});

	std::vector<int> peer_1_connection_event;
	peer_1_obj_1.start_listening_peer_connection(
			[&peer_1_connection_event](int p_peer) {
				peer_1_connection_event.push_back(p_peer);
			},
			[](int p_peer) {
			});

	std::vector<int> peer_2_connection_event;
	peer_2_obj_1.start_listening_peer_connection(
			[&peer_2_connection_event](int p_peer) {
				peer_2_connection_event.push_back(p_peer);
			},
			[](int p_peer) {
			});

	server.start_as_server();
	peer_1.start_as_client(server);
	peer_2.start_as_client(server);
	CRASH_COND(server.get_peer() != 1);
	CRASH_COND(peer_1.get_peer() == server.get_peer());
	CRASH_COND(peer_2.get_peer() == server.get_peer());
	CRASH_COND(peer_1.get_peer() == peer_2.get_peer());
	CRASH_COND(peer_1.get_peer() == 0);
	CRASH_COND(peer_2.get_peer() == 0);

	// Check the events were executed.
	CRASH_COND(server_connection_event[0] != peer_1.get_peer());
	CRASH_COND(server_connection_event[1] != peer_2.get_peer());
	CRASH_COND(peer_1_connection_event[0] != server.get_peer());
	CRASH_COND(peer_2_connection_event[0] != server.get_peer());

	// Check the connected peers list is valid
	std::vector<int> connected_peers;
	server_obj_1.fetch_connected_peers(connected_peers);
	CRASH_COND(std::find(connected_peers.begin(), connected_peers.end(), peer_1.get_peer()) == connected_peers.end());
	CRASH_COND(std::find(connected_peers.begin(), connected_peers.end(), peer_2.get_peer()) == connected_peers.end());
	peer_1_obj_1.fetch_connected_peers(connected_peers);
	CRASH_COND(std::find(connected_peers.begin(), connected_peers.end(), server.get_peer()) == connected_peers.end());
	peer_2_obj_1.fetch_connected_peers(connected_peers);
	CRASH_COND(std::find(connected_peers.begin(), connected_peers.end(), server.get_peer()) == connected_peers.end());

	Vector<uint8_t> vec;
	vec.push_back(1);
	vec.push_back(2);
	vec.push_back(3);

	rpc_handle_server.rpc(peer_1_obj_1, peer_1_obj_1.get_server_peer(), true, 22, 44.0, NS::VarData(1, 2, 3), vec);

	// Make sure the rpc are not yet received.
	CRASH_COND(!server_rpc_executed_by.empty());

	const float delta = 1.0 / 60.0;
	server.process(delta);
	peer_1.process(delta);
	peer_2.process(delta);

	// Make sure the rpc was delivered after `process`.
	CRASH_COND(server_rpc_executed_by[0] != peer_1.get_peer());
	CRASH_COND(!peer_1_rpc_executed_by.empty());
	CRASH_COND(!peer_2_rpc_executed_by.empty());

	// -------------------------------------------------------Now test `latency`
	network_properties.rtt_seconds = 1.0;
	rpc_handle_2_obj_1.rpc(peer_2_obj_1, peer_2_obj_1.get_server_peer(), true, 22, 44.0, NS::VarData(1, 2, 3), vec);

	CRASH_COND(server_rpc_executed_by.size() != 1);

	// Process for less than 1 sec and make sure the rpc is never delivered.
	for (float t = 0.0; t < (1.0 - delta - 0.001); t += delta) {
		server.process(delta);
		peer_1.process(delta);
		peer_2.process(delta);

		// Make sure nothing is deliveted at this point.
		CRASH_COND(server_rpc_executed_by.size() != 1);
	}

	// Process twice and make sure the RPC was delivered.
	for (int i = 0; i < 2; i++) {
		server.process(delta);
		peer_1.process(delta);
		peer_2.process(delta);
	}

	CRASH_COND(server_rpc_executed_by.size() != 2);
	CRASH_COND(server_rpc_executed_by[1] != peer_2.get_peer());

	// ------------------------------- Test packet loss with unreliable packets.
	network_properties.rtt_seconds = 0.0;
	network_properties.packet_loss = 1.0; // 100% packet loss

	rpc_handle_server.rpc(server_obj_1, peer_1.get_peer(), true, 22, 44.0, NS::VarData(1, 2, 3), vec);
	for (float t = 0.0; t < 2.0; t += delta) {
		server.process(delta);
		peer_1.process(delta);
		peer_2.process(delta);

		CRASH_COND(server_rpc_executed_by[0] != peer_1.get_peer());
		CRASH_COND(server_rpc_executed_by[1] != peer_2.get_peer());
		CRASH_COND(!peer_1_rpc_executed_by.empty());
		CRASH_COND(!peer_2_rpc_executed_by.empty());
	}

	// --------------------------------- Test packet loss with reliable packets.
	server_obj_1.get_rpcs_info()[0].is_reliable = true;
	peer_1_obj_1.get_rpcs_info()[0].is_reliable = true;
	peer_2_obj_1.get_rpcs_info()[0].is_reliable = true;

	rpc_handle_server.rpc(server_obj_1, peer_1.get_peer(), true, 22, 44.0, NS::VarData(1, 2, 3), vec);
	server.process(delta);
	peer_1.process(delta);
	peer_2.process(delta);

	CRASH_COND(peer_1_rpc_executed_by.empty());
	CRASH_COND(peer_1_rpc_executed_by[0] != peer_1_obj_1.get_server_peer());

	// ----------------------------------- Test reliable packet doesn't reorder.
	network_properties.rtt_seconds = 0.0;
	network_properties.packet_loss = 1.0;
	network_properties.reorder = 1.0; // 100% reorder

	rpc_handle_server.rpc(server_obj_1, peer_2.get_peer(), true, 1, 44.0, NS::VarData(1, 2, 3), vec);
	rpc_handle_server.rpc(server_obj_1, peer_2.get_peer(), true, 2, 44.0, NS::VarData(1, 2, 3), vec);
	rpc_handle_server.rpc(server_obj_1, peer_2.get_peer(), true, 3, 44.0, NS::VarData(1, 2, 3), vec);

	server.process(delta);
	peer_1.process(delta);
	peer_2.process(delta);

	CRASH_COND(peer_2_rpc_executed_by.size() != 3);
	CRASH_COND(peer_2_rpc_executed_by[0] != server.get_peer());
	CRASH_COND(peer_2_rpc_executed_by[1] != server.get_peer());
	CRASH_COND(peer_2_rpc_executed_by[2] != server.get_peer());

	CRASH_COND(peer_2_rpc_b_values_by_exec_order[0] != 1);
	CRASH_COND(peer_2_rpc_b_values_by_exec_order[1] != 2);
	CRASH_COND(peer_2_rpc_b_values_by_exec_order[2] != 3);

	// -------------------------------------------------------- Test call local.
	server_obj_1.get_rpcs_info()[0].call_local = true;
	peer_1_obj_1.get_rpcs_info()[0].call_local = true;
	peer_2_obj_1.get_rpcs_info()[0].call_local = true;
	network_properties.rtt_seconds = 0.0;
	network_properties.packet_loss = 0.0;
	network_properties.reorder = 0.0;

	rpc_handle_server.rpc(server_obj_1, peer_2.get_peer(), true, 22, 44.0, NS::VarData(1, 2, 3), vec);

	server.process(delta);
	peer_1.process(delta);
	peer_2.process(delta);

	CRASH_COND(server_rpc_executed_by[2] != server.get_peer()); // Make sure this was executed locally too.
	CRASH_COND(peer_2_rpc_executed_by[3] != server.get_peer()); // Make sure this was executed remotely.
}
