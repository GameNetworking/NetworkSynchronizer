#include "test_doll_simulation.h"

#include "core/error/error_macros.h"
#include "core/math/vector3.h"
#include "core/variant/variant.h"
#include "local_scene.h"
#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/core/processor.h"
#include "modules/network_synchronizer/core/var_data.h"
#include "modules/network_synchronizer/data_buffer.h"
#include "modules/network_synchronizer/net_utilities.h"
#include "modules/network_synchronizer/tests/local_network.h"
#include "modules/network_synchronizer/tests/local_scene.h"
#include "modules/network_synchronizer/tests/test_math_lib.h"
#include "test_math_lib.h"
#include <chrono>
#include <string>
#include <thread>

namespace NS_Test {

const float delta = 1.0 / 60.0;

class TDSLocalNetworkedController : public NS::NetworkedController<NS::LocalNetworkInterface>, public NS::NetworkedControllerManager, public NS::LocalSceneObject {
public:
	NS::ObjectLocalId local_id = NS::ObjectLocalId::NONE;

	TDSLocalNetworkedController() = default;

	virtual void on_scene_entry() override {
		// Setup the NetworkInterface.
		get_network_interface().init(get_scene()->get_network(), name, authoritative_peer_id);
		setup(*this);

		set_xi(0);

		get_scene()->scene_sync->register_app_object(get_scene()->scene_sync->to_handle(this));
	}

	virtual void on_scene_exit() override {
		get_scene()->scene_sync->unregister_app_object(local_id);
	}

	virtual void setup_synchronizer(NS::LocalSceneSynchronizer &p_scene_sync, NS::ObjectLocalId p_id) override {
		local_id = p_id;
		p_scene_sync.register_variable(p_id, "xi");
	}

	void set_xi(int p_xi) {
		NS::VarData vd;
		vd.data.i32 = 1;
		vd.type = 0;
		NS::MapFunc::assign(variables, std::string("xi"), std::move(vd));
	}

	int get_xi() const {
		const NS::VarData *vd = NS::MapFunc::get_or_null(variables, std::string("xi"));
		if (vd) {
			return vd->data.i32;
		} else {
			return 0;
		}
	}

	// ------------------------------------------------- NetController interface
	virtual void collect_inputs(double p_delta, DataBuffer &r_buffer) override {
		r_buffer.add(true);
	}

	virtual void controller_process(double p_delta, DataBuffer &p_buffer) override {
		bool advance_xi;
		p_buffer.read(advance_xi);
		if (advance_xi) {
			set_xi(get_xi() + 1);
		}
	}

	virtual bool are_inputs_different(DataBuffer &p_buffer_A, DataBuffer &p_buffer_B) override {
		const bool v1 = p_buffer_A.read_bool();
		const bool v2 = p_buffer_B.read_bool();
		return v1 != v2;
	}

	virtual uint32_t count_input_size(DataBuffer &p_buffer) override {
		return p_buffer.get_bool_size();
	}
};

/// This class is responsible to verify the doll simulation.
/// This class is made in a way which allows to be overriden to test the sync
/// still works under bad network conditions.
struct TestDollSimulationBase {
	NS::LocalNetworkProps network_properties;

	NS::LocalScene server_scene;
	NS::LocalScene peer_1_scene;
	NS::LocalScene peer_2_scene;
	TDSLocalNetworkedController *controller_1_serv = nullptr;
	TDSLocalNetworkedController *controller_1_peer1 = nullptr;
	TDSLocalNetworkedController *controller_1_peer2 = nullptr;

	TDSLocalNetworkedController *controller_2_serv = nullptr;
	TDSLocalNetworkedController *controller_2_peer1 = nullptr;
	TDSLocalNetworkedController *controller_2_peer2 = nullptr;

private:
	virtual void on_scenes_initialized() {}
	virtual void on_server_process(float p_delta) {}
	virtual void on_client_1_process(float p_delta) {}
	virtual void on_client_2_process(float p_delta) {}
	virtual void on_scenes_processed(float p_delta) {}

public:
	TestDollSimulationBase() {}
	virtual ~TestDollSimulationBase() {}

	void init_test() {
		server_scene.get_network().network_properties = &network_properties;
		peer_1_scene.get_network().network_properties = &network_properties;
		peer_2_scene.get_network().network_properties = &network_properties;

		// Create a server
		server_scene.start_as_server();

		// and a client connected to the server.
		peer_1_scene.start_as_client(server_scene);

		// and a client connected to the server.
		peer_2_scene.start_as_client(server_scene);

		// Add the scene sync
		server_scene.scene_sync =
				server_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
		peer_1_scene.scene_sync =
				peer_1_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
		peer_2_scene.scene_sync =
				peer_2_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());

