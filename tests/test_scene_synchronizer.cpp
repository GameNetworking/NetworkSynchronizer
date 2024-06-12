#include "test_scene_synchronizer.h"

#include "../core/core.h"
#include "../core/ensure.h"
#include "../core/net_utilities.h"
#include "../core/var_data.h"
#include "../data_buffer.h"
#include "../tests/local_network.h"
#include "local_scene.h"
#include <functional>
#include <vector>

namespace NS_Test {

void test_ids() {
	NS::VarId var_id_0 = NS::VarId{ { 0 } };
	NS::VarId var_id_0_2 = NS::VarId{ { 0 } };
	NS::VarId var_id_1 = NS::VarId{ { 1 } };

	ASSERT_COND(var_id_0 == var_id_0_2);
	ASSERT_COND(var_id_0 != var_id_1);
	ASSERT_COND(var_id_0 <= var_id_1);
	ASSERT_COND(var_id_0 < var_id_1);
	ASSERT_COND(var_id_1 >= var_id_0);
	ASSERT_COND(var_id_1 > var_id_0);

	NS::VarId var_id_2 = var_id_1 + 1;
	ASSERT_COND(var_id_2.id == 2);

	NS::VarId var_id_3 = var_id_0;
	var_id_3 += var_id_1;
	var_id_3 += int(1);
	var_id_3 += uint32_t(1);
	ASSERT_COND(var_id_3.id == 3);
}

const double delta = 1.0 / 60.0;

class LocalNetworkedController : public NS::LocalSceneObject {
public:
	NS::ObjectLocalId local_id = NS::ObjectLocalId::NONE;

	LocalNetworkedController() {}

	virtual void on_scene_entry() override {
		variables.insert(std::make_pair("position", NS::VarData()));

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
				std::bind(&LocalNetworkedController::collect_inputs, this, std::placeholders::_1, std::placeholders::_2),
				std::bind(&LocalNetworkedController::count_input_size, this, std::placeholders::_1),
				std::bind(&LocalNetworkedController::are_inputs_different, this, std::placeholders::_1, std::placeholders::_2),
				std::bind(&LocalNetworkedController::controller_process, this, std::placeholders::_1, std::placeholders::_2));

		p_scene_sync.register_variable(p_id, "position");
	}

	void collect_inputs(double p_delta, DataBuffer &r_buffer) {
		r_buffer.add_bool(true);
	}

	void controller_process(double p_delta, DataBuffer &p_buffer) {
		if (p_buffer.read_bool()) {
			const float one_meter = 1.0;
			variables["position"].data.f32 += p_delta * one_meter;
		}
	}

	bool are_inputs_different(DataBuffer &p_buffer_A, DataBuffer &p_buffer_B) {
		return p_buffer_A.read_bool() != p_buffer_B.read_bool();
	}

	uint32_t count_input_size(DataBuffer &p_buffer) {
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
	ASSERT_COND_MSG(server_scene.scene_sync->is_server(), "This must be a server scene sync.");

	peer_1_scene.scene_sync =
			peer_1_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
	ASSERT_COND_MSG(peer_1_scene.scene_sync->is_client(), "This must be a client scene sync.");

	peer_2_scene.scene_sync =
			peer_2_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
	ASSERT_COND_MSG(peer_2_scene.scene_sync->is_client(), "This must be a cliet scene sync.");

	// Make sure the controller exists right away the peer are connected.
	ASSERT_COND_MSG(server_scene.scene_sync->get_controller_for_peer(server_scene.get_peer(), false), "This must be NON null at this point.");
	ASSERT_COND_MSG(peer_1_scene.scene_sync->get_controller_for_peer(server_scene.get_peer(), false), "This must be NON null at this point.");
	ASSERT_COND_MSG(peer_2_scene.scene_sync->get_controller_for_peer(server_scene.get_peer(), false), "This must be NON null at this point.");

	ASSERT_COND_MSG(server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer(), false), "This must be NON null at this point.");
	ASSERT_COND_MSG(peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer(), false), "This must be NON null at this point.");
	ASSERT_COND_MSG(peer_2_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer(), false), "This must be NON null at this point.");

	ASSERT_COND_MSG(server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer(), false), "This must be NON null at this point.");
	ASSERT_COND_MSG(peer_1_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer(), false), "This must be NON null at this point.");
	ASSERT_COND_MSG(peer_2_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer(), false), "This must be NON null at this point.");

	// Make sure all the controllers are disabled.
	ASSERT_COND_MSG(server_scene.scene_sync->get_controller_for_peer(server_scene.get_peer())->can_simulate() == false, "This must be disabled at this point.");
	ASSERT_COND_MSG(peer_1_scene.scene_sync->get_controller_for_peer(server_scene.get_peer())->can_simulate() == false, "This must be disabled at this point.");
	ASSERT_COND_MSG(peer_2_scene.scene_sync->get_controller_for_peer(server_scene.get_peer())->can_simulate() == false, "This must be disabled at this point.");

	ASSERT_COND_MSG(server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->can_simulate() == false, "This must be disabled at this point.");
	ASSERT_COND_MSG(peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->can_simulate() == false, "This must be disabled at this point.");
	ASSERT_COND_MSG(peer_2_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->can_simulate() == false, "This must be disabled at this point.");

	ASSERT_COND_MSG(server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->can_simulate() == false, "This must be disabled at this point.");
	ASSERT_COND_MSG(peer_1_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->can_simulate() == false, "This must be disabled at this point.");
	ASSERT_COND_MSG(peer_2_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->can_simulate() == false, "This must be disabled at this point.");

	// Validate the controllers mode.
	ASSERT_COND_MSG(server_scene.scene_sync->get_controller_for_peer(server_scene.get_peer())->is_server_controller(), "This must be a ServerController on this peer.");
	ASSERT_COND_MSG(peer_1_scene.scene_sync->get_controller_for_peer(server_scene.get_peer())->is_doll_controller(), "This must be a PlayerController on this peer.");
	ASSERT_COND_MSG(peer_2_scene.scene_sync->get_controller_for_peer(server_scene.get_peer())->is_doll_controller(), "This must be a DollController on this peer.");

	ASSERT_COND_MSG(server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->is_server_controller(), "This must be a ServerController on this peer.");
	ASSERT_COND_MSG(peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->is_player_controller(), "This must be a PlayerController on this peer.");
	ASSERT_COND_MSG(peer_2_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->is_doll_controller(), "This must be a DollController on this peer.");

	ASSERT_COND_MSG(server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->is_server_controller(), "This must be a ServerController on this peer.");
	ASSERT_COND_MSG(peer_1_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->is_doll_controller(), "This must be a DollController on this peer.");
	ASSERT_COND_MSG(peer_2_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->is_player_controller(), "This must be a PlayerController on this peer.");

	// Spawn the object controlled by the peer 1
	server_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());
	peer_1_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());
	peer_2_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());

	// Add peer 2 controller.
	server_scene.add_object<LocalNetworkedController>("controller_2", peer_2_scene.get_peer());
	peer_1_scene.add_object<LocalNetworkedController>("controller_2", peer_2_scene.get_peer());
	peer_2_scene.add_object<LocalNetworkedController>("controller_2", peer_2_scene.get_peer());

	// Make sure the realtime is now enabled.
	ASSERT_COND_MSG(server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->can_simulate(), "This must be enabled at this point.");
	ASSERT_COND_MSG(peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->can_simulate(), "This must be enabled at this point.");
	ASSERT_COND_MSG(peer_2_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->can_simulate() == false, "This must be disabled as the server sync never enabled at this point.");

	ASSERT_COND_MSG(server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->can_simulate(), "This must be enabled at this point.");
	ASSERT_COND_MSG(peer_1_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->can_simulate() == false, "This must be disabled as the server sync never enabled at this point.");
	ASSERT_COND_MSG(peer_2_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->can_simulate(), "This must be enabled at this point.");

	// Though the server must be disabled as no objects are being controlled.
	ASSERT_COND_MSG(server_scene.scene_sync->get_controller_for_peer(server_scene.get_peer())->can_simulate() == false, "This must be disabled at this point.");
	ASSERT_COND_MSG(peer_1_scene.scene_sync->get_controller_for_peer(server_scene.get_peer())->can_simulate() == false, "This must be disabled at this point.");
	ASSERT_COND_MSG(peer_2_scene.scene_sync->get_controller_for_peer(server_scene.get_peer())->can_simulate() == false, "This must be disabled at this point.");
}

