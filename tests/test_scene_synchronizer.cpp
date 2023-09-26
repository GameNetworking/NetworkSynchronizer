#include "test_scene_synchronizer.h"

#include "core/error/error_macros.h"
#include "core/math/vector3.h"
#include "core/variant/variant.h"
#include "local_scene.h"
#include "modules/network_synchronizer/tests/local_network.h"

namespace NS_Test {

const float delta = 1.0 / 60.0;

class LocalNetworkedController : public NS::NetworkedController<NS::LocalNetworkInterface>, public NS::NetworkedControllerManager, public NS::LocalSceneObject {
public:
	LocalNetworkedController() {}

	virtual void on_scene_entry() override {
		// Setup the NetworkInterface.
		get_network_interface().init(get_scene()->get_network(), name, authoritative_peer_id);
		setup(*this);

		Variant line_pos = 0.0;
		variables.insert(std::make_pair("position", line_pos));

		get_scene()->scene_sync->register_app_object(this);
	}

	virtual void on_scene_exit() override {
		get_scene()->scene_sync->on_app_object_removed(this);
	}

	virtual void setup_synchronizer(NS::LocalSceneSynchronizer &p_scene_sync) override {
		p_scene_sync.register_variable(this, "position");
	}

	virtual void collect_inputs(double p_delta, DataBuffer &r_buffer) override {
		r_buffer.add_bool(true);
	}

	virtual void controller_process(double p_delta, DataBuffer &p_buffer) override {
		if (p_buffer.read_bool()) {
			float position = variables["position"];
			const float one_meter = 1.0;
			position += p_delta * one_meter;
			variables["position"] = position;
		}
	}

	virtual bool are_inputs_different(DataBuffer &p_buffer_A, DataBuffer &p_buffer_B) override {
		return p_buffer_A.read_bool() != p_buffer_B.read_bool();
	}

	virtual uint32_t count_input_size(DataBuffer &p_buffer) override {
		return p_buffer.get_bool_size();
	}
};

void test_client_and_server_initialization() {
	NS::LocalScene server_scene;
	server_scene.start_as_server();

	NS::LocalScene peer_1_scene;
	peer_1_scene.start_as_client(server_scene);

	NS::LocalScene peer_2_scene;
	peer_2_scene.start_as_client(server_scene);

	// Add the scene sync
	server_scene.scene_sync =
			server_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
	CRASH_COND_MSG(!server_scene.scene_sync->is_server(), "This must be a server scene sync.");

	peer_1_scene.scene_sync =
			peer_1_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
	CRASH_COND_MSG(!peer_1_scene.scene_sync->is_client(), "This must be a client scene sync.");

	peer_2_scene.scene_sync =
			peer_2_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
	CRASH_COND_MSG(!peer_2_scene.scene_sync->is_client(), "This must be a cliet scene sync.");

	// Add peer 1 controller.
	server_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());
	CRASH_COND_MSG(!server_scene.fetch_object<LocalNetworkedController>("controller_1")->is_server_controller(), "This must be a ServerController on this peer.");

	peer_1_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());
	CRASH_COND_MSG(!peer_1_scene.fetch_object<LocalNetworkedController>("controller_1")->is_player_controller(), "This must be a PlayerController on this peer.");

	peer_2_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());
	CRASH_COND_MSG(!peer_2_scene.fetch_object<LocalNetworkedController>("controller_1")->is_doll_controller(), "This must be a DollController on this peer.");

	// Add peer 2 controller.
	server_scene.add_object<LocalNetworkedController>("controller_2", peer_2_scene.get_peer());
	CRASH_COND(!server_scene.fetch_object<LocalNetworkedController>("controller_2")->is_server_controller());
	peer_1_scene.add_object<LocalNetworkedController>("controller_2", peer_2_scene.get_peer());
	CRASH_COND(!peer_1_scene.fetch_object<LocalNetworkedController>("controller_2")->is_doll_controller());
	peer_2_scene.add_object<LocalNetworkedController>("controller_2", peer_2_scene.get_peer());
	CRASH_COND(!peer_2_scene.fetch_object<LocalNetworkedController>("controller_2")->is_player_controller());
}

class TestSceneObject : public NS::LocalSceneObject {
public:
	virtual void on_scene_entry() override {
		get_scene()->scene_sync->register_variable(this, "var_1");
	}

	virtual void on_scene_exit() override {
		get_scene()->scene_sync->on_app_object_removed(this);
	}
};

