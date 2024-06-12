#include "test_simulation.h"

#include "../core/core.h"
#include "../core/data_buffer.h"
#include "../core/ensure.h"
#include "../core/net_utilities.h"
#include "../core/processor.h"
#include "../core/var_data.h"
#include "local_network.h"
#include "local_scene.h"
#include "test_math_lib.h"
#include <string>

namespace NS_Test {

const double delta = 1.0 / 60.0;

class MagnetSceneObject : public NS::LocalSceneObject {
public:
	NS::ObjectLocalId local_id = NS::ObjectLocalId::NONE;

	virtual void on_scene_entry() override {
		set_weight(1.0);
		set_position(Vec3());

		get_scene()->scene_sync->register_app_object(get_scene()->scene_sync->to_handle(this));
	}

	virtual void setup_synchronizer(NS::LocalSceneSynchronizer &p_scene_sync, NS::ObjectLocalId p_id) override {
		local_id = p_id;
		p_scene_sync.register_variable(p_id, "weight");
		p_scene_sync.register_variable(p_id, "position");
	}

	virtual void on_scene_exit() override {
		get_scene()->scene_sync->on_app_object_removed(get_scene()->scene_sync->to_handle(this));
	}

	void set_weight(float w) {
		NS::VarData vd;
		vd.type = 1;
		vd.data.f32 = w;
		NS::MapFunc::assign(variables, std::string("weight"), std::move(vd));
	}

	float get_weight() const {
		const NS::VarData *vd = NS::MapFunc::get_or_null(variables, std::string("weight"));
		if (vd) {
			return vd->data.f32;
		} else {
			return 1.0;
		}
	}

	void set_position(const Vec3 &p_pos) {
		NS::VarData vd = p_pos;
		vd.type = 2;
		NS::MapFunc::assign(variables, std::string("position"), std::move(vd));
	}

	Vec3 get_position() const {
		const NS::VarData *vd = NS::MapFunc::get_or_null(variables, std::string("position"));
		if (vd) {
			return Vec3::from(*vd);
		} else {
			return Vec3();
		}
	}
};

class TSLocalNetworkedController : public NS::LocalSceneObject {
public:
	NS::ObjectLocalId local_id = NS::ObjectLocalId::NONE;

	TSLocalNetworkedController() = default;

	virtual void on_scene_entry() override {
		set_weight(1.0);
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
				authoritative_peer_id,
				[this](double p_delta, NS::DataBuffer &r_buffer) -> void { collect_inputs(p_delta, r_buffer); },
				[this](NS::DataBuffer &p_buffer) -> int { return count_input_size(p_buffer); },
				[this](NS::DataBuffer &p_buffer_A, NS::DataBuffer &p_buffer_b) -> bool { return are_inputs_different(p_buffer_A, p_buffer_b); },
				[this](double p_delta, NS::DataBuffer &p_buffer) -> void { controller_process(p_delta, p_buffer); });

		p_scene_sync.register_variable(p_id, "weight");
		p_scene_sync.register_variable(p_id, "position");
	}

	void set_weight(float w) {
		NS::VarData vd;
		vd.data.f32 = w;
		vd.type = 1;
		NS::MapFunc::assign(variables, std::string("weight"), std::move(vd));
	}

	float get_weight() const {
		const NS::VarData *vd = NS::MapFunc::get_or_null(variables, std::string("weight"));
		if (vd) {
			return vd->data.f32;
		} else {
			return 1.0;
		}
	}

	void set_position(const Vec3 &p_pos) {
		NS::VarData vd = p_pos;
		vd.type = 2;
		NS::MapFunc::assign(variables, std::string("position"), std::move(vd));
	}

	Vec3 get_position() const {
		const NS::VarData *vd = NS::MapFunc::get_or_null(variables, std::string("position"));
		if (vd) {
			return Vec3::from(*vd);
		} else {
			return Vec3();
		}
	}

	// ------------------------------------------------- NetController interface
	const Vec3 inputs[20] = {
		Vec3(1.0, 0.0, 0.0),
		Vec3(0.0, 1.0, 0.0),
		Vec3(0.0, 0.0, 1.0),
		Vec3(0.0, 1.0, 0.0),
		Vec3(1.0, 0.0, 0.0),
		Vec3(1.0, 0.0, 0.0),
		Vec3(1.0, 0.0, 0.0),
		Vec3(0.0, 0.0, 1.0),
		Vec3(0.0, 0.0, 1.0),
		Vec3(0.0, 0.0, 1.0),
		Vec3(0.0, 1.0, 0.0),
		Vec3(1.0, 0.0, 0.0),
		Vec3(0.0, 0.0, 1.0),
		Vec3(1.0, 0.0, 0.0),
		Vec3(1.0, 0.0, 0.0),
		Vec3(1.0, 0.0, 0.0),
		Vec3(1.0, 0.0, 0.0),
		Vec3(1.0, 0.0, 0.0),
		Vec3(1.0, 0.0, 0.0),
		Vec3(0.0, 1.0, 0.0)
	};

