#include "test_scene_synchronizer.h"

#include "core/error/error_macros.h"
#include "core/math/vector3.h"
#include "core/variant/variant.h"
#include "local_scene.h"
#include "modules/network_synchronizer/tests/local_network.h"

namespace NS_Test {

class TestSceneObject : public NS::LocalSceneObject {
public:
	virtual void on_scene_entry() override {
		get_scene()->scene_sync->register_app_object(this);
	}

	virtual void on_scene_exit() override {
		get_scene()->scene_sync->on_app_object_removed(this);
	}
};

class LocalNetworkedController : public NS::NetworkedController, public NS::NetworkedControllerManager, public NS::LocalSceneObject {
public:
	NS::LocalNetworkInterface network_interface;

	LocalNetworkedController() {}

	virtual void on_scene_entry() override {
		// Setup the NetworkInterface.
		setup(network_interface, *this);
		network_interface.init(get_scene()->get_network(), name);

		Variant line_pos = 0.0;
		variables.insert(std::make_pair("position", line_pos));
		get_scene()->scene_sync->register_app_object(this);
	}

	virtual void on_scene_exit() override {
		get_scene()->scene_sync->on_app_object_removed(this);
	}

	virtual void setup_synchronizer(NS::LocalSceneSynchronizer &p_scene_sync) override {
		p_scene_sync.register_variable(static_cast<NS::LocalSceneObject *>(this), "position");
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

void test_client_server_sync() {
	NS::LocalScene server_scene;
	server_scene.start_as_server();

	NS::LocalScene peer_1_scene;
	peer_1_scene.start_as_client(server_scene);

	NS::LocalScene peer_2_scene;
	peer_2_scene.start_as_client(server_scene);

	// Add the scene sync
	server_scene.scene_sync =
			server_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer()).get();

	peer_1_scene.scene_sync =
			peer_1_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer()).get();

	peer_2_scene.scene_sync =
			peer_2_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer()).get();

	// Add peer 1 controller.
	server_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());
	peer_1_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());
	peer_2_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());

	// Add peer 2 controller.
	server_scene.add_object<LocalNetworkedController>("controller_2", peer_2_scene.get_peer());
	peer_1_scene.add_object<LocalNetworkedController>("controller_2", peer_2_scene.get_peer());
	peer_2_scene.add_object<LocalNetworkedController>("controller_2", peer_2_scene.get_peer());

	const float delta = 1.0 / 60.0;
	for (float t = 0.0; t < 2.0; t += delta) {
		server_scene.process(delta);
		peer_1_scene.process(delta);
		peer_2_scene.process(delta);
	}

	ERR_PRINT("[Server] Line position: " + rtos(server_scene.fetch_object("controller_1")->variables["position"]));
	ERR_PRINT("[client 1] Line position: " + rtos(peer_1_scene.fetch_object("controller_1")->variables["position"]));
	ERR_PRINT("[client 2] Line position: " + rtos(peer_2_scene.fetch_object("controller_1")->variables["position"]));
}

void test_scene_synchronizer() {
	test_client_server_sync();
}
}; //namespace NS_Test