class TestSceneObject : public NS::LocalSceneObject {
public:
	NS::ObjectLocalId local_id = NS::ObjectLocalId::NONE;

	virtual void on_scene_entry() override {
		get_scene()->scene_sync->register_app_object(get_scene()->scene_sync->to_handle(this));
	}

	virtual void setup_synchronizer(NS::LocalSceneSynchronizer &p_scene_sync, NS::ObjectLocalId p_id) override {
		local_id = p_id;
		p_scene_sync.register_variable(p_id, "var_1");
	}

	virtual void on_scene_exit() override {
		get_scene()->scene_sync->on_app_object_removed(get_scene()->scene_sync->to_handle(this));
	}
};

void test_sync_groups() {
	// ---------------------------------------------------------- INITIALIZATION
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

	// Add 2 objects controlled by the peer 1.
	const NS::ObjectLocalId controlled_obj_1_id = server_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer())->local_id;
	peer_1_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());
	peer_2_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());

	const NS::ObjectLocalId controlled_obj_2_id = server_scene.add_object<LocalNetworkedController>("controller_2", peer_1_scene.get_peer())->local_id;
	peer_1_scene.add_object<LocalNetworkedController>("controller_2", peer_1_scene.get_peer());
	peer_2_scene.add_object<LocalNetworkedController>("controller_2", peer_1_scene.get_peer());

	// Add 2 objects controlled by the peer 2.
	const NS::ObjectLocalId controlled_obj_3_id = server_scene.add_object<LocalNetworkedController>("controller_3", peer_2_scene.get_peer())->local_id;
	peer_1_scene.add_object<LocalNetworkedController>("controller_3", peer_2_scene.get_peer());
	peer_2_scene.add_object<LocalNetworkedController>("controller_3", peer_2_scene.get_peer());

	const NS::ObjectLocalId controlled_obj_4_id = server_scene.add_object<LocalNetworkedController>("controller_4", peer_2_scene.get_peer())->local_id;
	peer_1_scene.add_object<LocalNetworkedController>("controller_4", peer_2_scene.get_peer());
	peer_2_scene.add_object<LocalNetworkedController>("controller_4", peer_2_scene.get_peer());

	// Add an object.
	const NS::ObjectLocalId obj_1_id = server_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer())->local_id;
	peer_1_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer());
	peer_2_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer());

	// Create 3 sync groups
	const NS::SyncGroupId group_1 = server_scene.scene_sync->sync_group_create();
	const NS::SyncGroupId group_2 = server_scene.scene_sync->sync_group_create();
	const NS::SyncGroupId group_3 = server_scene.scene_sync->sync_group_create();

	// ---------------------------------------------------------- ASSERTION FUNC

	// This function is used to assert the object is into the desired sync groups.
	auto assert_group = [](NS::SceneSynchronizerBase *scene_sync, NS::ObjectLocalId p_id, const std::vector<NS::SyncGroupId> &p_expected_simulated_sync_groups, const std::vector<NS::SyncGroupId> &p_expected_trickled_sync_groups, const std::vector<NS::SyncGroupId> &p_not_expected_into_sync_groups) {
		std::vector<NS::SyncGroupId> simulated_groups;
		std::vector<NS::SyncGroupId> trickled_groups;

		scene_sync->sync_group_fetch_object_grups(
				p_id,
				simulated_groups,
				trickled_groups);

		// Make sure the object is into to passed simulated sync groups but not into the trickled.
		for (const NS::SyncGroupId id : p_expected_simulated_sync_groups) {
			ASSERT_COND(NS::VecFunc::has(simulated_groups, id));
			ASSERT_COND(!NS::VecFunc::has(trickled_groups, id));
		}

		// Make sure the object is into to passed trickled sync groups but not into the simulated.
		for (const NS::SyncGroupId id : p_expected_trickled_sync_groups) {
			ASSERT_COND(NS::VecFunc::has(trickled_groups, id));
			ASSERT_COND(!NS::VecFunc::has(simulated_groups, id));
		}

		// Make sure the object is NOT into the passed not expected.
		for (const NS::SyncGroupId id : p_not_expected_into_sync_groups) {
			ASSERT_COND(!NS::VecFunc::has(trickled_groups, id));
			ASSERT_COND(!NS::VecFunc::has(simulated_groups, id));
		}
	};

	auto assert_listening = [](NS::SceneSynchronizerBase *scene_sync, NS::SyncGroupId p_id, const std::vector<int> &p_listening_peers, const std::vector<int> &p_not_listening_peers) {
		const std::vector<int> *listening = scene_sync->sync_group_get_listening_peers(p_id);
		ASSERT_COND(listening);

		for (int peer : p_listening_peers) {
			ASSERT_COND(NS::VecFunc::has(*listening, peer));
		}
		for (int peer : p_not_listening_peers) {
			ASSERT_COND(!NS::VecFunc::has(*listening, peer));
		}
	};

	auto assert_simulating = [](NS::SceneSynchronizerBase *scene_sync, NS::SyncGroupId p_id, const std::vector<int> &p_simulating_peers, const std::vector<int> &p_not_simulating_peers) {
		const std::vector<int> *simulating = scene_sync->sync_group_get_simulating_peers(p_id);
		ASSERT_COND(simulating);

		for (int peer : p_simulating_peers) {
			ASSERT_COND(NS::VecFunc::has(*simulating, peer));
		}
		for (int peer : p_not_simulating_peers) {
			ASSERT_COND(!NS::VecFunc::has(*simulating, peer));
		}
	};

	auto assert_networked = [](NS::SceneSynchronizerBase *scene_sync, NS::SyncGroupId p_id, const std::vector<int> &p_expected_peers, const std::vector<int> &p_not_expected_peers) {
		const NS::SyncGroup *sync_group = scene_sync->sync_group_get(p_id);

		for (int peer : p_expected_peers) {
			ASSERT_COND(NS::VecFunc::has(sync_group->get_networked_peers(), peer));
		}
		for (int peer : p_not_expected_peers) {
			ASSERT_COND(!NS::VecFunc::has(sync_group->get_networked_peers(), peer));
		}
	};

	// ----------------------------------------------------------- TEST DEFAULTS

	// Verify that by default all the peers are listening to the GLOBAL sync group.
	ASSERT_COND(server_scene.scene_sync->sync_group_get_peer_group(server_scene.get_peer()) == NS::SyncGroupId::GLOBAL);
	ASSERT_COND(server_scene.scene_sync->sync_group_get_peer_group(peer_1_scene.get_peer()) == NS::SyncGroupId::GLOBAL);
	ASSERT_COND(server_scene.scene_sync->sync_group_get_peer_group(peer_2_scene.get_peer()) == NS::SyncGroupId::GLOBAL);
	assert_listening(server_scene.scene_sync, NS::SyncGroupId::GLOBAL, { server_scene.get_peer(), peer_1_scene.get_peer(), peer_2_scene.get_peer() }, {});

	// Verify that by default all the objects are into the global group and always as simulating.
	assert_group(server_scene.scene_sync, controlled_obj_1_id, { NS::SyncGroupId::GLOBAL }, {}, { group_1, group_2, group_3 });
	assert_group(server_scene.scene_sync, controlled_obj_2_id, { NS::SyncGroupId::GLOBAL }, {}, { group_1, group_2, group_3 });
	assert_group(server_scene.scene_sync, controlled_obj_3_id, { NS::SyncGroupId::GLOBAL }, {}, { group_1, group_2, group_3 });
	assert_group(server_scene.scene_sync, controlled_obj_4_id, { NS::SyncGroupId::GLOBAL }, {}, { group_1, group_2, group_3 });
	assert_group(server_scene.scene_sync, obj_1_id, { NS::SyncGroupId::GLOBAL }, {}, { group_1, group_2, group_3 });

	assert_listening(server_scene.scene_sync, NS::SyncGroupId::GLOBAL, { server_scene.get_peer(), peer_1_scene.get_peer(), peer_2_scene.get_peer() }, {});
	assert_listening(server_scene.scene_sync, group_1, {}, { server_scene.get_peer(), peer_1_scene.get_peer(), peer_2_scene.get_peer() });
	assert_listening(server_scene.scene_sync, group_2, {}, { server_scene.get_peer(), peer_1_scene.get_peer(), peer_2_scene.get_peer() });
	assert_listening(server_scene.scene_sync, group_3, {}, { server_scene.get_peer(), peer_1_scene.get_peer(), peer_2_scene.get_peer() });

	// Notice that the server, since it doesn't have a controller, is not into any sync group.
	assert_networked(server_scene.scene_sync, NS::SyncGroupId::GLOBAL, { peer_1_scene.get_peer(), peer_2_scene.get_peer() }, { server_scene.get_peer() });
	assert_networked(server_scene.scene_sync, group_1, {}, { server_scene.get_peer(), peer_1_scene.get_peer(), peer_2_scene.get_peer() });
	assert_networked(server_scene.scene_sync, group_2, {}, { server_scene.get_peer(), peer_1_scene.get_peer(), peer_2_scene.get_peer() });
	assert_networked(server_scene.scene_sync, group_3, {}, { server_scene.get_peer(), peer_1_scene.get_peer(), peer_2_scene.get_peer() });

	// ------------------------------------------------ MODIFY GLOBAL SYNC GROUP

	// Try to modify the sync group, and make sure it was not modified as the
	// global sync group can't be modified.
	server_scene.scene_sync->sync_group_add_object(obj_1_id, NS::SyncGroupId::GLOBAL, false);
	assert_group(server_scene.scene_sync, obj_1_id, { NS::SyncGroupId::GLOBAL }, {}, { group_1, group_2, group_3 });

	// Try again with another API.
	server_scene.scene_sync->sync_group_remove_object(obj_1_id, NS::SyncGroupId::GLOBAL);
	assert_group(server_scene.scene_sync, obj_1_id, { NS::SyncGroupId::GLOBAL }, {}, { group_1, group_2, group_3 });

	// ---------------------------------------------------- MOVE LISTENING PEERs

	// Move the peer to a different sync group, and check it.
	server_scene.scene_sync->sync_group_move_peer_to(server_scene.get_peer(), group_1);
	server_scene.scene_sync->sync_group_move_peer_to(peer_1_scene.get_peer(), group_2);
	server_scene.scene_sync->sync_group_move_peer_to(peer_2_scene.get_peer(), group_3);

	assert_listening(server_scene.scene_sync, NS::SyncGroupId::GLOBAL, {}, { server_scene.get_peer(), peer_1_scene.get_peer(), peer_2_scene.get_peer() });
	assert_listening(server_scene.scene_sync, group_1, { server_scene.get_peer() }, { peer_1_scene.get_peer(), peer_2_scene.get_peer() });
	assert_listening(server_scene.scene_sync, group_2, { peer_1_scene.get_peer() }, { server_scene.get_peer(), peer_2_scene.get_peer() });
	assert_listening(server_scene.scene_sync, group_3, { peer_2_scene.get_peer() }, { server_scene.get_peer(), peer_1_scene.get_peer() });

	ASSERT_COND(server_scene.scene_sync->sync_group_get_peer_group(server_scene.get_peer()) == group_1);
	ASSERT_COND(server_scene.scene_sync->sync_group_get_peer_group(peer_1_scene.get_peer()) == group_2);
	ASSERT_COND(server_scene.scene_sync->sync_group_get_peer_group(peer_2_scene.get_peer()) == group_3);

	// Verify that objects didn't change group.
	assert_group(server_scene.scene_sync, controlled_obj_1_id, { NS::SyncGroupId::GLOBAL }, {}, { group_1, group_2, group_3 });
	assert_group(server_scene.scene_sync, controlled_obj_2_id, { NS::SyncGroupId::GLOBAL }, {}, { group_1, group_2, group_3 });
	assert_group(server_scene.scene_sync, controlled_obj_3_id, { NS::SyncGroupId::GLOBAL }, {}, { group_1, group_2, group_3 });
	assert_group(server_scene.scene_sync, controlled_obj_4_id, { NS::SyncGroupId::GLOBAL }, {}, { group_1, group_2, group_3 });
	assert_group(server_scene.scene_sync, obj_1_id, { NS::SyncGroupId::GLOBAL }, {}, { group_1, group_2, group_3 });

	// At this point, there is nothing to listen, so any peer is simulating.
	ASSERT_COND(!server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->server_is_peer_simulating_this_controller(peer_1_scene.get_peer()));
	ASSERT_COND(!server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->server_is_peer_simulating_this_controller(peer_1_scene.get_peer()));
	ASSERT_COND(!server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->server_is_peer_simulating_this_controller(peer_2_scene.get_peer()));
	ASSERT_COND(!server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->server_is_peer_simulating_this_controller(peer_2_scene.get_peer()));

	// ------------------------------------------- MOVE OBJECTS INTO SYNC GROUPs

	//     LISTENERS:                  PEER 1                                 PEER 2
	//                   |   ========= GROUP 2 ============       =========== GROUP 3 ==========
	// Peer 1:           |
	//  |- controller 1  | GROUP-2-SIMULATED  /............../  /............../  /.............../
	//  |- controller 2  | GROUP-2-SIMULATED  /............../  /............../  GROUP-3-TRICKLED
	// Peer 2:           |
	//  |- controller 3  | GROUP-2-SIMULATED  /............../  /............../  /.............../
	//  |- controller 4  | /.............../  GROUP-2-TRICKLED  /............../  /.............../

	// Move the controlled objects 1 and 2 into group 2 as simulated
	// and the controlled object 2 into group 3 as trickled.
	server_scene.scene_sync->sync_group_add_object(controlled_obj_1_id, group_2, true);
	server_scene.scene_sync->sync_group_add_object(controlled_obj_2_id, group_2, true);
	server_scene.scene_sync->sync_group_add_object(controlled_obj_2_id, group_3, false);

	// Now move the controlled objects 3 into group 2 as simulated
	// and the controlled object 4 into group 2 as trickled.
	server_scene.scene_sync->sync_group_add_object(controlled_obj_3_id, group_2, true);
	server_scene.scene_sync->sync_group_add_object(controlled_obj_4_id, group_2, false);

	// Assert the change was made.
	assert_group(server_scene.scene_sync, controlled_obj_1_id, { NS::SyncGroupId::GLOBAL, group_2 }, {}, { group_1, group_3 });
	assert_group(server_scene.scene_sync, controlled_obj_2_id, { NS::SyncGroupId::GLOBAL, group_2 }, { group_3 }, { group_1 });
	assert_group(server_scene.scene_sync, controlled_obj_3_id, { NS::SyncGroupId::GLOBAL, group_2 }, {}, { group_1, group_3 });
	assert_group(server_scene.scene_sync, controlled_obj_4_id, { NS::SyncGroupId::GLOBAL }, { group_2 }, { group_1, group_3 });

	// Assert the networked list is as expected, after the move.
	assert_networked(server_scene.scene_sync, NS::SyncGroupId::GLOBAL, { peer_1_scene.get_peer(), peer_2_scene.get_peer() }, { server_scene.get_peer() });
	assert_networked(server_scene.scene_sync, group_1, {}, { server_scene.get_peer(), peer_1_scene.get_peer(), peer_2_scene.get_peer() });
	assert_networked(server_scene.scene_sync, group_2, { peer_1_scene.get_peer(), peer_2_scene.get_peer() }, { server_scene.get_peer() });
	assert_networked(server_scene.scene_sync, group_3, { peer_1_scene.get_peer() }, { server_scene.get_peer(), peer_2_scene.get_peer() });

	// --------------------------------------------- CONTROLLER SIMULATION CHECK

	// No one is simulating on group 1 and 3
	assert_simulating(server_scene.scene_sync, group_1, {}, { server_scene.get_peer(), peer_1_scene.get_peer(), peer_2_scene.get_peer() });
	assert_simulating(server_scene.scene_sync, group_3, {}, { server_scene.get_peer(), peer_1_scene.get_peer(), peer_2_scene.get_peer() });
	// Peers 1 and 2 are simulating on group 2.
	assert_simulating(server_scene.scene_sync, group_2, { peer_1_scene.get_peer(), peer_2_scene.get_peer() }, { server_scene.get_peer() });

	// The peer 1 is simulating the peer 1 controller, according to the above sync group setup.
	ASSERT_COND(server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->server_is_peer_simulating_this_controller(peer_1_scene.get_peer()));
	// The peer 1 is also simulated by the peer 2 because it's listening on the SYNC GROUP 2.
	ASSERT_COND(server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->server_is_peer_simulating_this_controller(peer_1_scene.get_peer()));

	// The peer 2 is not simulating anything because it's listening on the SYNC GROUP 3.
	ASSERT_COND(!server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->server_is_peer_simulating_this_controller(peer_2_scene.get_peer()));
	ASSERT_COND(!server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->server_is_peer_simulating_this_controller(peer_2_scene.get_peer()));

	// ------------------------------- CONTROLLER SIMULATION CHECK - MOVING PEER

	// Move peer 1 to sync group 1 and verify everything works as expected.
	server_scene.scene_sync->sync_group_move_peer_to(peer_1_scene.get_peer(), group_1);

	// No one is simulating on group 1 despite the peer 1 is on group 1
	assert_simulating(server_scene.scene_sync, group_1, {}, { server_scene.get_peer(), peer_1_scene.get_peer(), peer_2_scene.get_peer() });
	assert_simulating(server_scene.scene_sync, group_3, {}, { server_scene.get_peer(), peer_1_scene.get_peer(), peer_2_scene.get_peer() });
	// Peer 1 and 2 are still simulating on group 2.
	assert_simulating(server_scene.scene_sync, group_2, { peer_1_scene.get_peer(), peer_2_scene.get_peer() }, { server_scene.get_peer() });

	// Also verify the controllers have been updated.
	// Since the objects controlled by peer 1 are on the group 2 and the peer 1
	// is listening the group 1, no controllers are actually sending data to any peer.
	ASSERT_COND(!server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->server_is_peer_simulating_this_controller(peer_1_scene.get_peer()));
	ASSERT_COND(!server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->server_is_peer_simulating_this_controller(peer_1_scene.get_peer()));
	ASSERT_COND(!server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->server_is_peer_simulating_this_controller(peer_2_scene.get_peer()));
	ASSERT_COND(!server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->server_is_peer_simulating_this_controller(peer_2_scene.get_peer()));

	// ----------------------------- CONTROLLER SIMULATION CHECK - MOVING OBJECT

	// Move the peer 1 back to group 2.
	server_scene.scene_sync->sync_group_move_peer_to(peer_1_scene.get_peer(), group_2);
	// Verify
	ASSERT_COND(server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->server_is_peer_simulating_this_controller(peer_1_scene.get_peer()));
	ASSERT_COND(server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->server_is_peer_simulating_this_controller(peer_1_scene.get_peer()));

	// Now, remove the object controlled by the peer 2 from group 2.
	server_scene.scene_sync->sync_group_remove_object(controlled_obj_3_id, group_2);

	// Make sure the peer 2 controller is not simulating on peer 1 anylonger.
	ASSERT_COND(!server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->server_is_peer_simulating_this_controller(peer_1_scene.get_peer()));
	// Also make sure the peer 2 is not simulating on peer 2.
	ASSERT_COND(!server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->server_is_peer_simulating_this_controller(peer_2_scene.get_peer()));

	// Move the peer 2 to group 1
	server_scene.scene_sync->sync_group_move_peer_to(peer_2_scene.get_peer(), group_1);

	// Make sure that the peer 2 is not yet simulating the peer 2.
	ASSERT_COND(!server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->server_is_peer_simulating_this_controller(peer_2_scene.get_peer()));

	// Verify that the controlled_3 is only on the global group.
	assert_group(server_scene.scene_sync, controlled_obj_3_id, { NS::SyncGroupId::GLOBAL }, {}, { group_1, group_2, group_3 });

	// Add the object to group 1
	server_scene.scene_sync->sync_group_add_object(controlled_obj_3_id, group_1, true);

	// Make sure that the peer 2 is now simulating on peer 2.
	ASSERT_COND(server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->server_is_peer_simulating_this_controller(peer_2_scene.get_peer()));

	// -------------------------------- MOVING OBJECT FROM SIMULATED TO TRICKLED

	// Verify that the controlled_3 is only on the global group.
	assert_group(server_scene.scene_sync, controlled_obj_3_id, { NS::SyncGroupId::GLOBAL, group_1 }, {}, { group_2, group_3 });

	// Change the object mode from simulated to trickled.
	server_scene.scene_sync->sync_group_add_object(controlled_obj_3_id, group_1, false);

	// Make sure the controlled_3 is trickling.
	assert_group(server_scene.scene_sync, controlled_obj_3_id, { NS::SyncGroupId::GLOBAL }, { group_1 }, { group_2, group_3 });

	// So, make sure the peer 2 is not simulating anymore on peer 2.
	ASSERT_COND(!server_scene.scene_sync->get_controller_for_peer(peer_2_scene.get_peer())->server_is_peer_simulating_this_controller(peer_2_scene.get_peer()));

	// The peer 2 is still networked despite not anylonger simulating.
	assert_networked(server_scene.scene_sync, group_1, { peer_2_scene.get_peer() }, { server_scene.get_peer(), peer_1_scene.get_peer() });
}

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

	server_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer());
	peer_1_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer());
	peer_2_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer());

	for (int f = 0; f < 2; f++) {
		// Test with notify interval set to 0
		{
			server_scene.scene_sync->set_frame_confirmation_timespan(0.0);

			// Set the `var_1` to a different value to all the clients.
			server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 0;
			peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 1;
			peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 2;
			ASSERT_COND(server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 0);
			ASSERT_COND(peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 1);
			ASSERT_COND(peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 2);

			// Process exactly 1 time.
			// NOTE: Processing the controller so the server receives the input right after.
			server_scene.process(delta);
			peer_1_scene.process(delta);
			peer_2_scene.process(delta);

			// The notification interval is set to 0 therefore the server sends
			// the snapshot right away: since the server snapshot is always
			// at least one frame behind the client, we can assume that the
			// client has applied the server correction.
			ASSERT_COND(server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 0);
			ASSERT_COND(peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 0);
			ASSERT_COND(peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 0);
		}

		// Test with notify interval set to 0.5 seconds.
		{
			server_scene.scene_sync->set_frame_confirmation_timespan(0.5);

			// Set the `var_1` to a different value to all the clients.
			server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 3;
			peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 4;
			peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 5;

			// Process for 0.5 second + delta
			float time = 0.0;
			for (; time <= 0.5 + delta + 0.001; time += delta) {
				// NOTE: Processing the controller so the server receives the input right after.
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);

				if (
						server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 3 &&
						peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 3 &&
						peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 3) {
					break;
				}
			}

			// The notification interval is set to 0.5 therefore the server sends
			// the snapshot after some 0.5s: since the server snapshot is always
			// at least one frame behind the client, we can assume that the
			// client has applied the server correction.
			ASSERT_COND(server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 3);
			ASSERT_COND(peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 3);
			ASSERT_COND(peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 3);
			ASSERT_COND(time < 0.5);
		}

		// Test by making sure the Scene Sync is able to sync when the variable
		// changes only on the client side.
		{
			// No local controller, therefore the correction is applied by the
			// server right away.
			server_scene.scene_sync->set_frame_confirmation_timespan(0.0);

			// The server remains like it was.
			// server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"] = 3;
			// While the peers change its variables.
			peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 4;
			peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 5;

			if (f == 0) {
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);

				// Still the value expected is `3`.
				ASSERT_COND(server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 3);
				ASSERT_COND(peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 3);
				ASSERT_COND(peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 3);
			} else {
				// Note: the +1 is needed because the change is recored on the snapshot
				// the scene sync is going to created on the next "process".
				const NS::FrameIndex change_made_on_frame = peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index() + 1;

				// When the local controller is set, the client scene sync compares the
				// received server snapshot with the one recorded on the client on
				// the same frame.
				// Since the above code is altering the variable on the client
				// and not on the server, which is 1 frame ahead the server,
				// the client will detects such change when it receives
				// the snapshot for the same (or newer) frame.

				// For the above reason we have to process the scenes multiple times,
				// before seeing the value correctly applied.
				// The reason is that the client scene sync creates the snapshot
				// right before the `process` function terminates: meaning that
				// the change made above is registered on the "next" frame.
				// So, the server have to be processed three times to catch the client.
				for (int h = 0; h < 10; h++) {
					server_scene.process(delta);
					peer_1_scene.process(delta);
					peer_2_scene.process(delta);

					// However, since the `peer_2` doesn't have the local controller
					// the server snapshot is expected to be applied right away.
					ASSERT_COND(peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 3);

					if (change_made_on_frame == server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index()) {
						// Break as soon as the server reaches the same snapshot.
						break;
					}
				}

				// Make sure the server is indeed at the same frame on which the
				// client made the change.
				ASSERT_COND(change_made_on_frame == server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index());

				// and now is time to check for the `peer_1`.
				ASSERT_COND(peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 == 3);
			}
		}

		if (f == 0) {
			// Now add the PlayerControllers and test the above mechanism still works.
			server_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());
			peer_1_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());
			peer_2_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());

			// Process three times to make sure all the peers are initialized at thie time.
			for (int j = 0; j < 2; j++) {
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);
			}

			ASSERT_COND(server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index() == NS::FrameIndex{ { 0 } });
			ASSERT_COND(peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index() == NS::FrameIndex{ { 1 } });
			// NOTE: No need to check the peer_2, because it's not an authoritative controller anyway.
		} else {
			// Make sure the controllers have been processed at this point.
			ASSERT_COND(server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index() != NS::FrameIndex{ { 0 } });
			ASSERT_COND(server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index() != NS::FrameIndex::NONE);
			ASSERT_COND(peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index() != NS::FrameIndex{ { 0 } });
			ASSERT_COND(peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index() != NS::FrameIndex::NONE);

			// NOTE: No need to check the peer_2, because it's not an authoritative controller anyway.
		}
	}
}

