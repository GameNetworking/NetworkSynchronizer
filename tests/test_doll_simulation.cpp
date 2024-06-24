#include "test_doll_simulation.h"

#include "../core/core.h"
#include "../core/data_buffer.h"
#include "../core/net_utilities.h"
#include "../core/processor.h"
#include "../core/var_data.h"
#include "../tests/local_network.h"
#include "../tests/local_scene.h"
#include "../tests/test_math_lib.h"
#include "local_scene.h"
#include "test_math_lib.h"
#include <chrono>
#include <string>
#include <thread>

namespace NS_Test {
const float delta = 1.0f / 60.0f;

class TDSControlledObject : public NS::LocalSceneObject {
public:
	NS::ObjectLocalId local_id = NS::ObjectLocalId::NONE;
	bool modify_input_on_next_frame = false;
	NS::VarData xy;

	TDSControlledObject() = default;

	virtual void on_scene_entry() override {
		if (get_scene()->scene_sync->is_server()) {
			get_scene()->scene_sync->register_app_object(get_scene()->scene_sync->to_handle(this));
		}
	}

	virtual void on_scene_exit() override {
		get_scene()->scene_sync->unregister_app_object(local_id);
	}

	virtual void setup_synchronizer(NS::LocalSceneSynchronizer &p_scene_sync, NS::ObjectLocalId p_id) override {
		local_id = p_id;

		p_scene_sync.setup_controller(
				p_id,
				std::bind(&TDSControlledObject::collect_inputs, this, std::placeholders::_1, std::placeholders::_2),
				std::bind(&TDSControlledObject::are_inputs_different, this, std::placeholders::_1, std::placeholders::_2),
				std::bind(&TDSControlledObject::controller_process, this, std::placeholders::_1, std::placeholders::_2));

		if (p_scene_sync.is_server()) {
			p_scene_sync.set_controlled_by_peer(
					p_id,
					authoritative_peer_id);
		}

		p_scene_sync.register_variable(
				p_id,
				"xy",
				[](NS::SynchronizerManager &p_synchronizer_manager, NS::ObjectHandle p_handle, const char *p_var_name, const NS::VarData &p_value) {
					static_cast<TDSControlledObject *>(NS::LocalSceneSynchronizer::from_handle(p_handle))->set_xy(p_value.data.vec.x, p_value.data.vec.y);
				},
				[](const NS::SynchronizerManager &p_synchronizer_manager, NS::ObjectHandle p_handle, const char *p_var_name, NS::VarData &r_value) {
					r_value.copy(static_cast<const TDSControlledObject *>(NS::LocalSceneSynchronizer::from_handle(p_handle))->xy);
				});
	}

	void set_xy(double x, double y) {
		xy = NS::VarData(x, y);
	}

	NS::VarData get_xy() const {
		return NS::VarData::make_copy(xy);
	}

	// ------------------------------------------------- NetController interface
	bool previous_input = true;

	void collect_inputs(float p_delta, NS::DataBuffer &r_buffer) {
		// Write true or false alternating each other.
		r_buffer.add(!previous_input);
		previous_input = !previous_input;
	}

	void controller_process(float p_delta, NS::DataBuffer &p_buffer) {
		bool advance_or_turn;
		p_buffer.read(advance_or_turn);

		if (modify_input_on_next_frame) {
			modify_input_on_next_frame = false;
			advance_or_turn = !advance_or_turn;
		}

		NS::VarData current = get_xy();
		if (advance_or_turn) {
			// Advance
			set_xy(current.data.vec.x + 1.0, current.data.vec.y);
		} else {
			// Turn
			set_xy(current.data.vec.x, current.data.vec.y + 1.0);
		}
	}

	bool are_inputs_different(NS::DataBuffer &p_buffer_A, NS::DataBuffer &p_buffer_B) {
		const bool v1 = p_buffer_A.read_bool();
		const bool v2 = p_buffer_B.read_bool();
		return v1 != v2;
	}
};

/// This class is responsible to verify the doll simulation.
/// This class is made in a way which allows to be overriden to test the sync
/// still works under bad network conditions.
struct TestDollSimulationBase {
	std::vector<NS::FrameIndex> peer1_desync_detected;
	std::vector<NS::FrameIndex> peer2_desync_detected;

