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

class TDSControlledObject : public NS::LocalSceneObject {
public:
	NS::ObjectLocalId local_id = NS::ObjectLocalId::NONE;

	TDSControlledObject() = default;

	virtual void on_scene_entry() override {
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
				std::bind(&TDSControlledObject::collect_inputs, this, std::placeholders::_1, std::placeholders::_2),
				std::bind(&TDSControlledObject::count_input_size, this, std::placeholders::_1),
				std::bind(&TDSControlledObject::are_inputs_different, this, std::placeholders::_1, std::placeholders::_2),
				std::bind(&TDSControlledObject::controller_process, this, std::placeholders::_1, std::placeholders::_2));

		p_scene_sync.register_variable(p_id, "xy");
	}

	void set_xy(float x, float y) {
		NS::VarData vd;
		vd.data.vec.x = x;
		vd.data.vec.y = y;
		vd.type = 0;
		NS::MapFunc::assign(variables, std::string("xy"), std::move(vd));
	}

	NS::VarData get_xy() const {
		const NS::VarData *vd = NS::MapFunc::get_or_null(variables, std::string("xy"));
		if (vd) {
			return NS::VarData::make_copy(*vd);
		} else {
			return NS::VarData(0, 0);
		}
	}

	// ------------------------------------------------- NetController interface
	bool previous_input = true;
	void collect_inputs(double p_delta, DataBuffer &r_buffer) {
		// Write true or false alternating each other.
		r_buffer.add(!previous_input);
		previous_input = !previous_input;
	}

	void controller_process(double p_delta, DataBuffer &p_buffer) {
		bool advance_or_turn;
		p_buffer.read(advance_or_turn);
		NS::VarData current = get_xy();
		if (advance_or_turn) {
			// Advance
			set_xy(current.data.vec.x + 1, current.data.vec.y);
		} else {
			// Turn
			set_xy(current.data.vec.x, current.data.vec.y + 1);
		}

		// TODO remove this, is here just for debug.
		const bool debug_procesing = false;
		if (debug_procesing) {
			if (authoritative_peer_id != 2) {
				return;
			}
			NS::PeerNetworkedController *controller = scene_owner->scene_sync->get_controller_for_peer(authoritative_peer_id);
			NS::FrameIndex fi = controller->get_current_frame_index();
			std::string frame_info;
			frame_info += "FrameIndex: " + fi;
			frame_info += " initial X: " + std::to_string(current.data.vec.x) + " Y: " + std::to_string(current.data.vec.y);
			frame_info += " input: " + std::string(advance_or_turn ? "advance" : "turn");

			if (controller->is_doll_controller()) {
				NS::SceneSynchronizerBase::__print_line("Doll controller " + frame_info);
			} else if (controller->is_player_controller()) {
				NS::SceneSynchronizerBase::__print_line("Player controller " + frame_info);
			} else if (controller->is_server_controller()) {
				NS::SceneSynchronizerBase::__print_line("Server controller " + frame_info);
			}
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
	std::vector<NS::FrameIndex> peer1_desync_detected;
	std::vector<NS::FrameIndex> peer2_desync_detected;

	NS::LocalNetworkProps network_properties;

	NS::LocalScene server_scene;
	NS::LocalScene peer_1_scene;
	NS::LocalScene peer_2_scene;
	TDSControlledObject *controlled_1_serv = nullptr;
	TDSControlledObject *controlled_1_peer1 = nullptr;
	TDSControlledObject *controlled_1_peer2 = nullptr;

	TDSControlledObject *controlled_2_serv = nullptr;
	TDSControlledObject *controlled_2_peer1 = nullptr;
	TDSControlledObject *controlled_2_peer2 = nullptr;

	float frame_confirmation_timespan = 1. / 60.;

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

		server_scene.scene_sync->set_frame_confirmation_timespan(frame_confirmation_timespan);

		// Then compose the scene: 2 controllers.
		controlled_1_serv = server_scene.add_object<TDSControlledObject>("controller_1", peer_1_scene.get_peer());
		controlled_1_peer1 = peer_1_scene.add_object<TDSControlledObject>("controller_1", peer_1_scene.get_peer());
		controlled_1_peer2 = peer_2_scene.add_object<TDSControlledObject>("controller_1", peer_1_scene.get_peer());

		controlled_2_serv = server_scene.add_object<TDSControlledObject>("controller_2", peer_2_scene.get_peer());
		controlled_2_peer1 = peer_1_scene.add_object<TDSControlledObject>("controller_2", peer_2_scene.get_peer());
		controlled_2_peer2 = peer_2_scene.add_object<TDSControlledObject>("controller_2", peer_2_scene.get_peer());

		server_scene.scene_sync->register_process(server_scene.scene_sync->find_local_id(), PROCESS_PHASE_LATE, [=](double p_delta) -> void {
			on_server_process(p_delta);
		});
		peer_1_scene.scene_sync->register_process(peer_1_scene.scene_sync->find_local_id(), PROCESS_PHASE_LATE, [=](double p_delta) -> void {
			on_client_1_process(p_delta);
		});
		peer_2_scene.scene_sync->register_process(peer_2_scene.scene_sync->find_local_id(), PROCESS_PHASE_LATE, [=](double p_delta) -> void {
			on_client_2_process(p_delta);
		});

		peer_1_scene.scene_sync->event_state_validated.bind([this](NS::FrameIndex fi, bool p_desync_detected) -> void {
			if (p_desync_detected) {
				peer1_desync_detected.push_back(fi);
			}
		});
		peer_2_scene.scene_sync->event_state_validated.bind([this](NS::FrameIndex fi, bool p_desync_detected) -> void {
			if (p_desync_detected) {
				peer2_desync_detected.push_back(fi);
			}
		});

		// Set the position of each object:
		controlled_1_serv->set_xy(100, 0);
		controlled_1_peer1->set_xy(100, 0);
		controlled_1_peer2->set_xy(100, 0);

		controlled_2_serv->set_xy(0, 0);
		controlled_2_peer1->set_xy(0, 0);
		controlled_2_peer2->set_xy(0, 0);

		on_scenes_initialized();
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

struct TestDollSimulationWithPositionCheck : public TestDollSimulationBase {
	virtual void on_scenes_initialized() override {
		// Ensure the controllers are at their initial location as defined by the doll simulation class.
		ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_1_serv->get_xy(), NS::VarData(100, 0)));
		ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_1_peer1->get_xy(), NS::VarData(100, 0)));
		ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_1_peer2->get_xy(), NS::VarData(100, 0)));

		ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_2_serv->get_xy(), NS::VarData(0, 0)));
		ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_2_peer2->get_xy(), NS::VarData(0, 0)));
		ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_2_peer1->get_xy(), NS::VarData(0, 0)));
	}

	std::vector<NS::VarData> controlled_1_player_position;
	std::vector<NS::VarData> controlled_2_player_position;

	virtual void on_server_process(double p_delta) override {
		// Nothing to do.
	}

	virtual void on_client_1_process(double p_delta) override {
		controlled_1_player_position.push_back(controlled_1_peer1->get_xy());
	}

	virtual void on_client_2_process(double p_delta) override {
		controlled_2_player_position.push_back(controlled_2_peer2->get_xy());
	}

	virtual void on_scenes_processed(double p_delta) override {
		const NS::FrameIndex controller_1_player_frame_index = peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index();
		const NS::FrameIndex controller_2_player_frame_index = peer_2_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->get_current_frame_index();

		const NS::FrameIndex controller_2_doll_frame_index = peer_1_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->get_current_frame_index();
		const NS::FrameIndex controller_1_doll_frame_index = peer_2_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index();

		if (controller_1_doll_frame_index != NS::FrameIndex::NONE) {
			// Make sure the players are always ahead the dolls.
			ASSERT_COND(controller_1_player_frame_index > controller_2_doll_frame_index);
			ASSERT_COND(controller_2_player_frame_index > controller_1_doll_frame_index);

			// Verify the doll are at the exact location they were on the player.
			const NS::VarData doll_1_position = controlled_1_peer2->get_xy();
			const NS::VarData doll_2_position = controlled_2_peer1->get_xy();
			const NS::VarData &player_1_position = controlled_1_player_position[controller_1_doll_frame_index.id];
			const NS::VarData &player_2_position = controlled_2_player_position[controller_2_doll_frame_index.id];
			ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(player_1_position, doll_1_position));
			ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(player_2_position, doll_2_position));
		}
	}
};

