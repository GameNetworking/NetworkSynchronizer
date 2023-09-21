
#include "core/error/error_macros.h"
#include "modules/network_synchronizer/core/network_interface.h"
#include "modules/network_synchronizer/core/processor.h"
#include "modules/network_synchronizer/networked_controller.h"
#include "modules/network_synchronizer/scene_synchronizer.h"
#include <functional>
#include <map>
#include <vector>

namespace NS_Test {
class LocalNetwork {
	bool is_server = false;
	int this_peer = 0;

	int peer_counter = 2;
	std::map<int, LocalNetwork *> connected_peers;

public:
	NS::Processor<int> connected_event;
	NS::Processor<int> disconnected_event;

public:
	int get_peer() const {
		return this_peer;
	}

	const std::map<int, LocalNetwork *> &get_connected_peers() const {
		return connected_peers;
	}

	void start_as_server() {
		is_server = true;
		this_peer = 1;
	}

	void start_as_client(LocalNetwork &p_server_network) {
		CRASH_COND(!p_server_network.is_server);
		is_server = false;

		const int peer = p_server_network.peer_counter;
		p_server_network.peer_counter += 1;

		p_server_network.connected_peers[peer] = this;
		connected_peers[1] = this;

		p_server_network.connected_event.broadcast(peer);
		connected_event.broadcast(1);
	}

	/*
		template <typename... ARGS>
		void rpc(uint8_t p_rpc_id, int p_peer, ARGS... p_args) {
		}

		void rpc_send(uint8_t p_rpc_id, int p_sender_peer, const std::vector<Variant> &p_args) {}

		void rpc_receive(uint8_t p_rpc_id, int p_sender_peer, const std::vector<Variant> p_args) {
			ERR_FAIL_COND_MSG(funcs.size() <= p_rpc_id, "The received rpc `" + itos(p_rpc_id) + "` doesn't exists.");
			rpc_last_sender = p_sender_peer;
			funcs[p_rpc_id](p_args);

		}
		*/
};

class LocalNetworkInterface : public NS::NetworkInterface {
public:
	String name;
	LocalNetwork *network = nullptr;
	int authoritative_peer_id = 0;

	NS::PHandler processor_handler_connected = NS::NullPHandler;
	NS::PHandler processor_handler_disconnected = NS::NullPHandler;

	virtual String get_name() const override { return name; }

	virtual int get_server_peer() const override { return 1; }

	/// Call this function to start receiving events on peer connection / disconnection.
	virtual void start_listening_peer_connection(
			std::function<void(int /*p_peer*/)> p_on_peer_connected_callback,
			std::function<void(int /*p_peer*/)> p_on_peer_disconnected_callback) override {
		processor_handler_connected =
				network->connected_event.bind(p_on_peer_connected_callback);

		processor_handler_disconnected =
				network->disconnected_event.bind(p_on_peer_disconnected_callback);
	}

	/// Call this function to stop receiving events on peer connection / disconnection.
	virtual void stop_listening_peer_connection() override {
		network->connected_event.unbind(processor_handler_connected);
		network->disconnected_event.unbind(processor_handler_disconnected);
		processor_handler_connected = NS::NullPHandler;
		processor_handler_disconnected = NS::NullPHandler;
	}

	/// Fetch the current client peer_id
	virtual int fetch_local_peer_id() const override {
		return network->get_peer();
	}

	/// Fetch the list with all the connected peers.
	virtual Vector<int> fetch_connected_peers() const override {
		Vector<int> peers;
		for (const auto &[peer_id, _] : network->get_connected_peers()) {
			peers.push_back(peer_id);
		}
		return peers;
	}

	/// Get the peer id controlling this unit.
	virtual int get_unit_authority() const override {
		return authoritative_peer_id;
	}

	/// Can be used to verify if the local peer is connected to a server.
	virtual bool is_local_peer_networked() const override {
		return network->get_peer() != 0;
	}

	/// Can be used to verify if the local peer is the server.
	virtual bool is_local_peer_server() const override {
		return network->get_peer() == 1;
	}
};

class LocalSceneSynchronizer : public NS::SceneSynchronizer, public NS::SynchronizerManager {
public:
	virtual Node *get_node_or_null(const NodePath &p_path) override {
	}

	virtual NS::NetworkedController *extract_network_controller(Node *p_node) const override {
	}

	virtual const NS::NetworkedController *extract_network_controller(const Node *p_node) const override {
	}

	virtual void rpc_send__state(int p_peer, const Variant &p_snapshot) override {
	}

	virtual void rpc_send__notify_need_full_snapshot(int p_peer) override {
	}

	virtual void rpc_send__set_network_enabled(int p_peer, bool p_enabled) override {
	}

	virtual void rpc_send__notify_peer_status(int p_peer, bool p_enabled) override {
	}

	virtual void rpc_send__deferred_sync_data(int p_peer, const Vector<uint8_t> &p_data) override {
	}
};

void test_scene_processing() {
	//LocalNetwork network;
	//network.start_as_server();
	//network.start_as_client(LocalNetwork & p_server_network)
}

void test_scene_synchronizer() {
	test_scene_processing();
}
}; //namespace NS_Test