	NS::LocalNetworkProps network_properties;

	/// Set this to true to ensure the sub_ticks doesn't cause de-syncs.
	bool disable_sub_ticks = false;

	NS::LocalScene server_scene;
	NS::LocalScene peer_1_scene;
	NS::LocalScene peer_2_scene;
	TDSControlledObject *controlled_1_serv = nullptr;
	TDSControlledObject *controlled_1_peer1 = nullptr;
	TDSControlledObject *controlled_1_peer2 = nullptr;

	TDSControlledObject *controlled_2_serv = nullptr;
	TDSControlledObject *controlled_2_peer1 = nullptr;
	TDSControlledObject *controlled_2_peer2 = nullptr;

	float frame_confirmation_timespan = 1.f / 60.f;

private:
	virtual void on_scenes_initialized() {
	}

	virtual void on_server_process(float p_delta) {
	}

	virtual void on_client_1_process(float p_delta) {
	}

	virtual void on_client_2_process(float p_delta) {
	}

	virtual void on_scenes_processed(float p_delta) {
	}

public:
	TestDollSimulationBase() {
	}

	virtual ~TestDollSimulationBase() {
	}

	void init_test(bool p_no_sub_ticks = false) {
		disable_sub_ticks = p_no_sub_ticks;

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
		if (p_no_sub_ticks) {
			server_scene.scene_sync =
					server_scene.add_object<NS::LocalSceneSynchronizerNoSubTicks>("sync", server_scene.get_peer());
			peer_1_scene.scene_sync =
					peer_1_scene.add_object<NS::LocalSceneSynchronizerNoSubTicks>("sync", server_scene.get_peer());
			peer_2_scene.scene_sync =
					peer_2_scene.add_object<NS::LocalSceneSynchronizerNoSubTicks>("sync", server_scene.get_peer());
		} else {
			server_scene.scene_sync =
					server_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
			peer_1_scene.scene_sync =
					peer_1_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
			peer_2_scene.scene_sync =
					peer_2_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
		}

		server_scene.scene_sync->set_frame_confirmation_timespan(frame_confirmation_timespan);

		// Then compose the scene: 2 controllers.
		controlled_1_serv = server_scene.add_object<TDSControlledObject>("controller_1", peer_1_scene.get_peer());
		controlled_1_peer1 = peer_1_scene.add_object<TDSControlledObject>("controller_1", peer_1_scene.get_peer());
		controlled_1_peer2 = peer_2_scene.add_object<TDSControlledObject>("controller_1", peer_1_scene.get_peer());

		controlled_2_serv = server_scene.add_object<TDSControlledObject>("controller_2", peer_2_scene.get_peer());
		controlled_2_peer1 = peer_1_scene.add_object<TDSControlledObject>("controller_2", peer_2_scene.get_peer());
		controlled_2_peer2 = peer_2_scene.add_object<TDSControlledObject>("controller_2", peer_2_scene.get_peer());

		server_scene.scene_sync->register_process(server_scene.scene_sync->find_local_id(), PROCESS_PHASE_LATE, [=](float p_delta) -> void {
			on_server_process(p_delta);
		});
		peer_1_scene.scene_sync->register_process(peer_1_scene.scene_sync->find_local_id(), PROCESS_PHASE_LATE, [=](float p_delta) -> void {
			on_client_1_process(p_delta);
		});
		peer_2_scene.scene_sync->register_process(peer_2_scene.scene_sync->find_local_id(), PROCESS_PHASE_LATE, [=](float p_delta) -> void {
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
		controlled_1_serv->set_xy(100., 0.);
		controlled_1_peer1->set_xy(100., 0.);
		controlled_1_peer2->set_xy(100., 0.);

		controlled_2_serv->set_xy(0., 0.);
		controlled_2_peer1->set_xy(0., 0.);
		controlled_2_peer2->set_xy(0., 0.);

		on_scenes_initialized();
	}

	float rand_range(float M, float N) {
		return float(M + (float(rand()) / (float(RAND_MAX) / (N - M))));
	}

	void do_test(const int p_frames_count, bool p_wait_for_time_pass = false, bool p_process_server = true, bool p_process_peer1 = true, bool p_process_peer2 = true) {
		for (int i = 0; i < p_frames_count; i++) {
			float sim_delta = delta;
			float processed_time = 0.0f;
			while (sim_delta > 0.0001f) {
				const float rand_delta = disable_sub_ticks ? sim_delta : rand_range(0.005f, sim_delta);
				sim_delta -= std::min(rand_delta, sim_delta);

				processed_time += rand_delta;

				if (p_process_server) {
					server_scene.process(rand_delta);
				}
				if (p_process_peer1) {
					peer_1_scene.process(rand_delta);
				}
				if (p_process_peer2) {
					peer_2_scene.process(rand_delta);
				}
			}

			on_scenes_processed(processed_time);
			if (p_wait_for_time_pass) {
				const int ms = int(processed_time * 1000.0f);
				std::this_thread::sleep_for(std::chrono::milliseconds(ms));
			}
		}
	}
};

struct TestDollSimulationWithPositionCheck : public TestDollSimulationBase {
	virtual void on_scenes_initialized() override {
		// Ensure the controllers are at their initial location as defined by the doll simulation class.
		NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_1_serv->get_xy(), NS::VarData(100., 0.)));
		NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_1_peer1->get_xy(), NS::VarData(100., 0.)));
		NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_1_peer2->get_xy(), NS::VarData(100., 0.)));

		NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_2_serv->get_xy(), NS::VarData(0., 0.)));
		NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_2_peer2->get_xy(), NS::VarData(0., 0.)));
		NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_2_peer1->get_xy(), NS::VarData(0., 0.)));
	}

	std::vector<NS::VarData> controlled_1_player_position;
	std::vector<NS::VarData> controlled_2_player_position;

	virtual void on_server_process(float p_delta) override {
		// Nothing to do.
	}

	virtual void on_client_1_process(float p_delta) override {
		// Store the player 1 inputs.
		const NS::FrameIndex controller_1_player_frame_index = peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index();
		if (controlled_1_player_position.size() <= controller_1_player_frame_index.id) {
			controlled_1_player_position.resize(controller_1_player_frame_index.id + 1);
		}
		controlled_1_player_position[controller_1_player_frame_index.id] = controlled_1_peer1->get_xy();
	}

	virtual void on_client_2_process(float p_delta) override {
		// Store the player 2 inputs.
		const NS::FrameIndex controller_2_player_frame_index = peer_2_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->get_current_frame_index();
		if (controlled_2_player_position.size() <= controller_2_player_frame_index.id) {
			controlled_2_player_position.resize(controller_2_player_frame_index.id + 1);
		}
		controlled_2_player_position[controller_2_player_frame_index.id] = controlled_2_peer2->get_xy();
	}

	virtual void on_scenes_processed(float p_delta) override {
		NS_ASSERT_COND(peer1_desync_detected.size() == 0);
		NS_ASSERT_COND(peer2_desync_detected.size() == 0);

		const NS::FrameIndex controller_1_player_frame_index = peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index();
		const NS::FrameIndex controller_2_player_frame_index = peer_2_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->get_current_frame_index();

		const NS::FrameIndex controller_2_doll_frame_index = peer_1_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->get_current_frame_index();
		const NS::FrameIndex controller_1_doll_frame_index = peer_2_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index();

		if (controller_1_doll_frame_index != NS::FrameIndex::NONE) {
			// Make sure the players are always ahead the dolls.
			NS_ASSERT_COND(controller_1_player_frame_index > controller_2_doll_frame_index);
			NS_ASSERT_COND(controller_2_player_frame_index > controller_1_doll_frame_index);

			// Verify the doll are at the exact location they were on the player.
			const NS::VarData doll_1_position = controlled_1_peer2->get_xy();
			const NS::VarData doll_2_position = controlled_2_peer1->get_xy();
			const NS::VarData &player_1_position = controlled_1_player_position[controller_1_doll_frame_index.id];
			const NS::VarData &player_2_position = controlled_2_player_position[controller_2_doll_frame_index.id];
			NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(player_1_position, doll_1_position));
			NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(player_2_position, doll_2_position));
		}
	}
};