	void collect_inputs(double p_delta, NS::DataBuffer &r_buffer) {
		const NS::FrameIndex current_frame_index = scene_owner->scene_sync->get_controller_for_peer(authoritative_peer_id)->get_current_frame_index();
		const int index = current_frame_index.id % 20;
		std::ignore = r_buffer.add_normalized_vector3(Vector3(inputs[index].x, inputs[index].y, inputs[index].z), NS::DataBuffer::COMPRESSION_LEVEL_3);
	}

	void controller_process(double p_delta, NS::DataBuffer &p_buffer) {
		ASSERT_COND(p_delta == delta);
		const float speed = 1.0;
		const Vector3 v = p_buffer.read_normalized_vector3(NS::DataBuffer::COMPRESSION_LEVEL_3);
		const Vec3 input(v.x, v.y, v.z);
		set_position(get_position() + (input * speed * p_delta));
	}

	bool are_inputs_different(NS::DataBuffer &p_buffer_A, NS::DataBuffer &p_buffer_B) {
		const Vector3 v1 = p_buffer_A.read_normalized_vector3(NS::DataBuffer::COMPRESSION_LEVEL_3);
		const Vector3 v2 = p_buffer_B.read_normalized_vector3(NS::DataBuffer::COMPRESSION_LEVEL_3);
		return v1 != v2;
	}

	uint32_t count_input_size(NS::DataBuffer &p_buffer) {
		return p_buffer.get_normalized_vector2_size(NS::DataBuffer::COMPRESSION_LEVEL_3);
	}
};

void process_magnet_simulation(NS::LocalSceneSynchronizer &scene_sync, double p_delta, MagnetSceneObject &p_mag) {
	ASSERT_COND(p_delta == delta);
	const float pushing_force = 200.0;

	for (const NS::ObjectData *od : scene_sync.get_sorted_objects_data()) {
		if (!od) {
			continue;
		}

		NS::LocalSceneObject *lso = scene_sync.from_handle(od->app_object_handle);
		TSLocalNetworkedController *controller = dynamic_cast<TSLocalNetworkedController *>(lso);
		if (controller) {
			{
				const Vec3 mag_to_controller_dir = (controller->get_position() - p_mag.get_position()).normalized();
				controller->set_position(controller->get_position() + (mag_to_controller_dir * ((pushing_force / controller->get_weight()) * p_delta)));
			}

			{
				const Vec3 controller_dir_to_mag = (p_mag.get_position() - controller->get_position()).normalized();
				p_mag.set_position(p_mag.get_position() + (controller_dir_to_mag * ((pushing_force / p_mag.get_weight()) * p_delta)));
			}
		}
	}
}

void process_magnets_simulation(NS::LocalSceneSynchronizer &scene_sync, double p_delta) {
	for (const NS::ObjectData *od : scene_sync.get_sorted_objects_data()) {
		if (!od) {
			continue;
		}

		NS::LocalSceneObject *lso = scene_sync.from_handle(od->app_object_handle);
		MagnetSceneObject *mso = dynamic_cast<MagnetSceneObject *>(lso);
		if (mso) {
			process_magnet_simulation(scene_sync, p_delta, *mso);
		}
	}
}

/// This class is responsible to verify that the client and the server can run
/// the same simulation.
/// This class is made in a way which allows to be overriden to test the sync
/// still works under bad conditions.
struct TestSimulationBase {
	NS::LocalScene server_scene;
	NS::LocalScene peer_1_scene;

	TSLocalNetworkedController *controlled_obj_server = nullptr;
	NS::PeerNetworkedController *controller_server = nullptr;

	TSLocalNetworkedController *controlled_obj_p1 = nullptr;
	NS::PeerNetworkedController *controller_p1 = nullptr;

