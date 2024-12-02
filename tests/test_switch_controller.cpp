#include "test_simulation.h"

#include "../core/core.h"
#include "../core/data_buffer.h"
#include "../core/ensure.h"
#include "../core/net_math.h"
#include "../core/net_utilities.h"
#include "../core/processor.h"
#include "../core/var_data.h"
#include "../core/scene_synchronizer_debugger.h"
#include "local_network.h"
#include "local_scene.h"
#include "test_math_lib.h"
#include <string>

namespace NS_Test {
const float delta = 1.0f / 60.0f;

class FeatherSceneObject : public NS::LocalSceneObject {
public:
	NS::ObjectLocalId local_id = NS::ObjectLocalId::NONE;

	Vec3 position;
	Vec3 velocity;

	virtual void on_scene_entry() override {
		set_position(Vec3());
		set_velocity(Vec3());

		if (get_scene()->scene_sync->is_server()) {
			get_scene()->scene_sync->register_app_object(get_scene()->scene_sync->to_handle(this));
		}
	}

	virtual void setup_synchronizer(NS::LocalSceneSynchronizer &p_scene_sync, NS::ObjectLocalId p_id) override {
		local_id = p_id;

		p_scene_sync.register_variable(
				p_id, "position",
				[](NS::SynchronizerManager &p_synchronizer_manager, NS::ObjectHandle p_handle, const std::string &p_var_name, const NS::VarData &p_value) {
					static_cast<FeatherSceneObject *>(NS::LocalSceneSynchronizer::from_handle(p_handle))->position = Vec3::from(p_value);
				},
				[](const NS::SynchronizerManager &p_synchronizer_manager, NS::ObjectHandle p_handle, const std::string &p_var_name, NS::VarData &r_value) {
					r_value = static_cast<const FeatherSceneObject *>(NS::LocalSceneSynchronizer::from_handle(p_handle))->position;
				});

		p_scene_sync.register_variable(
				p_id, "velocity",
				[](NS::SynchronizerManager &p_synchronizer_manager, NS::ObjectHandle p_handle, const std::string &p_var_name, const NS::VarData &p_value) {
					static_cast<FeatherSceneObject *>(NS::LocalSceneSynchronizer::from_handle(p_handle))->velocity = Vec3::from(p_value);
				},
				[](const NS::SynchronizerManager &p_synchronizer_manager, NS::ObjectHandle p_handle, const std::string &p_var_name, NS::VarData &r_value) {
					r_value = static_cast<const FeatherSceneObject *>(NS::LocalSceneSynchronizer::from_handle(p_handle))->velocity;
				});
	}

	virtual void on_scene_exit() override {
		get_scene()->scene_sync->on_app_object_removed(get_scene()->scene_sync->to_handle(this));
	}

	void set_position(const Vec3 &p_pos) {
		position = p_pos;
	}

	Vec3 get_position() const {
		return position;
	}

	void set_velocity(const Vec3 &p_vel) {
		velocity = p_vel;
	}

	Vec3 get_velocity() const {
		return velocity;
	}
};

class FeatherPlayerController : public NS::LocalSceneObject {
public:
	std::function<void()> on_feather_controller_switched;
	NS::ObjectLocalId local_id = NS::ObjectLocalId::NONE;
	Vec3 position;

	FeatherPlayerController() = default;

	virtual void on_scene_entry() override {
		set_position(Vec3());

		get_scene()->scene_sync->register_app_object(get_scene()->scene_sync->to_handle(this));
	}

	virtual void on_scene_exit() override {
		get_scene()->scene_sync->unregister_app_object(local_id);
	}