// Test the ability to process a doll without causing any
// reconciliation or miss any input.
void test_simulation_without_reconciliation(float p_frame_confirmation_timespan) {
	TestDollSimulationWithPositionCheck test;
	test.frame_confirmation_timespan = p_frame_confirmation_timespan;
	// NOTICE: Disabling sub ticks because these cause some desync
	//         desync that invalidate this test.
	test.init_test(true);

	test.do_test(100);

	NS_ASSERT_COND(test.peer1_desync_detected.size() == 0);
	NS_ASSERT_COND(test.peer2_desync_detected.size() == 0);
}

struct TestDollSimulationStorePositions : public TestDollSimulationBase {
	virtual void on_scenes_initialized() override {
		// Ensure the controllers are at their initial location as defined by the doll simulation class.
		NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_1_serv->get_xy(), NS::VarData(100., 0.)));
		NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_1_peer1->get_xy(), NS::VarData(100., 0.)));
		NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_1_peer2->get_xy(), NS::VarData(100., 0.)));

		NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_2_serv->get_xy(), NS::VarData(0., 0.)));
		NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_2_peer2->get_xy(), NS::VarData(0., 0.)));
		NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_2_peer1->get_xy(), NS::VarData(0., 0.)));
	}

	// Used to introduce a desync by changing the input on the server.
	std::map<NS::FrameIndex, NS::VarData> controlled_1_player_position;
	std::map<NS::FrameIndex, NS::VarData> controlled_2_player_position;
	std::map<NS::FrameIndex, NS::VarData> controlled_1_doll_position;
	std::map<NS::FrameIndex, NS::VarData> controlled_2_doll_position;
	int doll_1_max_queued_input_count = 0;
	int doll_2_max_queued_input_count = 0;

	virtual void on_server_process(float p_delta) override {
	}

	virtual void on_client_1_process(float p_delta) override {
		const NS::FrameIndex controller_1_player_frame_index = peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index();
		const NS::FrameIndex controller_2_doll_frame_index = peer_1_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->get_current_frame_index();

		const int doll_input_count = peer_1_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->get_doll_controller()->get_inputs_count();
		doll_2_max_queued_input_count = std::max(doll_2_max_queued_input_count, doll_input_count);

		NS::MapFunc::assign(controlled_1_player_position, controller_1_player_frame_index, controlled_1_peer1->get_xy());
		NS::MapFunc::assign(controlled_2_doll_position, controller_2_doll_frame_index, controlled_2_peer1->get_xy());
	}

	virtual void on_client_2_process(float p_delta) override {
		const NS::FrameIndex controller_2_player_frame_index = peer_2_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->get_current_frame_index();
		const NS::FrameIndex controller_1_doll_frame_index = peer_2_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index();

		const int doll_input_count = peer_2_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_doll_controller()->get_inputs_count();
		doll_1_max_queued_input_count = std::max(doll_1_max_queued_input_count, doll_input_count);

		NS::MapFunc::assign(controlled_2_player_position, controller_2_player_frame_index, controlled_2_peer2->get_xy());
		NS::MapFunc::assign(controlled_1_doll_position, controller_1_doll_frame_index, controlled_1_peer2->get_xy());
	}

	void assert_no_desync(NS::FrameIndex peer_1_assert_after, NS::FrameIndex peer_2_assert_after) {
		assert_no_desync(peer1_desync_detected, peer_1_assert_after);
		assert_no_desync(peer2_desync_detected, peer_2_assert_after);
	}

	void assert_no_desync(const std::vector<NS::FrameIndex> &p_desync_vector, NS::FrameIndex assert_after) {
		for (auto desync_frame : p_desync_vector) {
			NS_ASSERT_COND(desync_frame < assert_after);
		}
	}

	void assert_positions(NS::FrameIndex controlled_1_assert_after, NS::FrameIndex controlled_2_assert_after) {
		assert_positions(controlled_1_player_position, controlled_1_doll_position, controlled_1_assert_after);
		assert_positions(controlled_2_player_position, controlled_2_doll_position, controlled_2_assert_after);
	}

	void assert_positions(const std::map<NS::FrameIndex, NS::VarData> &p_player_map, const std::map<NS::FrameIndex, NS::VarData> &p_doll_map, NS::FrameIndex assert_after) {
		// Find the biggeest FrameInput
		NS::FrameIndex biggest_frame_index = NS::FrameIndex{ { 0 } };
		for (const auto &[fi, vd] : p_doll_map) {
			if (fi != NS::FrameIndex::NONE) {
				if (fi > biggest_frame_index) {
					biggest_frame_index = fi;
				}
			}
		}

		NS_ASSERT_COND(assert_after <= biggest_frame_index)

		// Now, iterate over all the frames and make sure the positions are the same
		for (NS::FrameIndex i = NS::FrameIndex{ { 0 } }; i <= biggest_frame_index; i += 1) {
			const NS::VarData *player_position = NS::MapFunc::get_or_null(p_player_map, i);
			const NS::VarData *doll_position = NS::MapFunc::get_or_null(p_doll_map, i);
			if (i > assert_after) {
				NS_ASSERT_COND(player_position);
				NS_ASSERT_COND(doll_position);
				NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(*player_position, *doll_position));
			}
		}
	}
};

