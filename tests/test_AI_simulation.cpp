#include "test_AI_simulation.h"

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
namespace NS_AI_Test {
int frames_per_seconds = 60;

std::shared_ptr<NS::LocalSceneSynchronizerNoSubTicks> SceneSyncNoSubTicks_Obj1;
std::shared_ptr<NS::LocalSceneSynchronizerNoSubTicks> SceneSyncNoSubTicks_Obj2;
std::shared_ptr<NS::LocalSceneSynchronizerNoSubTicks> SceneSyncNoSubTicks_Obj3;

std::shared_ptr<NS::LocalSceneSynchronizer> SceneSync_Obj1;
std::shared_ptr<NS::LocalSceneSynchronizer> SceneSync_Obj2;
std::shared_ptr<NS::LocalSceneSynchronizer> SceneSync_Obj3;

class TAIControlledObject : public NS::LocalSceneObject {
public:
	NS::ObjectLocalId local_id = NS::ObjectLocalId::NONE;
	bool modify_input_on_next_frame = false;
	NS::VarData xy;

	TAIControlledObject() = default;

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
				std::bind(&TAIControlledObject::collect_inputs, this, std::placeholders::_1, std::placeholders::_2),
				std::bind(&TAIControlledObject::are_inputs_different, this, std::placeholders::_1, std::placeholders::_2),
				std::bind(&TAIControlledObject::controller_process, this, std::placeholders::_1, std::placeholders::_2));

		if (p_scene_sync.is_server()) {
			p_scene_sync.set_controlled_by_peer(
					p_id,
					authoritative_peer_id);
		}

		p_scene_sync.register_variable(
				p_id,
				"xy",
				[](NS::SynchronizerManager &p_synchronizer_manager, NS::ObjectHandle p_handle, const std::string &p_var_name, const NS::VarData &p_value) {
					static_cast<TAIControlledObject *>(NS::LocalSceneSynchronizer::from_handle(p_handle))->set_xy(p_value.data.vec.x, p_value.data.vec.y);
				},
				[](const NS::SynchronizerManager &p_synchronizer_manager, NS::ObjectHandle p_handle, const std::string &p_var_name, NS::VarData &r_value) {
					r_value.copy(static_cast<const TAIControlledObject *>(NS::LocalSceneSynchronizer::from_handle(p_handle))->xy);
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
struct TestAISimulationBase {
	std::vector<NS::FrameIndex> peer1_desync_detected;
	std::vector<NS::FrameIndex> peer2_desync_detected;

	NS::LocalNetworkProps network_properties;

	/// Set this to true to ensure the sub_ticks doesn't cause de-syncs.
	bool disable_sub_ticks = false;

	NS::LocalScene server_scene;
	NS::LocalScene peer_1_scene;
	NS::LocalScene peer_2_scene;

	TAIControlledObject *controlled_0_serv = nullptr;
	TAIControlledObject *controlled_0_peer1 = nullptr;
	TAIControlledObject *controlled_0_peer2 = nullptr;

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
	TestAISimulationBase() {
	}

	virtual ~TestAISimulationBase() {
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
					server_scene.add_existing_object(SceneSyncNoSubTicks_Obj1, "sync", server_scene.get_peer());
			peer_1_scene.scene_sync =
					peer_1_scene.add_existing_object(SceneSyncNoSubTicks_Obj2, "sync", server_scene.get_peer());
			peer_2_scene.scene_sync =
					peer_2_scene.add_existing_object(SceneSyncNoSubTicks_Obj3, "sync", server_scene.get_peer());
		} else {
			server_scene.scene_sync =
					server_scene.add_existing_object(SceneSync_Obj1, "sync", server_scene.get_peer());
			peer_1_scene.scene_sync =
					peer_1_scene.add_existing_object(SceneSync_Obj2, "sync", server_scene.get_peer());
			peer_2_scene.scene_sync =
					peer_2_scene.add_existing_object(SceneSync_Obj3, "sync", server_scene.get_peer());
		}

		server_scene.scene_sync->set_frames_per_seconds(frames_per_seconds);
		peer_1_scene.scene_sync->set_frames_per_seconds(frames_per_seconds);
		peer_2_scene.scene_sync->set_frames_per_seconds(frames_per_seconds);

		server_scene.scene_sync->set_frame_confirmation_timespan(frame_confirmation_timespan);

		// Then compose the scene: 3 controllers.
		controlled_0_serv = server_scene.add_object<TAIControlledObject>("controller_0", server_scene.get_peer());
		controlled_0_peer1 = peer_1_scene.add_object<TAIControlledObject>("controller_0", server_scene.get_peer());
		controlled_0_peer2 = peer_2_scene.add_object<TAIControlledObject>("controller_0", server_scene.get_peer());

		server_scene.add_object<TAIControlledObject>("controller_1", peer_1_scene.get_peer());
		peer_1_scene.add_object<TAIControlledObject>("controller_1", peer_1_scene.get_peer());
		peer_2_scene.add_object<TAIControlledObject>("controller_1", peer_1_scene.get_peer());

		server_scene.add_object<TAIControlledObject>("controller_2", peer_2_scene.get_peer());
		peer_1_scene.add_object<TAIControlledObject>("controller_2", peer_2_scene.get_peer());
		peer_2_scene.add_object<TAIControlledObject>("controller_2", peer_2_scene.get_peer());

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
		controlled_0_serv->set_xy(-100., 0.);
		controlled_0_peer1->set_xy(-100., 0.);
		controlled_0_peer2->set_xy(-100., 0.);

		on_scenes_initialized();
	}

	float rand_range(float M, float N) {
		return float(M + (float(rand()) / (float(RAND_MAX) / (N - M))));
	}

	void do_test(const int p_frames_count, bool p_wait_for_time_pass = false, bool p_process_server = true, bool p_process_peer1 = true, bool p_process_peer2 = true) {
		NS_ASSERT_COND(server_scene.scene_sync->get_frames_per_seconds()==peer_1_scene.scene_sync->get_frames_per_seconds());
		NS_ASSERT_COND(server_scene.scene_sync->get_frames_per_seconds()==peer_2_scene.scene_sync->get_frames_per_seconds());

		for (int i = 0; i < p_frames_count; i++) {
			float sim_delta = server_scene.scene_sync->get_fixed_frame_delta();
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

struct TestAISimulationWithPositionCheck : public TestAISimulationBase {
	virtual void on_scenes_initialized() override {
		// Ensure the controllers are at their initial location as defined by the doll simulation class.
		NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_0_serv->get_xy(), NS::VarData(-100., 0.)));
		NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_0_peer1->get_xy(), NS::VarData(-100., 0.)));
		NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(controlled_0_peer2->get_xy(), NS::VarData(-100., 0.)));
	}