void test_processing_with_late_controller_registration() {
	// This test make sure that the peer receives the server updates ASAP, despite
	// the `notify_interval` set.
	// This is important becouse unless the client receives the NetId for its
	// local controller, the controller can't generate the first input.

	NS::LocalScene server_scene;
	server_scene.start_as_server();

	NS::LocalScene peer_1_scene;
	peer_1_scene.start_as_client(server_scene);

	// Add the scene sync
	server_scene.scene_sync =
			server_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());
	peer_1_scene.scene_sync =
			peer_1_scene.add_object<NS::LocalSceneSynchronizer>("sync", server_scene.get_peer());

	server_scene.scene_sync->to_handle(server_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer()));
	peer_1_scene.scene_sync->to_handle(peer_1_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer()));

	// Quite high notify state interval, to make sure the snapshot is not sent "soon".
	server_scene.scene_sync->set_frame_confirmation_timespan(10.0);

	// Process all the peers, so the initial setup is performed.
	server_scene.process(delta);
	peer_1_scene.process(delta);

	// Now add the PlayerControllers.
	server_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());
	peer_1_scene.add_object<LocalNetworkedController>("controller_1", peer_1_scene.get_peer());

	// Process two times.
	for (int j = 0; j < 2; j++) {
		server_scene.process(delta);
		peer_1_scene.process(delta);
	}

	// Make sure the client can process right away as the NetId is networked
	// already.
	ASSERT_COND(server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index() == NS::FrameIndex{ { 0 } });
	ASSERT_COND(peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index() == NS::FrameIndex{ { 1 } });
}