// Test the ability to reconcile a desynchronized doll.
void test_simulation_reconciliation(float p_frame_confirmation_timespan) {
	TestDollSimulationStorePositions test;
	test.frame_confirmation_timespan = p_frame_confirmation_timespan;
	// NOTICE: Disabling sub ticks because these cause some additional and
	//         difficult to control desync that invalidate this test.
	test.init_test(true);

	test.do_test(30);

	// 1. Make sure no desync were detected so far.
	NS_ASSERT_COND(test.peer1_desync_detected.size() == 0);
	NS_ASSERT_COND(test.peer2_desync_detected.size() == 0);

	// Ensure the positions are all the same.
	test.assert_positions(NS::FrameIndex{ { 0 } }, NS::FrameIndex{ { 0 } });

	// 2. Introduce a desync manually and test again.
	test.controlled_1_peer2->set_xy(0, 0); // Modify the doll on peer 1
	test.controlled_2_peer1->set_xy(0, 0); // Modify the doll on peer 2

	// Run another 30 frames.
	test.do_test(30);

	NS_ASSERT_COND(test.peer1_desync_detected.size() == test.peer2_desync_detected.size());
	if (p_frame_confirmation_timespan <= 0.0) {
		// Ensure it was able to reconcile right away.
		// With `p_frame_confirmation_timespan == 0` the server snapshot is
		// received before the doll process it, and since the doll is able to
		// apply the server's snapshot during the normal processing, the desync
		// is not even triggered.
		NS_ASSERT_COND(test.peer1_desync_detected.size() == 0);
		NS_ASSERT_COND(test.peer2_desync_detected.size() == 0);
	} else {
		// Ensure it was able to reconcile in 1 frame or less.
		NS_ASSERT_COND(test.peer1_desync_detected.size() <= 1);
		NS_ASSERT_COND(test.peer2_desync_detected.size() <= 1);

		// Make sure the reconciliation was successful.
		// NOTE: 45 is a margin established basing on the `p_frame_confirmation_timespan`.
		const NS::FrameIndex ensure_no_desync_after = NS::FrameIndex{ { 45 } };
		test.assert_no_desync(ensure_no_desync_after, ensure_no_desync_after);

		// and despite that the simulations are correct.
		test.assert_positions(ensure_no_desync_after, ensure_no_desync_after);
	}
}