	std::vector<NS::VarData> controlled_0_positions_on_server;
	std::vector<NS::VarData> controlled_0_positions_on_peer_1;
	std::vector<NS::VarData> controlled_0_positions_on_peer_2;

	virtual void on_server_process(float p_delta) override {
		// Nothing to do.
		const NS::FrameIndex frame_index = server_scene.scene_sync->get_controller_for_peer(server_scene.get_peer())->get_current_frame_index();
		if (controlled_0_positions_on_server.size() <= frame_index.id) {
			controlled_0_positions_on_server.resize(frame_index.id + 1);
		}
		controlled_0_positions_on_server[frame_index.id] = controlled_0_serv->get_xy();
	}

	virtual void on_client_1_process(float p_delta) override {
		// Store the player 1 inputs.
		const NS::FrameIndex frame_index = peer_1_scene.scene_sync->get_controller_for_peer(server_scene.get_peer())->get_current_frame_index();
		if (frame_index != NS::FrameIndex::NONE) {
			if (controlled_0_positions_on_peer_1.size() <= frame_index.id) {
				controlled_0_positions_on_peer_1.resize(frame_index.id + 1);
			}
			controlled_0_positions_on_peer_1[frame_index.id] = controlled_0_peer1->get_xy();
		}
	}

	virtual void on_client_2_process(float p_delta) override {
		// Store the player 2 inputs.
		const NS::FrameIndex frame_index = peer_2_scene.scene_sync->get_controller_for_peer(server_scene.get_peer())->get_current_frame_index();
		if (frame_index != NS::FrameIndex::NONE) {
			if (controlled_0_positions_on_peer_2.size() <= frame_index.id) {
				controlled_0_positions_on_peer_2.resize(frame_index.id + 1);
			}
			controlled_0_positions_on_peer_2[frame_index.id] = controlled_0_peer2->get_xy();
		}
	}