// Test the ability to process a doll without causing any
// reconciliation or miss any input.
void test_simulation_without_reconciliation(float p_frame_confirmation_timespan) {
	TestDollSimulationWithPositionCheck test;
	test.frame_confirmation_timespan = p_frame_confirmation_timespan;
	// This test is not triggering any desynchronization.
	test.init_test();

	test.do_test(100);

	ASSERT_COND(test.peer1_desync_detected.size() == 0);
	ASSERT_COND(test.peer2_desync_detected.size() == 0);
}

struct TestDollSimulationStorePositions : public TestDollSimulationBase {
	virtual void on_scenes_initialized() override {
		// Ensure the controllers are at their initial location as defined by the doll simulation class.
		ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_1_serv->get_xy(), NS::VarData(100, 0)));
		ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_1_peer1->get_xy(), NS::VarData(100, 0)));
		ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_1_peer2->get_xy(), NS::VarData(100, 0)));

		ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_2_serv->get_xy(), NS::VarData(0, 0)));
		ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_2_peer2->get_xy(), NS::VarData(0, 0)));
		ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_2_peer1->get_xy(), NS::VarData(0, 0)));
	}

	std::map<NS::FrameIndex, NS::VarData> controlled_1_player_position;
	std::map<NS::FrameIndex, NS::VarData> controlled_2_player_position;
	std::map<NS::FrameIndex, NS::VarData> controlled_1_doll_position;
	std::map<NS::FrameIndex, NS::VarData> controlled_2_doll_position;

	virtual void on_server_process(double p_delta) override {
	}

	virtual void on_client_1_process(double p_delta) override {
		const NS::FrameIndex controller_1_player_frame_index = peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index();
		const NS::FrameIndex controller_2_doll_frame_index = peer_1_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->get_current_frame_index();

		NS::MapFunc::assign(controlled_1_player_position, controller_1_player_frame_index, controlled_1_peer1->get_xy());
		NS::MapFunc::assign(controlled_2_doll_position, controller_2_doll_frame_index, controlled_2_peer1->get_xy());
	}

	virtual void on_client_2_process(double p_delta) override {
		const NS::FrameIndex controller_2_player_frame_index = peer_2_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->get_current_frame_index();
		const NS::FrameIndex controller_1_doll_frame_index = peer_2_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index();

		NS::MapFunc::assign(controlled_2_player_position, controller_2_player_frame_index, controlled_2_peer2->get_xy());
		NS::MapFunc::assign(controlled_1_doll_position, controller_1_doll_frame_index, controlled_1_peer2->get_xy());
	}

	void assert_positions() {
		assert_positions(controlled_1_player_position, controlled_1_doll_position);
		assert_positions(controlled_2_player_position, controlled_2_doll_position);
	}

	void assert_positions(const std::map<NS::FrameIndex, NS::VarData> &p_player_map, const std::map<NS::FrameIndex, NS::VarData> &p_doll_map) {
		// Find the biggeest FrameInput
		NS::FrameIndex biggest_frame_index{ 0 };
		for (const auto &[fi, vd] : p_doll_map) {
			if (fi != NS::FrameIndex::NONE) {
				if (fi > biggest_frame_index) {
					biggest_frame_index = fi;
				}
			}
		}

		// Now, iterate over all the frames and make sure the positions are the same
		for (NS::FrameIndex i{ 0 }; i < biggest_frame_index; i += 1) {
			const NS::VarData *player_position = NS::MapFunc::get_or_null(p_player_map, i);
			const NS::VarData *doll_position = NS::MapFunc::get_or_null(p_doll_map, i);
			ASSERT_COND(player_position);
			ASSERT_COND(doll_position);
			ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(*player_position, *doll_position));
		}
	}
};