	virtual void setup_synchronizer(NS::LocalSceneSynchronizer &p_scene_sync, NS::ObjectLocalId p_id) override {
		local_id = p_id;

		p_scene_sync.setup_controller(
				p_id,
				[this](float p_delta, NS::DataBuffer &r_buffer) -> void {
					collect_inputs(p_delta, r_buffer);
				},
				[this](NS::DataBuffer &p_buffer_A, NS::DataBuffer &p_buffer_b) -> bool {
					return are_inputs_different(p_buffer_A, p_buffer_b);
				},
				[this](float p_delta, NS::DataBuffer &p_buffer) -> void {
					controller_process(p_delta, p_buffer);
				});

		p_scene_sync.set_controlled_by_peer(p_id, authoritative_peer_id);

		p_scene_sync.register_variable(
				p_id, "position",
				[](NS::SynchronizerManager &p_synchronizer_manager, NS::ObjectHandle p_handle, const std::string &p_var_name, const NS::VarData &p_value) {
					static_cast<FeatherPlayerController *>(NS::LocalSceneSynchronizer::from_handle(p_handle))->position = Vec3::from(p_value);
				},
				[](const NS::SynchronizerManager &p_synchronizer_manager, NS::ObjectHandle p_handle, const std::string &p_var_name, NS::VarData &r_value) {
					r_value = static_cast<const FeatherPlayerController *>(NS::LocalSceneSynchronizer::from_handle(p_handle))->position;
				});
	}

	void set_position(const Vec3 &p_pos) {
		position = p_pos;
	}

	Vec3 get_position() const {
		return position;
	}

	// ------------------------------------------------- NetController interface
	bool move_feather_inputs[20] = {
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
	};

	const Vec3 inputs[20] = {
		Vec3(1.0f, 0.0f, 0.0f),
		Vec3(0.0f, 1.0f, 0.0f),
		Vec3(0.0f, 0.0f, 1.0f),
		Vec3(0.0f, 1.0f, 0.0f),
		Vec3(1.0f, 0.0f, 0.0f),
		Vec3(1.0f, 0.0f, 0.0f),
		Vec3(1.0f, 0.0f, 0.0f),
		Vec3(0.0f, 0.0f, 1.0f),
		Vec3(0.0f, 0.0f, 1.0f),
		Vec3(0.0f, 0.0f, 1.0f),
		Vec3(0.0f, 1.0f, 0.0f),
		Vec3(1.0f, 0.0f, 0.0f),
		Vec3(0.0f, 0.0f, 1.0f),
		Vec3(1.0f, 0.0f, 0.0f),
		Vec3(1.0f, 0.0f, 0.0f),
		Vec3(1.0f, 0.0f, 0.0f),
		Vec3(1.0f, 0.0f, 0.0f),
		Vec3(1.0f, 0.0f, 0.0f),
		Vec3(1.0f, 0.0f, 0.0f),
		Vec3(0.0f, 1.0f, 0.0f)
	};

	void collect_inputs(float p_delta, NS::DataBuffer &r_buffer) {
		const NS::FrameIndex current_frame_index = scene_owner->scene_sync->get_controller_for_peer(authoritative_peer_id)->get_current_frame_index();
		const int index = current_frame_index.id % 20;
		r_buffer.add_normalized_vector3(inputs[index].x, inputs[index].y, inputs[index].z, NS::DataBuffer::COMPRESSION_LEVEL_3);
		r_buffer.add(move_feather_inputs[index]);
	}

	void controller_process(float p_delta, NS::DataBuffer &p_buffer) {
		NS_ASSERT_COND(p_delta == delta);
		const float speed = 1.0;
		Vec3 input;
		p_buffer.read_normalized_vector3(input.x, input.y, input.z, NS::DataBuffer::COMPRESSION_LEVEL_3);
		set_position(get_position() + (input * speed * p_delta));

		bool move_feather;
		p_buffer.read(move_feather);

		if (move_feather) {
			FeatherSceneObject *feather = scene_owner->fetch_object<FeatherSceneObject>("feather_1");
			feather->set_position(get_position());
			feather->set_velocity(input * 20.0f);

			// Switch the control to the peer controlling this object now.
			// In this way the feather is now part of the timeline of the last
			// peer touching it.
			scene_owner->scene_sync->set_controlled_by_peer(feather->local_id, authoritative_peer_id);

			if (on_feather_controller_switched) {
				on_feather_controller_switched();
			}
		}
	}