	NS::FrameIndex process_until_frame = NS::FrameIndex{ { 300 } };
	int process_until_frame_timeout = 20;

private:
	virtual void on_scenes_initialized() {}
	virtual void on_server_process(double p_delta) {}
	virtual void on_client_process(double p_delta) {}
	virtual void on_scenes_processed(double p_delta) {}
	virtual void on_scenes_done() {}

public:
	TestSimulationBase() {}
	virtual ~TestSimulationBase() {}

	double rand_range(double M, double N) {
		return M + (rand() / (RAND_MAX / (N - M)));
	}

	void do_test() {
		// Create a server
		server_scene.start_as_server();

		// and a client connected to the server.
		peer_1_scene.start_as_client(server_scene);

		// Add the scene sync
		server_scene.scene_sync =
				server_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
		peer_1_scene.scene_sync =
				peer_1_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());

		// Then compose the scene: 1 controller and 2 magnets.
		controlled_obj_server = server_scene.add_object<TSLocalNetworkedController>("controller_1", peer_1_scene.get_peer());
		controller_server = server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer());

		controlled_obj_p1 = peer_1_scene.add_object<TSLocalNetworkedController>("controller_1", peer_1_scene.get_peer());
		controller_p1 = peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer());

		MagnetSceneObject *light_magnet_server = server_scene.add_object<MagnetSceneObject>("magnet_1", server_scene.get_peer());
		MagnetSceneObject *light_magnet_p1 = peer_1_scene.add_object<MagnetSceneObject>("magnet_1", server_scene.get_peer());

		MagnetSceneObject *heavy_magnet_server = server_scene.add_object<MagnetSceneObject>("magnet_2", server_scene.get_peer());
		MagnetSceneObject *heavy_magnet_p1 = peer_1_scene.add_object<MagnetSceneObject>("magnet_2", server_scene.get_peer());

		// Register the process
		server_scene.scene_sync->register_process(controlled_obj_server->local_id, PROCESS_PHASE_POST, [=](double p_delta) -> void {
			process_magnets_simulation(*server_scene.scene_sync, p_delta);
		});
		peer_1_scene.scene_sync->register_process(controlled_obj_p1->local_id, PROCESS_PHASE_POST, [=](double p_delta) -> void {
			process_magnets_simulation(*peer_1_scene.scene_sync, p_delta);
		});
		server_scene.scene_sync->register_process(controlled_obj_server->local_id, PROCESS_PHASE_LATE, [=](double p_delta) -> void {
			on_server_process(p_delta);
		});
		peer_1_scene.scene_sync->register_process(controlled_obj_p1->local_id, PROCESS_PHASE_LATE, [=](double p_delta) -> void {
			on_client_process(p_delta);
		});

		on_scenes_initialized();

		// Set the weight of each object:
		controlled_obj_server->set_position(Vec3(1.0, 1.0, 1.0));
		controlled_obj_p1->set_position(Vec3(1.0, 1.0, 1.0));
		controlled_obj_server->set_weight(70.0);
		controlled_obj_p1->set_weight(70.0);

		light_magnet_server->set_position(Vec3(2.0, 1.0, 1.0));
		light_magnet_p1->set_position(Vec3(2.0, 1.0, 1.0));
		light_magnet_server->set_weight(1.0);
		light_magnet_p1->set_weight(1.0);

		heavy_magnet_server->set_position(Vec3(1.0, 1.0, 2.0));
		heavy_magnet_p1->set_position(Vec3(1.0, 1.0, 2.0));
		heavy_magnet_server->set_weight(200.0);
		heavy_magnet_p1->set_weight(200.0);

		bool server_reached_target_frame = false;
		bool p1_reached_target_frame = false;

		Vec3 controller_server_position_at_target_frame;
		Vec3 light_mag_server_position_at_target_frame;
		Vec3 heavy_mag_server_position_at_target_frame;
		Vec3 controller_p1_position_at_target_frame;
		Vec3 light_mag_p1_position_at_target_frame;
		Vec3 heavy_mag_p1_position_at_target_frame;

		while (true) {
			// Use a random delta, to make sure the NetSync can be processed
			// by a normal process loop with dynamic `delta_time`.
			const double rand_delta = rand_range(0.005, delta);
			server_scene.process(rand_delta);
			peer_1_scene.process(rand_delta);

			on_scenes_processed(rand_delta);

			if (controller_server->get_current_frame_index() == process_until_frame) {
				server_reached_target_frame = true;
				controller_server_position_at_target_frame = controlled_obj_server->get_position();
				light_mag_server_position_at_target_frame = light_magnet_server->get_position();
				heavy_mag_server_position_at_target_frame = heavy_magnet_server->get_position();
			}
			if (controller_p1->get_current_frame_index() == process_until_frame) {
				p1_reached_target_frame = true;
				controller_p1_position_at_target_frame = controlled_obj_p1->get_position();
				light_mag_p1_position_at_target_frame = light_magnet_p1->get_position();
				heavy_mag_p1_position_at_target_frame = heavy_magnet_p1->get_position();
			}

			if (server_reached_target_frame && p1_reached_target_frame) {
				break;
			}

			ASSERT_COND(controller_server->get_current_frame_index() >= (process_until_frame + process_until_frame_timeout) && controller_server->get_current_frame_index() == NS::FrameIndex::NONE);
			ASSERT_COND(controller_p1->get_current_frame_index() >= (process_until_frame + process_until_frame_timeout) && controller_p1->get_current_frame_index() == NS::FrameIndex::NONE);
		}

		//                  ---- Validation phase ----
		// First make sure all positions have changed at all.
		ASSERT_COND(controlled_obj_server->get_position().distance_to(Vec3(1, 1, 1)) > 0.0001);
		ASSERT_COND(light_magnet_server->get_position().distance_to(Vec3(2, 1, 1)) > 0.0001);
		ASSERT_COND(heavy_magnet_server->get_position().distance_to(Vec3(1, 1, 2)) > 0.0001);

		// Now, make sure the client and server positions are the same: ensuring the
		// sync worked.
		ASSERT_COND(controller_server_position_at_target_frame.distance_to(controller_p1_position_at_target_frame) < 0.0001);
		ASSERT_COND(light_mag_server_position_at_target_frame.distance_to(light_mag_p1_position_at_target_frame) < 0.0001);
		ASSERT_COND(heavy_mag_server_position_at_target_frame.distance_to(heavy_mag_p1_position_at_target_frame) < 0.0001);

		on_scenes_done();
	}
};

