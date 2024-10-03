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
		peer_1_scene.scene_sync->set_debug_rewindings_enabled(true);

		// TODO remove
		server_scene.scene_sync->get_debugger().set_log_level(NS::ERROR);
		//peer_1_scene.scene_sync->get_debugger().set_log_level(NS::ERROR);
		peer_1_scene.scene_sync->get_debugger().set_log_level(NS::VERBOSE);
		peer_2_scene.scene_sync->get_debugger().set_log_level(NS::ERROR);

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

		player_controlled_object_1_p1->move_feather_inputs[4] = true;
		player_controlled_object_1_p1->move_feather_inputs[14] = true;

		//player_controlled_object_2_p2->move_feather_inputs[9] = true;
		//player_controlled_object_2_p2->move_feather_inputs[19] = true;

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

		Vec3 controller_1_position_on_server_at_target_frame;
		Vec3 controller_1_position_on_p1_at_target_frame;
		Vec3 controller_1_position_on_p2_at_target_frame;

		Vec3 controller_2_position_on_server_at_target_frame;
		Vec3 controller_2_position_on_p1_at_target_frame;
		Vec3 controller_2_position_on_p2_at_target_frame;

		Vec3 feather_position_on_server_at_target_frame;
		Vec3 feather_position_on_p1_at_target_frame;
		Vec3 feather_position_on_p2_at_target_frame;

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
				controller_1_position_on_server_at_target_frame = player_controlled_object_1_server->get_position();
				feather_position_on_server_at_target_frame = feather_server->get_position();
			}
			if (controller_p2_server->get_current_frame_index() == process_until_frame) {
				server_reached_target_frame = true;
				controller_2_position_on_server_at_target_frame = player_controlled_object_2_server->get_position();
				feather_position_on_server_at_target_frame = feather_server->get_position();
			}
			if (controller_p1_p1->get_current_frame_index() == process_until_frame) {
				p1_reached_target_frame = true;
				controller_1_position_on_p1_at_target_frame = player_controlled_object_1_p1->get_position();
				feather_position_on_p1_at_target_frame = feather_p1->get_position();
			}
			if (controller_p2_p2->get_current_frame_index() == process_until_frame) {
				p2_reached_target_frame = true;
				controller_2_position_on_p2_at_target_frame = player_controlled_object_2_p2->get_position();
				feather_position_on_p2_at_target_frame = feather_p2->get_position();
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

		// TODO implement this.
		//                  ---- Validation phase ----
		// First make sure all positions have changed at all.
		//NS_ASSERT_COND(player_controlled_object_1_server->get_position().distance_to(Vec3(1, 1, 1)) > 0.0001);

		// Now, make sure the client and server positions are the same: ensuring the
		// sync worked.
		//NS_ASSERT_COND(controller_position_on_server_at_target_frame.distance_to(controller_position_on_p1_at_target_frame) < 0.0001);
		//NS_ASSERT_COND(feather_position_on_server_at_target_frame.distance_to(feather_position_on_p1_at_target_frame) < 0.0001);
		//NS_ASSERT_COND(heavy_mag_server_position_at_target_frame.distance_to(heavy_mag_p1_position_at_target_frame) < 0.0001);

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
		NS_ASSERT_COND(p1_rewinded_frames.empty());
		NS_ASSERT_COND(p2_rewinded_frames.empty());
	}
};

/// This test was build to verify that the NetSync is able to immediately re-sync
/// a scene even when switching the object authority.
/// It manually de-sync the server by teleporting the controller, and then
/// make sure the client was immediately re-sync with a single rewinding action.
struct TestSwitchControllerWithRewind : public TestSwitchControllerBase {
	NS::FrameIndex reset_position_on_frame = NS::FrameIndex{ { 100 } };
	float notify_state_interval = 0.0f;

public:
	std::vector<NS::FrameIndex> p1_rewinded_frames;
	std::vector<NS::FrameIndex> p2_rewinded_frames;
	// The ID of snapshot sent by the server.
	NS::FrameIndex p1_correction_snapshot_sent = NS::FrameIndex{ { 0 } };
	NS::FrameIndex p2_correction_snapshot_sent = NS::FrameIndex{ { 0 } };

	TestSwitchControllerWithRewind(float p_notify_state_interval) :
		notify_state_interval(p_notify_state_interval) {
	}