	bool are_inputs_different(NS::DataBuffer &p_buffer_A, NS::DataBuffer &p_buffer_B) {
		Vec3 A;
		Vec3 B;
		p_buffer_A.read_normalized_vector3(A.x, A.y, A.z, NS::DataBuffer::COMPRESSION_LEVEL_3);
		p_buffer_B.read_normalized_vector3(B.x, B.y, B.z, NS::DataBuffer::COMPRESSION_LEVEL_3);

		if (
			!NS::MathFunc::is_equal_approx(A.x, B.x) ||
			!NS::MathFunc::is_equal_approx(A.y, B.y) ||
			!NS::MathFunc::is_equal_approx(A.z, B.z)) {
			return true;
		}

		bool move_feather_A;
		bool move_feather_B;
		p_buffer_A.read(move_feather_A);
		p_buffer_B.read(move_feather_B);

		if (move_feather_A != move_feather_B) {
			return true;
		}

		return false;
	}
};

void process_movable_feathers_simulation(NS::LocalSceneSynchronizer &scene_sync, float p_delta) {
	for (const NS::ObjectData *od : scene_sync.get_sorted_objects_data()) {
		if (!od) {
			continue;
		}

		NS::LocalSceneObject *lso = scene_sync.from_handle(od->app_object_handle);
		FeatherSceneObject *fso = dynamic_cast<FeatherSceneObject *>(lso);
		if (fso) {
			fso->set_position(fso->get_position() + (fso->get_velocity() * p_delta));
		}
	}
}

/// This class is responsible to verify that is possible to change the peer
/// controlling an objects from frame to frame without causing de-sync.
struct TestSwitchControllerBase {
	NS::LocalScene server_scene;
	NS::LocalScene peer_1_scene;
	NS::LocalScene peer_2_scene;

	FeatherPlayerController *player_controlled_object_1_server = nullptr;
	FeatherPlayerController *player_controlled_object_1_p1 = nullptr;
	FeatherPlayerController *player_controlled_object_1_p2 = nullptr;

	FeatherPlayerController *player_controlled_object_2_server = nullptr;
	FeatherPlayerController *player_controlled_object_2_p1 = nullptr;
	FeatherPlayerController *player_controlled_object_2_p2 = nullptr;

	NS::PeerNetworkedController *controller_p1_server = nullptr;
	NS::PeerNetworkedController *controller_p1_p1 = nullptr;

	NS::PeerNetworkedController *controller_p2_server = nullptr;
	NS::PeerNetworkedController *controller_p2_p2 = nullptr;

	NS::FrameIndex process_until_frame = NS::FrameIndex{ { 300 } };
	int process_until_frame_timeout = 20;

	std::vector<NS::FrameIndex> server_switched_controller_on_frame_for_p1;
	std::vector<NS::FrameIndex> server_switched_controller_on_frame_for_p2;

	virtual void on_scenes_initialized() {
	}

	virtual void on_server_process(float p_delta) {
	}

	virtual void on_client_p1_process(float p_delta) {
	}

	virtual void on_client_p2_process(float p_delta) {
	}

	virtual void on_scenes_processed(float p_delta) {
	}

	virtual void on_scenes_done() {
	}

	void notify_feather_player_controller_switched() {
	}

public:
	TestSwitchControllerBase() {
	}

	virtual ~TestSwitchControllerBase() {
	}

	float rand_range(float M, float N) {
		return M + (rand() / (float(RAND_MAX) / (N - M)));
	}

	void do_test() {
		// Create a server
		server_scene.start_as_server();

		// Add client 1 connected to the server.
		peer_1_scene.start_as_client(server_scene);

		// Add client 2 connected to the server.
		peer_2_scene.start_as_client(server_scene);

		// Add the scene sync
		server_scene.scene_sync =
				server_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
		peer_1_scene.scene_sync =
				peer_1_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
		peer_2_scene.scene_sync =
				peer_2_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());