void test_simulation_with_hiccups(TestDollSimulationStorePositions &test) {
	// Partially process.
	test.network_properties.rtt_seconds = 0.0;

	{
		NS::FrameIndex controller_1_doll_frame_index = test.peer_2_scene.scene_sync->get_controller_for_peer(test.peer_1_scene.get_peer())->get_current_frame_index();
		NS::FrameIndex controller_2_doll_frame_index = test.peer_1_scene.scene_sync->get_controller_for_peer(test.peer_2_scene.get_peer())->get_current_frame_index();

		for (int i = 0; i < 20; i++) {
			if (i % 2 == 0) {
				test.do_test(10, false, true, false, true);
			} else {
				test.do_test(10, false, true, true, false);
			}

			const NS::FrameIndex controller_1_doll_new_frame_index = test.peer_2_scene.scene_sync->get_controller_for_peer(test.peer_1_scene.get_peer())->get_current_frame_index();
			const NS::FrameIndex controller_2_doll_new_frame_index = test.peer_1_scene.scene_sync->get_controller_for_peer(test.peer_2_scene.get_peer())->get_current_frame_index();

			// Ensure the doll keep going forward.
			NS_ASSERT_COND(controller_1_doll_frame_index == NS::FrameIndex::NONE || controller_1_doll_frame_index <= controller_1_doll_new_frame_index);
			NS_ASSERT_COND(controller_2_doll_frame_index == NS::FrameIndex::NONE || controller_2_doll_frame_index <= controller_2_doll_new_frame_index);

			controller_1_doll_frame_index = controller_1_doll_new_frame_index;
			controller_2_doll_frame_index = controller_2_doll_new_frame_index;
		}
	}

	test.do_test(100);

	const NS::FrameIndex controller_1_last_player_frame_index = test.peer_1_scene.scene_sync->get_controller_for_peer(test.peer_1_scene.get_peer())->get_current_frame_index();
	const NS::FrameIndex controller_2_last_player_frame_index = test.peer_2_scene.scene_sync->get_controller_for_peer(test.peer_2_scene.get_peer())->get_current_frame_index();

	const NS::FrameIndex controller_1_last_doll_frame_index = test.peer_2_scene.scene_sync->get_controller_for_peer(test.peer_1_scene.get_peer())->get_current_frame_index();
	const NS::FrameIndex controller_2_last_doll_frame_index = test.peer_1_scene.scene_sync->get_controller_for_peer(test.peer_2_scene.get_peer())->get_current_frame_index();

	const int latency_factor = 15;

	NS_ASSERT_COND(controller_1_last_player_frame_index - latency_factor <= controller_1_last_doll_frame_index);
	NS_ASSERT_COND(controller_2_last_player_frame_index - latency_factor <= controller_2_last_doll_frame_index);

	// Make sure the last frames are identical.
	test.assert_positions(
			test.peer1_desync_detected.back() + 10,
			test.peer2_desync_detected.back() + 10);
}