// Test the ability to reconcile a desynchronized doll.
void test_simulation_reconciliation(float p_frame_confirmation_timespan) {
	TestDollSimulationStorePositions test;
	test.frame_confirmation_timespan = p_frame_confirmation_timespan;
	// This test is not triggering any desynchronization.
	test.init_test();

	test.do_test(30);

	// 1. Make sure no desync were detected so far.
	ASSERT_COND(test.peer1_desync_detected.size() == 0);
	ASSERT_COND(test.peer2_desync_detected.size() == 0);

	// Ensure the positions are all the same.
	test.assert_positions();

	// 2. Introduce a desync manually and test again.
	test.controlled_1_peer2->set_xy(0, 0); // Modify the doll on peer 1
	test.controlled_2_peer1->set_xy(0, 0); // Modify the doll on peer 2

	// Run another 30 frames.
	test.do_test(30);

	// Make sure there was 1 desyc
	ASSERT_COND(test.peer1_desync_detected.size() == 1);
	ASSERT_COND(test.peer2_desync_detected.size() == 1);

	// and despite that the simulations are correct.
	test.assert_positions();
}

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
	// TODO enable these
	//test_simulation_without_reconciliation(0.0);
	//test_simulation_without_reconciliation(1. / 30.);
	test_simulation_reconciliation(0.0);
	//test_simulation_reconciliation(1.0 / 5.0); // TODO enable
	// TODO test with latency.
	// TODO test lag compensation.
	// TODO ensure the snapshots are cleared correctly and they do not buildup.
	test_latency();

	// TODO Remove this once the test are all implemented.
	ASSERT_COND(false);
}

}; //namespace NS_Test
