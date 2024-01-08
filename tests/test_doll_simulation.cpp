#include "test_doll_simulation.h"

#include "core/error/error_macros.h"

#include "../core/core.h"
#include "../core/net_utilities.h"
#include "../core/processor.h"
#include "../core/var_data.h"
#include "../data_buffer.h"
#include "../tests/local_network.h"
#include "../tests/local_scene.h"
#include "../tests/test_math_lib.h"
#include "local_scene.h"
#include "test_math_lib.h"
#include <chrono>
#include <string>
#include <thread>

namespace NS_Test {

const double delta = 1.0 / 60.0;

class TDSLocalNetworkedController : public NS::LocalSceneObject {
public:
	NS::ObjectLocalId local_id = NS::ObjectLocalId::NONE;

	TDSLocalNetworkedController() = default;

	virtual void on_scene_entry() override {
		// Setup the NetworkInterface.
		set_xi(0);

		get_scene()->scene_sync->register_app_object(get_scene()->scene_sync->to_handle(this));
	}

	virtual void on_scene_exit() override {
		get_scene()->scene_sync->unregister_app_object(local_id);
	}

	virtual void setup_synchronizer(NS::LocalSceneSynchronizer &p_scene_sync, NS::ObjectLocalId p_id) override {
		local_id = p_id;

		p_scene_sync.setup_controller(
				p_id,
				authoritative_peer_id,
				[this](double p_delta, DataBuffer &r_buffer) -> void { collect_inputs(p_delta, r_buffer); },
				[this](DataBuffer &p_buffer) -> int { return count_input_size(p_buffer); },
				[this](DataBuffer &p_buffer_A, DataBuffer &p_buffer_b) -> bool { return are_inputs_different(p_buffer_A, p_buffer_b); },
				[this](double p_delta, DataBuffer &p_buffer) -> void { controller_process(p_delta, p_buffer); });

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
	void collect_inputs(double p_delta, DataBuffer &r_buffer) {
		r_buffer.add(true);
	}

	void controller_process(double p_delta, DataBuffer &p_buffer) {
		bool advance_xi;
		p_buffer.read(advance_xi);
		if (advance_xi) {
			set_xi(get_xi() + 1);
		}
	}

	bool are_inputs_different(DataBuffer &p_buffer_A, DataBuffer &p_buffer_B) {
		const bool v1 = p_buffer_A.read_bool();
		const bool v2 = p_buffer_B.read_bool();
		return v1 != v2;
	}

	uint32_t count_input_size(DataBuffer &p_buffer) {
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
	virtual void on_server_process(double p_delta) {}
	virtual void on_client_1_process(double p_delta) {}
	virtual void on_client_2_process(double p_delta) {}
	virtual void on_scenes_processed(double p_delta) {}

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

		server_scene.scene_sync->register_process(server_scene.scene_sync->find_local_id(), PROCESS_PHASE_LATE, [=](double p_delta) -> void {
			on_server_process(p_delta);
		});
		peer_1_scene.scene_sync->register_process(peer_1_scene.scene_sync->find_local_id(), PROCESS_PHASE_LATE, [=](double p_delta) -> void {
			on_client_1_process(p_delta);
		});
		peer_2_scene.scene_sync->register_process(peer_2_scene.scene_sync->find_local_id(), PROCESS_PHASE_LATE, [=](double p_delta) -> void {
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

	double rand_range(double M, double N) {
		return M + (rand() / (RAND_MAX / (N - M)));
	}

	void do_test(const int p_frames_count, bool p_wait_for_time_pass = false) {
		for (int i = 0; i < p_frames_count; i++) {
			float sim_delta = delta;
			while (sim_delta > 0.0) {
				const float rand_delta = rand_range(0.005, sim_delta);
				sim_delta -= std::min(rand_delta, sim_delta);

				server_scene.process(rand_delta);
				peer_1_scene.process(rand_delta);
				peer_2_scene.process(rand_delta);
			}

			on_scenes_processed(delta);
			if (p_wait_for_time_pass) {
				const int ms = delta * 1000.0;
				std::this_thread::sleep_for(std::chrono::milliseconds(ms));
			}
		}
	}
};

void test_latency() {
	TestDollSimulationBase test;
	test.init_test();

	test.server_scene.scene_sync->set_frame_confirmation_timespan(0.0);
	test.server_scene.scene_sync->set_latency_update_rate(0.05);

	const int peer1 = test.peer_1_scene.get_peer();
	const int peer2 = test.peer_2_scene.get_peer();

	// TEST 1 with 0 latency
	test.network_properties.rtt_seconds = 0.;

	test.do_test(10, true);

	// Make sure the latency is the same between client and the server.
	ASSERT_COND_MSG(test.server_scene.scene_sync->get_peer_latency(peer1) == test.peer_1_scene.scene_sync->get_peer_latency(peer1), "Server latency: " + std::to_string(test.server_scene.scene_sync->get_peer_latency(peer1)) + " Client latency: " + std::to_string(test.peer_1_scene.scene_sync->get_peer_latency(peer1)));
	ASSERT_COND(test.server_scene.scene_sync->get_peer_latency(peer2) == test.peer_1_scene.scene_sync->get_peer_latency(peer2));
	ASSERT_COND(test.server_scene.scene_sync->get_peer_latency(peer1) == test.peer_2_scene.scene_sync->get_peer_latency(peer1));
	ASSERT_COND(test.server_scene.scene_sync->get_peer_latency(peer2) == test.peer_2_scene.scene_sync->get_peer_latency(peer2));

	// Now make sure the latency is below 5 for both, as there is no latency at this point.
	ASSERT_COND(test.server_scene.scene_sync->get_peer_latency(peer1) <= 5);
	ASSERT_COND(test.server_scene.scene_sync->get_peer_latency(peer2) <= 5);

	// TEST 2 with 100 latency
	test.network_properties.rtt_seconds = 0.1;

	test.do_test(20, true);

	// Make sure the latency is the same between client and the server.
	ASSERT_COND(test.server_scene.scene_sync->get_peer_latency(peer1) == test.peer_1_scene.scene_sync->get_peer_latency(peer1));
	ASSERT_COND(test.server_scene.scene_sync->get_peer_latency(peer2) == test.peer_1_scene.scene_sync->get_peer_latency(peer2));
	ASSERT_COND(test.server_scene.scene_sync->get_peer_latency(peer1) == test.peer_2_scene.scene_sync->get_peer_latency(peer1));
	ASSERT_COND(test.server_scene.scene_sync->get_peer_latency(peer2) == test.peer_2_scene.scene_sync->get_peer_latency(peer2));

	// Now make sure the latency is around 100
	ASSERT_COND(test.server_scene.scene_sync->get_peer_latency(peer1) >= 60 && test.server_scene.scene_sync->get_peer_latency(peer1) <= 105);
	ASSERT_COND(test.server_scene.scene_sync->get_peer_latency(peer2) >= 60 && test.server_scene.scene_sync->get_peer_latency(peer2) <= 105);
}

void test_doll_simulation() {
	test_latency();
}

}; //namespace NS_Test