void test_simulation_with_latency() {
	TestDollSimulationStorePositions test;
	test.frame_confirmation_timespan = 1.0f / 10.0f;
	// NOTICE: Disabling sub ticks because these cause some additional and
	//         difficult to control desync that invalidate this test.
	test.init_test(true);

	const NS::PeerNetworkedController *doll_controller_1 = test.peer_1_scene.scene_sync->get_controller_for_peer(test.peer_2_scene.get_peer());
	const NS::PeerNetworkedController *doll_controller_2 = test.peer_2_scene.scene_sync->get_controller_for_peer(test.peer_1_scene.get_peer());

	test.do_test(30);

	// 1. Make sure no desync were detected so far.
	NS_ASSERT_COND(test.peer1_desync_detected.size() == 0);
	NS_ASSERT_COND(test.peer2_desync_detected.size() == 0);

	// Ensure the positions are all the same.
	test.assert_positions(NS::FrameIndex{ { 0 } }, NS::FrameIndex{ { 0 } });

	// 2. Introduce some latency
	test.network_properties.rtt_seconds = 0.2f;

	test.do_test(600);

	NS::FrameIndex assert_after = NS::FrameIndex{ { 90 } };
	// Make sure no desync were detected after:
	test.assert_no_desync(assert_after, assert_after);
	// Ensure the positions are all the same after:

	test.assert_positions(assert_after, assert_after);

	// 3. Remove the latency
	test.network_properties.rtt_seconds = 0.0;

	const size_t desync_count_peer_1 = test.peer1_desync_detected.size();
	const size_t desync_count_peer_2 = test.peer2_desync_detected.size();

	test.do_test(200);

	// Make sure there was just 1 desync that was triggered by the lag compensation
	// to clear the accumulated inputs.
	NS_ASSERT_COND(test.peer1_desync_detected.size() == desync_count_peer_1 + 1);
	NS_ASSERT_COND(test.peer2_desync_detected.size() == desync_count_peer_2 + 1);

	assert_after = std::max(test.peer1_desync_detected.back(), test.peer2_desync_detected.back()) + 1;
	test.assert_no_desync(assert_after, assert_after);

	// Ensure the positions are all the same.
	test.assert_positions(assert_after, assert_after);

	// Ensure the dolls' queued inputs are no more than 5, to ensure the lag
	// compensation works.
	const int doll_controller_1_input_count = doll_controller_1->get_doll_controller()->get_inputs_count();
	const int doll_controller_2_input_count = doll_controller_2->get_doll_controller()->get_inputs_count();

	NS_ASSERT_COND(doll_controller_1_input_count <= test.doll_1_max_queued_input_count);
	NS_ASSERT_COND(doll_controller_2_input_count <= test.doll_2_max_queued_input_count);

	NS_ASSERT_COND(doll_controller_1_input_count <= 15);
	NS_ASSERT_COND(doll_controller_2_input_count <= 15);

	// Simulate an oscillating connection and ensure the controller is able to
	// reconcile and keep catching the server when the connection becomes good.
	{
		for (int i = 0; i < 10; i++) {
			if (i % 2 == 0) {
				test.network_properties.rtt_seconds = 0.5;
				// Introduce a desync manually.
				test.controlled_1_peer2->set_xy(0, 0); // Modify the doll on peer 1
				test.controlled_2_peer1->set_xy(0, 0); // Modify the doll on peer 2
			} else {
				test.network_properties.rtt_seconds = 0.0;
			}
			test.do_test(10);
		}

		test.network_properties.rtt_seconds = 0.0;
		test.do_test(10);

		const NS::FrameIndex controller_1_last_player_frame_index = test.peer_1_scene.scene_sync->get_controller_for_peer(test.peer_1_scene.get_peer())->get_current_frame_index();
		const NS::FrameIndex controller_2_last_player_frame_index = test.peer_2_scene.scene_sync->get_controller_for_peer(test.peer_2_scene.get_peer())->get_current_frame_index();

		const NS::FrameIndex controller_1_last_doll_frame_index = test.peer_2_scene.scene_sync->get_controller_for_peer(test.peer_1_scene.get_peer())->get_current_frame_index();
		const NS::FrameIndex controller_2_last_doll_frame_index = test.peer_1_scene.scene_sync->get_controller_for_peer(test.peer_2_scene.get_peer())->get_current_frame_index();

		const int latency_factor = 15;

		NS_ASSERT_COND(controller_1_last_player_frame_index - latency_factor <= controller_1_last_doll_frame_index);
		NS_ASSERT_COND(controller_2_last_player_frame_index - latency_factor <= controller_2_last_doll_frame_index);

		test.assert_positions(
				controller_1_last_player_frame_index - latency_factor,
				controller_2_last_player_frame_index - latency_factor);
	}

	// Partially process.
	test_simulation_with_hiccups(test);
}

