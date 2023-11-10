#include "test_simulation.h"

#include "core/error/error_macros.h"
#include "core/math/vector3.h"
#include "core/variant/variant.h"
#include "local_scene.h"
#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/core/var_data.h"
#include "modules/network_synchronizer/net_utilities.h"
#include "modules/network_synchronizer/tests/local_network.h"

namespace NS_Test {

const float delta = 1.0 / 60.0;

class LocalNetworkedController : public NS::NetworkedController<NS::LocalNetworkInterface>, public NS::NetworkedControllerManager, public NS::LocalSceneObject {
public:
	NS::ObjectLocalId local_id = NS::ObjectLocalId::NONE;

	LocalNetworkedController() {}

	virtual void on_scene_entry() override {
		// Setup the NetworkInterface.
		get_network_interface().init(get_scene()->get_network(), name, authoritative_peer_id);
		setup(*this);

		variables.insert(std::make_pair("position", NS::VarData()));

		get_scene()->scene_sync->register_app_object(get_scene()->scene_sync->to_handle(this));
	}

	virtual void on_scene_exit() override {
		get_scene()->scene_sync->unregister_app_object(local_id);
	}

	virtual void setup_synchronizer(NS::LocalSceneSynchronizer &p_scene_sync, NS::ObjectLocalId p_id) override {
		local_id = p_id;
		p_scene_sync.register_variable(p_id, "position");
	}

	virtual void collect_inputs(double p_delta, DataBuffer &r_buffer) override {
		r_buffer.add_bool(true);
	}

	virtual void controller_process(double p_delta, DataBuffer &p_buffer) override {
		if (p_buffer.read_bool()) {
			const float one_meter = 1.0;
			variables["position"].data.f32 += p_delta * one_meter;
		}
	}

	virtual bool are_inputs_different(DataBuffer &p_buffer_A, DataBuffer &p_buffer_B) override {
		return p_buffer_A.read_bool() != p_buffer_B.read_bool();
	}

	virtual uint32_t count_input_size(DataBuffer &p_buffer) override {
		return p_buffer.get_bool_size();
	}
};

void test_rewinding() {
	NS::LocalScene server_scene;
	server_scene.start_as_server();

	NS::LocalScene peer_1_scene;
	peer_1_scene.start_as_client(server_scene);

	NS::ObjectLocalId server_obj_1_oh = server_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer())->find_local_id();
	server_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());
	NS::ObjectLocalId p1_obj_1_oh = peer_1_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer())->find_local_id();
	peer_1_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());
}

void test_doll_simulation_rewindings() {
	// TODO implement this.
}

void test_simulation() {
	test_rewinding();
	test_doll_simulation_rewindings();
}
}; //namespace NS_Test