	virtual void on_scenes_processed(float p_delta) override {
		NS_ASSERT_COND(peer1_desync_detected.size() == 0);
		NS_ASSERT_COND(peer2_desync_detected.size() == 0);

		const NS::FrameIndex controller_0_frame_index_on_server = server_scene.scene_sync->get_controller_for_peer(server_scene.get_peer())->get_current_frame_index();
		const NS::FrameIndex controller_0_frame_index_on_peer1 = peer_1_scene.scene_sync->get_controller_for_peer(server_scene.get_peer())->get_current_frame_index();
		const NS::FrameIndex controller_0_frame_index_on_peer2 = peer_2_scene.scene_sync->get_controller_for_peer(server_scene.get_peer())->get_current_frame_index();

		// The server start processing the controllers right away, so this is supposed to be always false.
		NS_ASSERT_COND(controller_0_frame_index_on_server != NS::FrameIndex::NONE);

		const NS::VarData &server_0_position = controlled_0_positions_on_server[controller_0_frame_index_on_server.id];

		if (controller_0_frame_index_on_peer1 != NS::FrameIndex::NONE) {
			// Make sure the players are always ahead the dolls.
			NS_ASSERT_COND(controller_0_frame_index_on_server >= controller_0_frame_index_on_peer1);

			// Verify the doll are at the exact location they were on the player.
			const NS::VarData doll_0_position = controlled_0_peer1->get_xy();
			NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(server_0_position, doll_0_position));
		}

		if (controller_0_frame_index_on_peer2 != NS::FrameIndex::NONE) {
			// Make sure the players are always ahead the dolls.
			NS_ASSERT_COND(controller_0_frame_index_on_server >= controller_0_frame_index_on_peer2);

			// Verify the doll are at the exact location they were on the player.
			const NS::VarData doll_0_position = controlled_0_peer2->get_xy();
			NS_ASSERT_COND(NS::LocalSceneSynchronizer::var_data_compare(server_0_position, doll_0_position));
		}
	}
};

// Test the ability to process a doll without causing any
// reconciliation or miss any input.
void test_AI_replication(float p_frame_confirmation_timespan) {
	TestAISimulationWithPositionCheck test;
	test.frame_confirmation_timespan = p_frame_confirmation_timespan;

	// NOTICE: Disabling sub ticks because these cause some desync
	//         desync that invalidate this test.
	test.init_test(true);

	test.do_test(100);

	NS_ASSERT_COND(test.peer1_desync_detected.size() == 0);
	NS_ASSERT_COND(test.peer2_desync_detected.size() == 0);
	NS_ASSERT_COND(test.controlled_0_positions_on_server.size() >= 100);
	NS_ASSERT_COND(test.controlled_0_positions_on_peer_1.size() > 90);
	NS_ASSERT_COND(test.controlled_0_positions_on_peer_2.size() > 90);
}

void test_AI_simulation() {
	SceneSyncNoSubTicks_Obj1 = std::make_shared<NS::LocalSceneSynchronizerNoSubTicks>();
	SceneSyncNoSubTicks_Obj2 = std::make_shared<NS::LocalSceneSynchronizerNoSubTicks>();
	SceneSyncNoSubTicks_Obj3 = std::make_shared<NS::LocalSceneSynchronizerNoSubTicks>();
	SceneSync_Obj1 = std::make_shared<NS::LocalSceneSynchronizer>();
	SceneSync_Obj2 = std::make_shared<NS::LocalSceneSynchronizer>();
	SceneSync_Obj3 = std::make_shared<NS::LocalSceneSynchronizer>();

	const int initial_frames_per_seconds = frames_per_seconds;
	for (int i = 0; i < 2; i++) {
		test_AI_replication(0.0f);
		frames_per_seconds *= 2;
	}

	frames_per_seconds = initial_frames_per_seconds;

	SceneSyncNoSubTicks_Obj1->clear_scene();
	SceneSyncNoSubTicks_Obj2->clear_scene();
	SceneSyncNoSubTicks_Obj3->clear_scene();
	SceneSync_Obj1->clear_scene();
	SceneSync_Obj2->clear_scene();
	SceneSync_Obj3->clear_scene();

	SceneSyncNoSubTicks_Obj1 = std::shared_ptr<NS::LocalSceneSynchronizerNoSubTicks>();
	SceneSyncNoSubTicks_Obj2 = std::shared_ptr<NS::LocalSceneSynchronizerNoSubTicks>();
	SceneSyncNoSubTicks_Obj3 = std::shared_ptr<NS::LocalSceneSynchronizerNoSubTicks>();
	SceneSync_Obj1 = std::shared_ptr<NS::LocalSceneSynchronizer>();
	SceneSync_Obj2 = std::shared_ptr<NS::LocalSceneSynchronizer>();
	SceneSync_Obj3 = std::shared_ptr<NS::LocalSceneSynchronizer>();
}
};
};