void test_simulation_with_hiccups() {
	TestDollSimulationStorePositions test;
	test.frame_confirmation_timespan = 1.0f / 10.0f;
	// NOTICE: Disabling sub ticks because these cause some additional and
	//         difficult to control desync that invalidate this test.
	test.init_test(true);

	test_simulation_with_hiccups(test);
}

void test_latency() {
	TestDollSimulationBase test;
	test.init_test();

	test.server_scene.scene_sync->set_frame_confirmation_timespan(0.0f);
	test.server_scene.scene_sync->set_latency_update_rate(0.05f);

	const int peer1 = test.peer_1_scene.get_peer();
	const int peer2 = test.peer_2_scene.get_peer();

	// TEST 1 with 0 latency
	test.network_properties.rtt_seconds = 0.f;

	test.do_test(10, true);

	// Make sure the latency is the same between client and the server.
	NS_ASSERT_COND_MSG(test.server_scene.scene_sync->get_peer_latency_ms(peer1) == test.peer_1_scene.scene_sync->get_peer_latency_ms(peer1), "Server latency: " + std::to_string(test.server_scene.scene_sync->get_peer_latency_ms(peer1)) + " Client latency: " + std::to_string(test.peer_1_scene.scene_sync->get_peer_latency_ms(peer1)));
	NS_ASSERT_COND(test.server_scene.scene_sync->get_peer_latency_ms(peer2) == test.peer_1_scene.scene_sync->get_peer_latency_ms(peer2));
	NS_ASSERT_COND(test.server_scene.scene_sync->get_peer_latency_ms(peer1) == test.peer_2_scene.scene_sync->get_peer_latency_ms(peer1));
	NS_ASSERT_COND(test.server_scene.scene_sync->get_peer_latency_ms(peer2) == test.peer_2_scene.scene_sync->get_peer_latency_ms(peer2));

	// Now make sure the latency is below 5 for both, as there is no latency at this point.
	NS_ASSERT_COND(test.server_scene.scene_sync->get_peer_latency_ms(peer1) <= 5);
	NS_ASSERT_COND(test.server_scene.scene_sync->get_peer_latency_ms(peer2) <= 5);

	// TEST 2 with 100 latency
	test.network_properties.rtt_seconds = 0.1f;

	test.do_test(20, true);

	// Make sure the latency is the same between client and the server.
	NS_ASSERT_COND(test.server_scene.scene_sync->get_peer_latency_ms(peer1) == test.peer_1_scene.scene_sync->get_peer_latency_ms(peer1));
	NS_ASSERT_COND(test.server_scene.scene_sync->get_peer_latency_ms(peer2) == test.peer_1_scene.scene_sync->get_peer_latency_ms(peer2));
	NS_ASSERT_COND(test.server_scene.scene_sync->get_peer_latency_ms(peer1) == test.peer_2_scene.scene_sync->get_peer_latency_ms(peer1));
	NS_ASSERT_COND(test.server_scene.scene_sync->get_peer_latency_ms(peer2) == test.peer_2_scene.scene_sync->get_peer_latency_ms(peer2));

	// Now make sure the latency is around 100
	NS_ASSERT_COND(test.server_scene.scene_sync->get_peer_latency_ms(peer1) >= 60 && test.server_scene.scene_sync->get_peer_latency_ms(peer1) <= 105);
	NS_ASSERT_COND(test.server_scene.scene_sync->get_peer_latency_ms(peer2) >= 60 && test.server_scene.scene_sync->get_peer_latency_ms(peer2) <= 105);
}