void test_snapshot_generation() {
	// TODO implement this.
}

void test_state_notify_for_no_rewind_properties() {
	// TODO implement this.
}

void test_variable_change_event() {
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

	NS::ObjectLocalId server_obj_1_oh = server_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer())->find_local_id();
	NS::ObjectLocalId p1_obj_1_oh = peer_1_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer())->find_local_id();
	NS::ObjectLocalId p2_obj_1_oh = peer_2_scene.add_object<TestSceneObject>("obj_1", server_scene.get_peer())->find_local_id();

	for (int f = 0; f < 2; f++) {
		// Test the changed variable for the event `CHANGE` is triggered.
		{
			bool is_server_change_event_triggered = false;
			bool is_p1_change_event_triggered = false;
			bool is_p2_change_event_triggered = false;

			NS::ListenerHandle server_lh = server_scene.scene_sync->track_variable_changes(
					server_obj_1_oh, "var_1", [&is_server_change_event_triggered](const std::vector<NS::VarData> &p_old_values) {
						is_server_change_event_triggered = true;
					},
					NetEventFlag::CHANGE);

			NS::ListenerHandle p1_lh = peer_1_scene.scene_sync->track_variable_changes(
					p1_obj_1_oh, "var_1", [&is_p1_change_event_triggered](const std::vector<NS::VarData> &p_old_values) {
						is_p1_change_event_triggered = true;
					},
					NetEventFlag::CHANGE);

			NS::ListenerHandle p2_lh = peer_2_scene.scene_sync->track_variable_changes(
					p2_obj_1_oh, "var_1", [&is_p2_change_event_triggered](const std::vector<NS::VarData> &p_old_values) {
						is_p2_change_event_triggered = true;
					},
					NetEventFlag::CHANGE);

			server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 2;
			peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 3;
			peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 4;

			peer_1_scene.process(delta);
			peer_2_scene.process(delta);

			ASSERT_COND(!is_server_change_event_triggered);
			ASSERT_COND(is_p1_change_event_triggered);
			ASSERT_COND(is_p2_change_event_triggered);

			// Now check it's triggered on the server too.
			// NOTE: processing after the clients, so we do not trigger the
			//       snapshot that would trigger the event.
			server_scene.process(delta);

			ASSERT_COND(is_server_change_event_triggered);

			// Now reset everything and process again without chaning the values
			is_server_change_event_triggered = false;
			is_p1_change_event_triggered = false;
			is_p2_change_event_triggered = false;

			for (int i = 0; i < 4; i++) {
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);
			}

			// Make sure the events are not called.
			ASSERT_COND(!is_server_change_event_triggered);
			ASSERT_COND(!is_p1_change_event_triggered);
			ASSERT_COND(!is_p2_change_event_triggered);

			// Now unregister the listeners.
			server_scene.scene_sync->untrack_variable_changes(server_lh);
			peer_1_scene.scene_sync->untrack_variable_changes(p1_lh);
			peer_2_scene.scene_sync->untrack_variable_changes(p2_lh);

			// Reset everything
			is_server_change_event_triggered = false;
			is_p1_change_event_triggered = false;
			is_p2_change_event_triggered = false;

			// Change the values
			server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 30;
			peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 30;
			peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 30;

			// Process again
			for (int i = 0; i < 4; i++) {
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);
			}

			// and make sure the events are not being called.
			ASSERT_COND(!is_server_change_event_triggered);
			ASSERT_COND(!is_p1_change_event_triggered);
			ASSERT_COND(!is_p2_change_event_triggered);
		}

		// Test the change event is triggered for the event `SYNC_RECONVER`
		{
			// Unify the state across all the peers
			server_scene.scene_sync->set_frame_confirmation_timespan(0.0);

			server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 0;
			peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 0;
			peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 0;

			for (int i = 0; i < 4; i++) {
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);
			}

			bool is_server_change_event_triggered = false;
			bool is_p1_change_event_triggered = false;
			bool is_p2_change_event_triggered = false;

			NS::ListenerHandle server_lh = server_scene.scene_sync->track_variable_changes(
					server_obj_1_oh, "var_1", [&is_server_change_event_triggered](const std::vector<NS::VarData> &p_old_values) {
						is_server_change_event_triggered = true;
					},
					NetEventFlag::SYNC_RECOVER);

			NS::ListenerHandle p1_lh = peer_1_scene.scene_sync->track_variable_changes(
					p1_obj_1_oh, "var_1", [&is_p1_change_event_triggered](const std::vector<NS::VarData> &p_old_values) {
						is_p1_change_event_triggered = true;
					},
					NetEventFlag::SYNC_RECOVER);

			NS::ListenerHandle p2_lh = peer_2_scene.scene_sync->track_variable_changes(
					p2_obj_1_oh, "var_1", [&is_p2_change_event_triggered](const std::vector<NS::VarData> &p_old_values) {
						is_p2_change_event_triggered = true;
					},
					NetEventFlag::SYNC_RECOVER);

			// Change the value on the server.
			server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 1;

			for (int i = 0; i < 4; i++) {
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);
			}

			// Make sure the event on the server was not triggered
			ASSERT_COND(!is_server_change_event_triggered);
			// But it was on the peers.
			ASSERT_COND(is_p1_change_event_triggered);
			ASSERT_COND(is_p2_change_event_triggered);

			// Now unregister the listeners.
			server_scene.scene_sync->untrack_variable_changes(server_lh);
			peer_1_scene.scene_sync->untrack_variable_changes(p1_lh);
			peer_2_scene.scene_sync->untrack_variable_changes(p2_lh);
		}

		// Test the change event is triggered for the event `SYNC_RESET`
		if (false) {
			// Unify the state across all the peers
			server_scene.scene_sync->set_frame_confirmation_timespan(0.0);

			server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 0;
			peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 0;
			peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 0;

			for (int i = 0; i < 4; i++) {
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);
			}

			bool is_server_change_event_triggered = false;
			bool is_p1_change_event_triggered = false;
			bool is_p2_change_event_triggered = false;

			NS::ListenerHandle server_lh = server_scene.scene_sync->track_variable_changes(
					server_obj_1_oh, "var_1", [&is_server_change_event_triggered, &server_scene](const std::vector<NS::VarData> &p_old_values) {
						is_server_change_event_triggered = true;
						ASSERT_COND(server_scene.scene_sync->is_resetted());
					},
					NetEventFlag::SYNC_RESET);

			NS::ListenerHandle p1_lh = peer_1_scene.scene_sync->track_variable_changes(
					p1_obj_1_oh, "var_1", [&is_p1_change_event_triggered, &peer_1_scene](const std::vector<NS::VarData> &p_old_values) {
						is_p1_change_event_triggered = true;
						ASSERT_COND(peer_1_scene.scene_sync->is_resetted());
					},
					NetEventFlag::SYNC_RESET);

			NS::ListenerHandle p2_lh = peer_2_scene.scene_sync->track_variable_changes(
					p2_obj_1_oh, "var_1", [&is_p2_change_event_triggered, &peer_2_scene](const std::vector<NS::VarData> &p_old_values) {
						is_p2_change_event_triggered = true;
						ASSERT_COND(peer_2_scene.scene_sync->is_resetted());
					},
					NetEventFlag::SYNC_RESET);

			// Mark the parameter as skip rewinding first.
			server_scene.scene_sync->set_skip_rewinding(server_obj_1_oh, "var_1", true);
			peer_1_scene.scene_sync->set_skip_rewinding(p1_obj_1_oh, "var_1", true);
			peer_2_scene.scene_sync->set_skip_rewinding(p2_obj_1_oh, "var_1", true);

			// Change the value on the server.
			server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 1;

			for (int i = 0; i < 4; i++) {
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);
			}

			// Make sure the event was not triggered on anyone since we are
			// skipping the rewinding.
			ASSERT_COND(!is_server_change_event_triggered);
			ASSERT_COND(!is_p1_change_event_triggered);
			ASSERT_COND(!is_p2_change_event_triggered);

			// Now set the var as rewinding.
			server_scene.scene_sync->set_skip_rewinding(server_obj_1_oh, "var_1", false);
			peer_1_scene.scene_sync->set_skip_rewinding(p1_obj_1_oh, "var_1", false);
			peer_2_scene.scene_sync->set_skip_rewinding(p2_obj_1_oh, "var_1", false);

			// Change the value on the server.
			server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 = 10;

			for (int i = 0; i < 4; i++) {
				server_scene.process(delta);
				peer_1_scene.process(delta);
				peer_2_scene.process(delta);
			}

			// Make sure the event was triggered now.
			ASSERT_COND(is_p1_change_event_triggered);
			ASSERT_COND(is_p2_change_event_triggered);

			// Now unregister the listeners.
			server_scene.scene_sync->untrack_variable_changes(server_lh);
			peer_1_scene.scene_sync->untrack_variable_changes(p1_lh);
			peer_2_scene.scene_sync->untrack_variable_changes(p2_lh);
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

			ASSERT_COND(server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index() == NS::FrameIndex{ { 0 } });
			ASSERT_COND(peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index() == NS::FrameIndex{ { 1 } });
			// NOTE: No need to check the peer_2, because it's not an authoritative controller anyway.
		} else {
			// Make sure the controllers have been processed at this point.
			ASSERT_COND(server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index() != NS::FrameIndex{ { 0 } });
			ASSERT_COND(server_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index() != NS::FrameIndex::NONE);
			ASSERT_COND(peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index() != NS::FrameIndex{ { 0 } });
			ASSERT_COND(peer_1_scene.scene_sync->get_controller_for_peer(peer_1_scene.get_peer())->get_current_frame_index() != NS::FrameIndex::NONE);

			// NOTE: No need to check the peer_2, because it's not an authoritative controller anyway.
		}
	}
}

void test_controller_processing() {
	// TODO implement this.
}

void test_streaming() {
	// TODO implement this.
}

void test_scene_synchronizer() {
	test_ids();
	test_client_and_server_initialization();
	test_sync_groups();
	test_state_notify();
	test_processing_with_late_controller_registration();
	test_snapshot_generation();
	test_state_notify_for_no_rewind_properties();
	test_variable_change_event();
	test_controller_processing();
	test_streaming();
}
}; //namespace NS_Test