		// TODO remove this.
		/*
		peer_1_scene.scene_sync->set_debug_rewindings_enabled(true);

		server_scene.scene_sync->get_debugger().set_log_level(NS::ERROR);
		//peer_1_scene.scene_sync->get_debugger().set_log_level(NS::ERROR);
		peer_1_scene.scene_sync->get_debugger().set_log_level(NS::VERBOSE);
		peer_2_scene.scene_sync->get_debugger().set_log_level(NS::ERROR);
		*/

		server_scene.scene_sync->get_debugger().set_log_prefix("SERVER");
		peer_1_scene.scene_sync->get_debugger().set_log_prefix("PEER-1");
		peer_2_scene.scene_sync->get_debugger().set_log_prefix("PEER-2");

		// Then compose the scene.
		player_controlled_object_1_server = server_scene.add_object<FeatherPlayerController>("controller_1", peer_1_scene.get_peer());
		player_controlled_object_1_p1 = peer_1_scene.add_object<FeatherPlayerController>("controller_1", peer_1_scene.get_peer());
		player_controlled_object_1_p2 = peer_2_scene.add_object<FeatherPlayerController>("controller_1", peer_1_scene.get_peer());

		player_controlled_object_2_server = server_scene.add_object<FeatherPlayerController>("controller_2", peer_2_scene.get_peer());
		player_controlled_object_2_p1 = peer_1_scene.add_object<FeatherPlayerController>("controller_2", peer_2_scene.get_peer());
		player_controlled_object_2_p2 = peer_2_scene.add_object<FeatherPlayerController>("controller_2", peer_2_scene.get_peer());

		player_controlled_object_1_server->on_feather_controller_switched = [this]() {
			NS::PeerNetworkedController *pc = server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer());
			NS_ASSERT_COND(pc);
			server_switched_controller_on_frame_for_p1.push_back(pc->get_current_frame_index());
		};