	virtual void on_scenes_initialized() override {
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
		if (controller_p1_server->get_current_frame_index() == reset_position_on_frame) {
			// Reset the character position only on the server, to simulate a desync.
			player_controlled_object_1_server->set_position(Vec3(-10.0, 10.0, 0.0));

			server_scene.scene_sync->event_sent_snapshot.bind([this](NS::FrameIndex p_frame_index, int p_peer) {
				p1_correction_snapshot_sent = p_frame_index;

				// Make sure this function is not called once again.
				server_scene.scene_sync->event_sent_snapshot.clear();
			});
		}

		if (controller_p2_server->get_current_frame_index() == reset_position_on_frame) {
			// Reset the character position only on the server, to simulate a desync.
			player_controlled_object_2_server->set_position(Vec3(100.0, -100.0, 0.0));

			server_scene.scene_sync->event_sent_snapshot.bind([this](NS::FrameIndex p_frame_index, int p_peer) {
				p2_correction_snapshot_sent = p_frame_index;

				// Make sure this function is not called once again.
				server_scene.scene_sync->event_sent_snapshot.clear();
			});
		}
	}

	virtual void on_scenes_done() override {
		NS_ASSERT_COND(p1_rewinded_frames.size() == 1);
		NS_ASSERT_COND(p1_rewinded_frames[0] >= reset_position_on_frame);
		NS_ASSERT_COND(p1_rewinded_frames[0] == p1_correction_snapshot_sent);

		NS_ASSERT_COND(p2_rewinded_frames.size() == 1);
		NS_ASSERT_COND(p2_rewinded_frames[0] >= reset_position_on_frame);
		NS_ASSERT_COND(p2_rewinded_frames[0] == p2_correction_snapshot_sent);
	}
};

/// This test validates the Partial Update feature.
/// It sets the controller as partially Updating each frame and ensure that the
/// controller is re-sync in exactly 1 frame even when the sync rate is set to 1.0 seconds.
struct TestSwitchControllerWithRewindAndPartialUpdate : public TestSwitchControllerWithRewind {
	TestSwitchControllerWithRewindAndPartialUpdate(float p_notify_state_interval) :
		TestSwitchControllerWithRewind(p_notify_state_interval) {
	}

	virtual void on_scenes_initialized() override {
		TestSwitchControllerWithRewind::on_scenes_initialized();

		// Set the controller eligible for partial update so their changes are notified ASAP.
		server_scene.scene_sync->sync_group_set_simulated_partial_update_timespan_seconds(
				player_controlled_object_1_server->local_id,
				NS::SyncGroupId::GLOBAL,
				true,
				0.0f);
	}

	virtual void on_scenes_done() override {
		TestSwitchControllerWithRewind::on_scenes_done();
		NS_ASSERT_COND(p1_rewinded_frames[0] == (reset_position_on_frame));
	}
};