/// This test was build to verify that the NetSync is able to immediately re-sync
/// a scene.
/// It manually de-sync the server by teleporting the controller, and then
/// make sure the client was immediately re-sync with a single rewinding action.
struct TestSimulationWithRewind : public TestSimulationBase {
	NS::FrameIndex reset_position_on_frame = NS::FrameIndex{ { 100 } };
	float notify_state_interval = 0.0;

public:
	std::vector<NS::FrameIndex> client_rewinded_frames;
	// The ID of snapshot sent by the server.
	NS::FrameIndex correction_snapshot_sent = NS::FrameIndex{ { 0 } };

	TestSimulationWithRewind(float p_notify_state_interval) :
			notify_state_interval(p_notify_state_interval) {}

	virtual void on_scenes_initialized() override {
		server_scene.scene_sync->set_frame_confirmation_timespan(notify_state_interval);
		// Make sure the client can predicts as many frames it needs (no need to add some more noise on this test).
		server_scene.scene_sync->set_max_predicted_intervals(20);

		controller_server->event_input_missed.bind([](NS::FrameIndex p_frame_index) {
			// The input should be never missing!
			ASSERT_NO_ENTRY();
		});

		controller_p1->get_scene_synchronizer()->event_state_validated.bind([this](NS::FrameIndex p_frame_index, bool p_desync) {
			if (p_desync) {
				client_rewinded_frames.push_back(p_frame_index);
			}
		});
	}

	virtual void on_server_process(double p_delta) override {
		if (controller_server->get_current_frame_index() == reset_position_on_frame) {
			// Reset the character position only on the server, to simulate a desync.
			controlled_obj_server->set_position(Vec3(0.0, 0.0, 0.0));

			server_scene.scene_sync->event_sent_snapshot.bind([this](NS::FrameIndex p_frame_index, int p_peer) {
				correction_snapshot_sent = p_frame_index;

				// Make sure this function is not called once again.
				server_scene.scene_sync->event_sent_snapshot.clear();
			});
		}
	}

	virtual void on_scenes_processed(double p_delta) override {
	}

	virtual void on_scenes_done() override {
		ASSERT_COND(client_rewinded_frames.size() == 1);
		ASSERT_COND(client_rewinded_frames[0] >= reset_position_on_frame);
		ASSERT_COND(client_rewinded_frames[0] == correction_snapshot_sent);
	}
};

void test_simulation() {
	TestSimulationBase().do_test();
	TestSimulationWithRewind(0.0).do_test();
	TestSimulationWithRewind(1.0).do_test();
}
}; //namespace NS_Test