		player_controlled_object_2_server->on_feather_controller_switched = [this]() {
			NS::PeerNetworkedController *pc = server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer());
			NS_ASSERT_COND(pc);
			server_switched_controller_on_frame_for_p2.push_back(pc->get_current_frame_index());
		};

		controller_p1_server = server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer());
		controller_p1_p1 = peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer());

		controller_p2_server = server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer());
		controller_p2_p2 = peer_2_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer());

		// Register the process
		server_scene.scene_sync->register_process(server_scene.scene_sync->local_id, PROCESS_PHASE_POST, [=](float p_delta) -> void {
			process_movable_feathers_simulation(*server_scene.scene_sync, p_delta);
		});
		peer_1_scene.scene_sync->register_process(peer_1_scene.scene_sync->local_id, PROCESS_PHASE_POST, [=](float p_delta) -> void {
			process_movable_feathers_simulation(*peer_1_scene.scene_sync, p_delta);
		});
		peer_2_scene.scene_sync->register_process(peer_2_scene.scene_sync->local_id, PROCESS_PHASE_POST, [=](float p_delta) -> void {
			process_movable_feathers_simulation(*peer_2_scene.scene_sync, p_delta);
		});

		server_scene.scene_sync->register_process(server_scene.scene_sync->local_id, PROCESS_PHASE_LATE, [=](float p_delta) -> void {
			on_server_process(p_delta);
		});
		peer_1_scene.scene_sync->register_process(peer_1_scene.scene_sync->local_id, PROCESS_PHASE_LATE, [=](float p_delta) -> void {
			on_client_p1_process(p_delta);
		});
		peer_2_scene.scene_sync->register_process(peer_2_scene.scene_sync->local_id, PROCESS_PHASE_LATE, [=](float p_delta) -> void {
			on_client_p2_process(p_delta);
		});

		on_scenes_initialized();

		// Set the weight of each controlled object:
		player_controlled_object_1_server->set_position(Vec3(1.0, 1.0, 1.0));
		player_controlled_object_1_p1->set_position(Vec3(1.0, 1.0, 1.0));
		player_controlled_object_1_p2->set_position(Vec3(1.0, 1.0, 1.0));

		player_controlled_object_2_server->set_position(Vec3(-1.0, -1.0, -1.0));
		player_controlled_object_2_p1->set_position(Vec3(-1.0, -1.0, -1.0));
		player_controlled_object_2_p2->set_position(Vec3(-1.0, -1.0, -1.0));

		FeatherSceneObject *feather_server = server_scene.add_object<FeatherSceneObject>("feather_1", server_scene.get_peer());
		FeatherSceneObject *feather_p1 = peer_1_scene.add_object<FeatherSceneObject>("feather_1", server_scene.get_peer());
		FeatherSceneObject *feather_p2 = peer_2_scene.add_object<FeatherSceneObject>("feather_1", server_scene.get_peer());

		feather_server->set_position(Vec3(2.0, 1.0, 1.0));
		feather_p1->set_position(Vec3(2.0, 1.0, 1.0));
		feather_p2->set_position(Vec3(2.0, 1.0, 1.0));

		feather_server->set_position(Vec3(0.0, 0.0, 0.0));
		feather_p1->set_velocity(Vec3(0.0, 0.0, 0.0));
		feather_p2->set_velocity(Vec3(0.0, 0.0, 0.0));

		bool server_reached_target_frame = false;
		bool p1_reached_target_frame = false;
		bool p2_reached_target_frame = false;

		while (true) {
			// Use a random delta, to make sure the NetSync can be processed
			// by a normal process loop with dynamic `delta_time`.
			const float rand_delta = rand_range(0.005f, delta);
			server_scene.process(rand_delta);
			peer_1_scene.process(rand_delta);
			peer_2_scene.process(rand_delta);

			on_scenes_processed(rand_delta);

			if (controller_p1_server->get_current_frame_index() == process_until_frame) {
				server_reached_target_frame = true;
			}
			if (controller_p2_server->get_current_frame_index() == process_until_frame) {
				server_reached_target_frame = true;
			}
			if (controller_p1_p1->get_current_frame_index() == process_until_frame) {
				p1_reached_target_frame = true;
			}
			if (controller_p2_p2->get_current_frame_index() == process_until_frame) {
				p2_reached_target_frame = true;
			}

			if (server_reached_target_frame && p1_reached_target_frame && p2_reached_target_frame) {
				break;
			}

			if (controller_p1_server->get_current_frame_index() != NS::FrameIndex::NONE) {
				NS_ASSERT_COND(controller_p1_server->get_current_frame_index() < (process_until_frame + process_until_frame_timeout));
			}
			if (controller_p1_p1->get_current_frame_index() != NS::FrameIndex::NONE) {
				NS_ASSERT_COND(controller_p1_p1->get_current_frame_index() < (process_until_frame + process_until_frame_timeout));
			}
			if (controller_p2_p2->get_current_frame_index() != NS::FrameIndex::NONE) {
				NS_ASSERT_COND(controller_p2_p2->get_current_frame_index() < (process_until_frame + process_until_frame_timeout));
			}
		}

		on_scenes_done();
	}
};

struct TestSwitchControllerNoRewind : public TestSwitchControllerBase {
	float notify_state_interval = 0.0f;

public:
	std::vector<NS::FrameIndex> p1_rewinded_frames;
	std::vector<NS::FrameIndex> p2_rewinded_frames;
	// The ID of snapshot sent by the server.
	NS::FrameIndex p1_correction_snapshot_sent = NS::FrameIndex{ { 0 } };
	NS::FrameIndex p2_correction_snapshot_sent = NS::FrameIndex{ { 0 } };

	TestSwitchControllerNoRewind(float p_notify_state_interval) :
		notify_state_interval(p_notify_state_interval) {
	}