void test_state_notify() {
	NS::LocalScene server_scene;
	server_scene.start_as_server();

	NS::LocalScene peer_1_scene;
	peer_1_scene.start_as_client(server_scene);

	NS::LocalScene peer_2_scene;
	peer_2_scene.start_as_client(server_scene);

	// Add the scene sync
	server_scene.scene_sync =
			server_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
	peer_1_scene.scene_sync =
			peer_1_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
	peer_2_scene.scene_sync =
			peer_2_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());

	// Add peer 1 controller.
	server_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer());
	peer_1_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer());
	peer_2_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer());

	for (int f = 0; f < 2; f++) {
		// Test with notify interval set to 0
		{
			server_scene.scene_sync->set_server_notify_state_interval(0.0);

			// Set the `var_1` to a different value to all the clients.
			server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"] = 0;
			peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"] = 1;
			peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"] = 2;

			// Process exactly 1 time.
			// NOTE: Processing the controller so the server receives the input right after.
			server_scene.process(delta);
			peer_1_scene.process(delta);
			peer_2_scene.process(delta);

			// The notification interval is set to 0 therefore the server sends
			// the snapshot right away: since the server snapshot is always
			// at least one frame behind the client, we can assume that the
			// client has applied the server correction.
			CRASH_COND(int(server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"]) != 0);
			CRASH_COND(int(peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"]) != 0);
			CRASH_COND(int(peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"]) != 0);
		}

		// Test with notify interval set to 0.5 seconds.
		{
			server_scene.scene_sync->set_server_notify_state_interval(0.5);

			// Set the `var_1` to a different value to all the clients.
			server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"] = 3;
			peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"] = 4;
			peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"] = 5;

			// Process for 0.5 second + delta
			float time = 0.0;
			for (; time <= 0.5 + delta + 0.001; time += delta) {
				// NOTE: Processing the controller so the server receives the input right after.
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);

				if (
						int(server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"]) == 3 &&
						int(peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"]) == 3 &&
						int(peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"]) == 3) {
					break;
				}
			}

			// The notification interval is set to 0.5 therefore the server sends
			// the snapshot after some 0.5s: since the server snapshot is always
			// at least one frame behind the client, we can assume that the
			// client has applied the server correction.
			CRASH_COND(int(server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"]) != 3);
			CRASH_COND(int(peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"]) != 3);
			CRASH_COND(int(peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"]) != 3);
			CRASH_COND(time >= 0.5);
		}

		// Test by making sure the Scene Sync is able to sync when the variable
		// changes only on the client side.
		{
			// No local controller, therefore the correction is applied by the
			// server right away.
			server_scene.scene_sync->set_server_notify_state_interval(0.0);

			// The server remains like it was.
			// server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"] = 3;
			// While the peers change its variables.
			peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"] = 4;
			peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"] = 5;

			if (f == 0) {
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);

				// Still the value expected is `3`.
				CRASH_COND(int(server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"]) != 3);
				CRASH_COND(int(peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"]) != 3);
				CRASH_COND(int(peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"]) != 3);
			} else {
				// Note: the +1 is needed because the change is recored on the snapshot
				// the scene sync is going to created on the next "process".
				const uint32_t change_made_on_frame = 1 + peer_1_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_input_id();

				// When the local controller is set, the client scene sync compares the
				// received server snapshot with the one recorded on the client on
				// the same frame.
				// Since the above code is altering the variable on the client
				// and not on the server, which is 1 frame ahead the server,
				// the client will detects such change when it receives
				// the snapshot for the same (or newer) frame.

				// For the above reason we have to process the scenes multiple times,
				// before seeing the value correctly applied.
				// Process 1
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);

				// However, since the `peer_2` doesn't have the local controller
				// the server snapshot is expected to be applied right away.
				CRASH_COND(int(peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"]) != 3);

				// Exactly we need to process the scene three times.
				// Process 2
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);

				// The reason is that the client scene sync creates the snapshot
				// right before the `process` function terminates: meaning that
				// the change made above is registered on the "next" frame.
				// So, the server have to be processed three times to catch the client.
				// Process 3
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);

				// Make sure the server is indeed at the same frame on which the
				// client made the change.
				CRASH_COND(change_made_on_frame != server_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_input_id());

				// and now is time to check for the `peer_1`.
				CRASH_COND(int(peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"]) != 3);
			}
		}

		if (f == 0) {
			// Now add the PlayerControllers and test the above mechanism still works.
			server_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());
			peer_1_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());
			peer_2_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());

			// Process two times to make sure all the peers are initialized at thie time.
			for (int j = 0; j < 2; j++) {
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);
			}

			CRASH_COND(server_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_input_id() != 0);
			CRASH_COND(peer_1_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_input_id() != 1);
			// NOTE: No need to check the peer_2, because it's not an authoritative controller anyway.
		} else {
			// Make sure the controllers have been processed at this point.
			CRASH_COND(server_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_input_id() == 0);
			CRASH_COND(server_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_input_id() == UINT32_MAX);
			CRASH_COND(peer_1_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_input_id() == 0);
			CRASH_COND(peer_1_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_input_id() == UINT32_MAX);

			// NOTE: No need to check the peer_2, because it's not an authoritative controller anyway.
		}
	}
}

void test_snapshot_generation() {
	// TODO implement this.
}

void test_rewinding() {
	// TODO implement this.
}

void test_state_notify_for_no_rewind_properties() {
	// TODO implement this.
}

void test_doll_simulation_rewindings() {
	// TODO implement this.
}

void test_variable_change_event() {
	// TODO implement this.
}

void test_controller_processing() {
	// TODO implement this.
}

void test_streaming() {
	// TODO implement this.
}

void test_scene_synchronizer() {
	test_client_and_server_initialization();
	test_state_notify();
	test_snapshot_generation();
	test_rewinding();
	test_state_notify_for_no_rewind_properties();
	test_doll_simulation_rewindings();
	test_variable_change_event();
	test_controller_processing();
	test_streaming();
}
}; //namespace NS_Test