/*
 
// TODO please remove this!
class ActorSceneObject : public NS::LocalSceneObject {
public:
	NS::ObjectLocalId local_id = NS::ObjectLocalId::NONE;
	Vec3 position;

	virtual void on_scene_entry() override {
		set_position(Vec3());

		if (get_scene()->scene_sync->is_server()) {
			get_scene()->scene_sync->register_app_object(get_scene()->scene_sync->to_handle(this));
		}
	}

	virtual void setup_synchronizer(NS::LocalSceneSynchronizer &p_scene_sync, NS::ObjectLocalId p_id) override {
		local_id = p_id;
		p_scene_sync.register_variable(
				p_id, "position",
				[](NS::SynchronizerManager &p_synchronizer_manager, NS::ObjectHandle p_handle, const std::string &p_var_name, const NS::VarData &p_value) {
					static_cast<ActorSceneObject *>(NS::LocalSceneSynchronizer::from_handle(p_handle))->position = Vec3::from(p_value);
				},
				[](const NS::SynchronizerManager &p_synchronizer_manager, NS::ObjectHandle p_handle, const std::string &p_var_name, NS::VarData &r_value) {
					r_value = static_cast<const ActorSceneObject *>(NS::LocalSceneSynchronizer::from_handle(p_handle))->position;
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
};

void process_actors_simulation(NS::LocalSceneSynchronizer &scene_sync, float p_delta) {
	for (const NS::ObjectData *od : scene_sync.get_sorted_objects_data()) {
		if (!od) {
			continue;
		}

		NS::LocalSceneObject *lso = scene_sync.from_handle(od->app_object_handle);
		ActorSceneObject *aso = dynamic_cast<ActorSceneObject *>(lso);
		if (aso) {
			aso->set_position(aso->get_position() + Vec3(0.2f * p_delta, 0.3f * p_delta, 0.4f * p_delta));
		}
	}
}

/// This test validates that the Partial Updated snapshot can be generated by the server
/// fetched by the client even when the controller peer is not included and that
/// the max objects per frame are properly respected.
/// NOTE: When setting `rolling_update` to true, it tests that the objects that
/// were excluded from the partial update because the max object limit was reached
/// gets an higher priority so that every object is properly synchronized.
struct TestSwitchControllerWithPartialUpdate : public TestSwitchControllerBase {
	NS::FrameIndex reset_position_on_frame = NS::FrameIndex{ { 100 } };

	/// When rolling update is set to true, the test verify that the objects
	/// excluded from the previous partial update gets higher priority and gets
	/// sync during the following updates, even if the objects just sync get modified again.
	const bool rolling_update;
	const float notify_state_interval;

	ActorSceneObject *actor_1_on_server = nullptr;
	ActorSceneObject *actor_1_on_peer1 = nullptr;

	ActorSceneObject *actor_2_on_server = nullptr;
	ActorSceneObject *actor_2_on_peer1 = nullptr;

	ActorSceneObject *actor_3_on_server = nullptr;
	ActorSceneObject *actor_3_on_peer1 = nullptr;

	ActorSceneObject *actor_4_on_server = nullptr;
	ActorSceneObject *actor_4_on_peer1 = nullptr;

public:
	std::vector<NS::FrameIndex> client_rewinded_frames;
	// The ID of snapshot sent by the server.
	std::vector<NS::FrameIndex> correction_snapshots_sent;

	TestSwitchControllerWithPartialUpdate(bool p_rolling_update) :
		rolling_update(p_rolling_update),
		notify_state_interval(1.0f) {
	}

	virtual void on_scenes_initialized() override {
		server_scene.scene_sync->set_frame_confirmation_timespan(notify_state_interval);
		// Make sure the client can predict as many frames it needs (no need to add some more noise to this test).
		server_scene.scene_sync->set_max_predicted_intervals(20);

		controller_p1_server->event_input_missed.bind([](NS::FrameIndex p_frame_index) {
			// The input should be never missing!
			NS_ASSERT_NO_ENTRY();
		});

		controller_p1_p1->get_scene_synchronizer()->event_state_validated.bind([this](NS::FrameIndex p_frame_index, bool p_desync) {
			if (p_desync) {
				client_rewinded_frames.push_back(p_frame_index);
			}
		});

		actor_1_on_server = server_scene.add_object<ActorSceneObject>("actor_1", server_scene.get_peer());
		actor_1_on_peer1 = peer_1_scene.add_object<ActorSceneObject>("actor_1", server_scene.get_peer());

		actor_2_on_server = server_scene.add_object<ActorSceneObject>("actor_2", server_scene.get_peer());
		actor_2_on_peer1 = peer_1_scene.add_object<ActorSceneObject>("actor_2", server_scene.get_peer());

		actor_3_on_server = server_scene.add_object<ActorSceneObject>("actor_3", server_scene.get_peer());
		actor_3_on_peer1 = peer_1_scene.add_object<ActorSceneObject>("actor_3", server_scene.get_peer());

		actor_4_on_server = server_scene.add_object<ActorSceneObject>("actor_4", server_scene.get_peer());
		actor_4_on_peer1 = peer_1_scene.add_object<ActorSceneObject>("actor_4", server_scene.get_peer());

		// Set the actor eligible for partial update so their changes are notified ASAP.
		server_scene.scene_sync->sync_group_set_simulated_partial_update_timespan_seconds(
				actor_1_on_server->local_id,
				NS::SyncGroupId::GLOBAL,
				true,
				0.0f);

		server_scene.scene_sync->sync_group_set_simulated_partial_update_timespan_seconds(
				actor_2_on_server->local_id,
				NS::SyncGroupId::GLOBAL,
				true,
				0.0f);

		server_scene.scene_sync->sync_group_set_simulated_partial_update_timespan_seconds(
				actor_3_on_server->local_id,
				NS::SyncGroupId::GLOBAL,
				true,
				0.0f);

		server_scene.scene_sync->sync_group_set_simulated_partial_update_timespan_seconds(
				actor_4_on_server->local_id,
				NS::SyncGroupId::GLOBAL,
				true,
				0.0f);

		// Ensure that only 2 objects are sent per Partial Update
		server_scene.scene_sync->set_max_objects_count_per_partial_update(2);

		server_scene.scene_sync->register_process(player_controlled_object_1_server->local_id, PROCESS_PHASE_POST, [=](float p_delta) -> void {
			process_actors_simulation(*server_scene.scene_sync, p_delta);
		});
		peer_1_scene.scene_sync->register_process(player_controlled_object_1_p1->local_id, PROCESS_PHASE_POST, [=](float p_delta) -> void {
			process_actors_simulation(*peer_1_scene.scene_sync, p_delta);
		});
	}

	virtual void on_server_process(float p_delta) override {
		if (controller_p1_server->get_current_frame_index() == reset_position_on_frame) {
			// Change the location of all the 4 objects.
			actor_1_on_server->set_position(Vec3(10.0, 10.0, 10.0));
			actor_2_on_server->set_position(Vec3(10.0, 10.0, 10.0));
			actor_3_on_server->set_position(Vec3(10.0, 10.0, 10.0));
			actor_4_on_server->set_position(Vec3(10.0, 10.0, 10.0));

			server_scene.scene_sync->event_sent_snapshot.bind([this](NS::FrameIndex p_frame_index, int p_peer) {
				correction_snapshots_sent.push_back(p_frame_index);
				if (rolling_update) {
					if (p_frame_index == (reset_position_on_frame + 2)) {
						server_scene.scene_sync->event_sent_snapshot.clear();
					}
				} else {
					if (p_frame_index == (reset_position_on_frame + 1)) {
						server_scene.scene_sync->event_sent_snapshot.clear();
					}
				}
			});
		} else if (rolling_update && controller_p1_server->get_current_frame_index() == reset_position_on_frame + 1) {
			// Change the location of the objects just notified.
			actor_1_on_server->set_position(Vec3(11.0, 12.0, 13.0));
			actor_2_on_server->set_position(Vec3(11.0, 12.0, 13.0));
		}
	}

	virtual void on_scenes_done() override {
		if (!rolling_update) {
			NS_ASSERT_COND(client_rewinded_frames.size() == 2);
			NS_ASSERT_COND(client_rewinded_frames[0] == reset_position_on_frame);
			NS_ASSERT_COND(client_rewinded_frames[1] == (reset_position_on_frame+1));
			NS_ASSERT_COND(correction_snapshots_sent.size() == 2);
			NS_ASSERT_COND(correction_snapshots_sent[0] == reset_position_on_frame);
			NS_ASSERT_COND(correction_snapshots_sent[1] == (reset_position_on_frame+1));
		} else {
			NS_ASSERT_COND(client_rewinded_frames.size() == 3);
			NS_ASSERT_COND(client_rewinded_frames[0] == reset_position_on_frame);
			NS_ASSERT_COND(client_rewinded_frames[1] == (reset_position_on_frame+1));
			NS_ASSERT_COND(client_rewinded_frames[2] == (reset_position_on_frame+2));
			NS_ASSERT_COND(correction_snapshots_sent.size() == 3);
			NS_ASSERT_COND(correction_snapshots_sent[0] == reset_position_on_frame);
			NS_ASSERT_COND(correction_snapshots_sent[1] == (reset_position_on_frame+1));
			NS_ASSERT_COND(correction_snapshots_sent[2] == (reset_position_on_frame+2));
		}
	}
};
*/

void test_switch_controller() {
	TestSwitchControllerNoRewind(0.0).do_test();
	//TestSwitchControllerNoRewind(0.5).do_test();

	// TODO test the sync of the authority change of the object.
	// TODO enable all the tests.
	//TestSwitchControllerWithRewind(0.0f).do_test();
	//TestSwitchControllerWithRewind(1.0f).do_test();
	//TestSwitchControllerWithRewindAndPartialUpdate(0.0f).do_test();
	//TestSwitchControllerWithRewindAndPartialUpdate(1.0f).do_test();
	//TestSwitchControllerWithPartialUpdate(false).do_test();
	//TestSwitchControllerWithPartialUpdate(true).do_test();
}
}; //namespace NS_Test