	virtual void on_scenes_initialized() override {
		TestSwitchControllerBase::on_scenes_initialized();

		server_scene.scene_sync->set_frame_confirmation_timespan(notify_state_interval);
		// Make sure the client can predict as many frames it needs (no need to add some more noise to this test).
		server_scene.scene_sync->set_max_predicted_intervals(20);

		controller_p1_server->event_input_missed.bind([](NS::FrameIndex p_frame_index) {
			// The input should be never missing!
			NS_ASSERT_NO_ENTRY();
		});

		controller_p2_server->event_input_missed.bind([](NS::FrameIndex p_frame_index) {
			// The input should be never missing!
			NS_ASSERT_NO_ENTRY();
		});

		controller_p1_p1->get_scene_synchronizer()->event_state_validated.bind([this](NS::FrameIndex p_frame_index, bool p_desync) {
			if (p_desync) {
				p1_rewinded_frames.push_back(p_frame_index);
			}
		});

		controller_p2_p2->get_scene_synchronizer()->event_state_validated.bind([this](NS::FrameIndex p_frame_index, bool p_desync) {
			if (p_desync) {
				p2_rewinded_frames.push_back(p_frame_index);
			}
		});
	}

	virtual void on_server_process(float p_delta) override {
	}

	virtual void on_scenes_done() override {
	}
};

// The object controller switch is performed only on one client.
struct TestSwitchControllerNoRewindSingleSwitch : public TestSwitchControllerNoRewind {
	TestSwitchControllerNoRewindSingleSwitch(float p_notify_state_interval) :
		TestSwitchControllerNoRewind(p_notify_state_interval) {
	}

	virtual void on_scenes_initialized() override {
		TestSwitchControllerNoRewind::on_scenes_initialized();

		player_controlled_object_2_p2->move_feather_inputs[9] = true;
		player_controlled_object_2_p2->move_feather_inputs[19] = true;
	}

	virtual void on_server_process(float p_delta) override {
	}

	virtual void on_scenes_done() override {
		// Since the player controller owned by the peer 2 is switching the controller
		// some rewinds can be triggered. At most 3 rewinds are tolerated.
		NS_ASSERT_COND(p1_rewinded_frames.size() <= 3);
		NS_ASSERT_COND(p2_rewinded_frames.size() <= 3);

		NS_ASSERT_COND(server_switched_controller_on_frame_for_p1.size() <= 0);
		NS_ASSERT_COND(server_switched_controller_on_frame_for_p2.size() > 5);
	}
};

// TODO this is not fully implemented yet.
// The object controller switch is performed only two clients one after the other
// multiple times.
struct TestSwitchControllerNoRewindMultipleSwitch : public TestSwitchControllerNoRewind {
	TestSwitchControllerNoRewindMultipleSwitch(float p_notify_state_interval) :
		TestSwitchControllerNoRewind(p_notify_state_interval) {
	}

	virtual void on_scenes_initialized() override {
		TestSwitchControllerNoRewind::on_scenes_initialized();

		player_controlled_object_1_p1->move_feather_inputs[4] = true;
		player_controlled_object_1_p1->move_feather_inputs[14] = true;

		player_controlled_object_2_p2->move_feather_inputs[9] = true;
		player_controlled_object_2_p2->move_feather_inputs[19] = true;
	}

	virtual void on_server_process(float p_delta) override {
	}

	virtual void on_scenes_done() override {
		// TODO find a way to check this.
		//NS_ASSERT_COND(server_switched_controller_on_frame_for_p1.size() > 1);
		//NS_ASSERT_COND(server_switched_controller_on_frame_for_p2.size() > 1);

		// Since the player controller owned by the peer 2 is switching the controller
		// only 1 rewind at most is expected on peer 1.
		NS_ASSERT_COND(p1_rewinded_frames.size()<=1);
		NS_ASSERT_COND(p2_rewinded_frames.empty());
	}
};

void test_switch_controller() {
	TestSwitchControllerNoRewindSingleSwitch(0.0f).do_test();
	TestSwitchControllerNoRewindSingleSwitch(0.1f).do_test();
	TestSwitchControllerNoRewindSingleSwitch(0.5f).do_test();
	TestSwitchControllerNoRewindSingleSwitch(1.0f).do_test();
}
}; //namespace NS_Test