		// Then compose the scene: 2 controllers.
		controller_1_serv = server_scene.add_object<TDSLocalNetworkedController>("controller_1", peer_1_scene.get_peer());
		controller_1_peer1 = peer_1_scene.add_object<TDSLocalNetworkedController>("controller_1", peer_1_scene.get_peer());
		controller_1_peer2 = peer_2_scene.add_object<TDSLocalNetworkedController>("controller_1", peer_1_scene.get_peer());

		controller_2_serv = server_scene.add_object<TDSLocalNetworkedController>("controller_2", peer_2_scene.get_peer());
		controller_2_peer1 = peer_1_scene.add_object<TDSLocalNetworkedController>("controller_2", peer_2_scene.get_peer());
		controller_2_peer2 = peer_2_scene.add_object<TDSLocalNetworkedController>("controller_2", peer_2_scene.get_peer());

		server_scene.scene_sync->register_process(server_scene.scene_sync->find_local_id(), PROCESSPHASE_LATE, [=](float p_delta) -> void {
			on_server_process(p_delta);
		});
		peer_1_scene.scene_sync->register_process(peer_1_scene.scene_sync->find_local_id(), PROCESSPHASE_LATE, [=](float p_delta) -> void {
			on_client_1_process(p_delta);
		});
		peer_2_scene.scene_sync->register_process(peer_2_scene.scene_sync->find_local_id(), PROCESSPHASE_LATE, [=](float p_delta) -> void {
			on_client_2_process(p_delta);
		});

		on_scenes_initialized();

		// Set the position of each object:
		controller_1_serv->set_xi(100);
		controller_1_peer1->set_xi(100);
		controller_1_peer2->set_xi(100);

		controller_2_serv->set_xi(0);
		controller_2_peer1->set_xi(0);
		controller_2_peer2->set_xi(0);
	}

	void do_test(const int p_frames_count, bool p_wait_for_time_pass = false) {
		for (int i = 0; i < p_frames_count; i++) {
			server_scene.process(delta);
			peer_1_scene.process(delta);
			peer_2_scene.process(delta);

			on_scenes_processed(delta);
			if (p_wait_for_time_pass) {
				const int ms = delta * 1000.0;
				std::this_thread::sleep_for(std::chrono::milliseconds(ms));
			}
		}
	}
};

void test_ping() {
	TestDollSimulationBase test;
	test.init_test();

	test.server_scene.scene_sync->set_frame_confirmation_timespan(0.0);
	test.server_scene.scene_sync->set_ping_update_rate(0.05);

	const int peer1 = test.peer_1_scene.get_peer();
	const int peer2 = test.peer_2_scene.get_peer();

	// TEST 1 with 0 PING
	test.network_properties.rtt_seconds = 0.;

	test.do_test(10, true);

	// Make sure the ping is the same between client and the server.
	CRASH_COND(test.server_scene.scene_sync->get_peers()[peer1].get_ping() != test.peer_1_scene.scene_sync->get_peers()[peer1].get_ping());
	CRASH_COND(test.server_scene.scene_sync->get_peers()[peer2].get_ping() != test.peer_1_scene.scene_sync->get_peers()[peer2].get_ping());
	CRASH_COND(test.server_scene.scene_sync->get_peers()[peer1].get_ping() != test.peer_2_scene.scene_sync->get_peers()[peer1].get_ping());
	CRASH_COND(test.server_scene.scene_sync->get_peers()[peer2].get_ping() != test.peer_2_scene.scene_sync->get_peers()[peer2].get_ping());

	// Now make sure the ping is below 5 for both, as there is no latency at this point.
	CRASH_COND(test.server_scene.scene_sync->get_peers()[peer1].get_ping() > 5);
	CRASH_COND(test.server_scene.scene_sync->get_peers()[peer2].get_ping() > 5);

	// TEST 2 with 100 PING
	test.network_properties.rtt_seconds = 0.1;

	test.do_test(20, true);

	// Make sure the ping is the same between client and the server.
	CRASH_COND(test.server_scene.scene_sync->get_peers()[peer1].get_ping() != test.peer_1_scene.scene_sync->get_peers()[peer1].get_ping());
	CRASH_COND(test.server_scene.scene_sync->get_peers()[peer2].get_ping() != test.peer_1_scene.scene_sync->get_peers()[peer2].get_ping());
	CRASH_COND(test.server_scene.scene_sync->get_peers()[peer1].get_ping() != test.peer_2_scene.scene_sync->get_peers()[peer1].get_ping());
	CRASH_COND(test.server_scene.scene_sync->get_peers()[peer2].get_ping() != test.peer_2_scene.scene_sync->get_peers()[peer2].get_ping());

	// Now make sure the ping is around 100
	CRASH_COND(test.server_scene.scene_sync->get_peers()[peer1].get_ping() < 60 || test.server_scene.scene_sync->get_peers()[peer1].get_ping() > 105);
	CRASH_COND(test.server_scene.scene_sync->get_peers()[peer2].get_ping() < 60 || test.server_scene.scene_sync->get_peers()[peer2].get_ping() > 105);
}

void test_doll_simulation() {
	test_ping();
}
}; //namespace NS_Test