void test_simulation_with_wrong_input() {
	TestDollSimulationStorePositions test;
	test.frame_confirmation_timespan = 1.0f / 10.0f;
	// NOTICE: Disabling sub ticks because these cause some additional and
	//         difficult to control desync that invalidate this test.
	test.init_test(true);

	const NS::PeerNetworkedController *server_controller_1 = test.server_scene.scene_sync->get_controller_for_peer(test.peer_1_scene.get_peer());
	const NS::PeerNetworkedController *server_controller_2 = test.server_scene.scene_sync->get_controller_for_peer(test.peer_2_scene.get_peer());

	test.do_test(30);

	// 1. Make sure no desync were detected so far.
	NS_ASSERT_COND(test.peer1_desync_detected.size() == 0);
	NS_ASSERT_COND(test.peer2_desync_detected.size() == 0);

	// Ensure the positions are all the same.
	test.assert_positions(NS::FrameIndex{ { 0 } }, NS::FrameIndex{ { 0 } });

	// 2. Now introduce a desync on the server.
	for (int test_count = 0; test_count < 20; test_count++) {
		for (int i = 0; i < 3; i++) {
			const NS::FrameIndex c1_assert_after = server_controller_1->get_current_frame_index() + 70;
			const NS::FrameIndex c2_assert_after = server_controller_2->get_current_frame_index() + 70;
			const size_t c1_desync_vec_size = test.peer1_desync_detected.size();
			const size_t c2_desync_vec_size = test.peer2_desync_detected.size();

			test.controlled_1_serv->modify_input_on_next_frame = true;
			test.controlled_2_serv->modify_input_on_next_frame = true;
			// Process 50 frames and ensure it recovers.
			test.do_test(80);

			// Ensure there was a desync.
			NS_ASSERT_COND(test.peer1_desync_detected.size() > c1_desync_vec_size);
			NS_ASSERT_COND(test.peer2_desync_detected.size() > c2_desync_vec_size);

			// But the position should be the same after frame 60 at least
			test.assert_no_desync(c1_assert_after, c2_assert_after);
			test.assert_positions(c1_assert_after, c2_assert_after);
		}

		if (test_count % 2 == 0) {
			test.network_properties.rtt_seconds = 0.1f;
		} else {
			test.network_properties.rtt_seconds = 0.0f;
		}
	}
}

void test_doll_simulation() {
	test_simulation_without_reconciliation(0.0f);
	test_simulation_without_reconciliation(1.f / 30.f);
	test_simulation_reconciliation(0.0f);
	test_simulation_reconciliation(1.0f / 10.0f);
	test_simulation_with_latency();
	test_simulation_with_hiccups();
	test_simulation_with_wrong_input();
	// TODO test with great latency and lag compensation.
	test_latency();
}
}; //namespace NS_Test