#include "test_scene_synchronizer.h"

#include "core/error/error_macros.h"
#include "core/math/vector3.h"
#include "core/variant/variant.h"
#include "local_scene.h"
#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/core/var_data.h"
#include "modules/network_synchronizer/net_utilities.h"
#include "modules/network_synchronizer/tests/local_network.h"

namespace NS_Test {

// TODO enable tests again.
/*
void test_ids() {
	NS::VarId var_id_0 = { 0 };
	NS::VarId var_id_0_2 = { 0 };
	NS::VarId var_id_1 = { 1 };

	CRASH_COND(var_id_0 != var_id_0_2);
	CRASH_COND(var_id_0 == var_id_1);
	CRASH_COND(var_id_0 > var_id_1);
	CRASH_COND(var_id_0 >= var_id_1);
	CRASH_COND(var_id_1 < var_id_0);
	CRASH_COND(var_id_1 <= var_id_0);

	NS::VarId var_id_2 = var_id_1 + 1;
	CRASH_COND(var_id_2.id != 2);

	NS::VarId var_id_3 = var_id_0;
	var_id_3 += var_id_1;
	var_id_3 += int(1);
	var_id_3 += uint32_t(1);
	CRASH_COND(var_id_3.id != 3);
}

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
			CRASH_COND(server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 != 0);
			CRASH_COND(peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 != 1);
			CRASH_COND(peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 != 2);

			// Process exactly 1 time.
			// NOTE: Processing the controller so the server receives the input right after.
			server_scene.process(delta);
			peer_1_scene.process(delta);
			peer_2_scene.process(delta);

			// The notification interval is set to 0 therefore the server sends
			// the snapshot right away: since the server snapshot is always
			// at least one frame behind the client, we can assume that the
			// client has applied the server correction.
			CRASH_COND(server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 != 0);
			CRASH_COND(peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 != 0);
			CRASH_COND(peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 != 0);
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
			CRASH_COND(server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 != 3);
			CRASH_COND(peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 != 3);
			CRASH_COND(peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 != 3);
			CRASH_COND(time >= 0.5);
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
				CRASH_COND(server_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 != 3);
				CRASH_COND(peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 != 3);
				CRASH_COND(peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 != 3);
			} else {
				// Note: the +1 is needed because the change is recored on the snapshot
				// the scene sync is going to created on the next "process".
				const NS::FrameIndex change_made_on_frame = peer_1_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index() + 1;

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
					CRASH_COND(peer_2_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 != 3);

					if (change_made_on_frame == server_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index()) {
						// Break as soon as the server reaches the same snapshot.
						break;
					}
				}

				// Make sure the server is indeed at the same frame on which the
				// client made the change.
				CRASH_COND(change_made_on_frame != server_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index());

				// and now is time to check for the `peer_1`.
				CRASH_COND(peer_1_scene.fetch_object<TestSceneObject>("obj_1")->variables["var_1"].data.i32 != 3);
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

			CRASH_COND(server_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index() != NS::FrameIndex{ 0 });
			CRASH_COND(peer_1_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index() != NS::FrameIndex{ 1 });
			// NOTE: No need to check the peer_2, because it's not an authoritative controller anyway.
		} else {
			// Make sure the controllers have been processed at this point.
			CRASH_COND(server_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index() == NS::FrameIndex{ 0 });
			CRASH_COND(server_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index() == NS::FrameIndex::NONE);
			CRASH_COND(peer_1_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index() == NS::FrameIndex{ 0 });
			CRASH_COND(peer_1_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index() == NS::FrameIndex::NONE);

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
	CRASH_COND(server_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index() != NS::FrameIndex{ 0 });
	CRASH_COND(peer_1_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index() != NS::FrameIndex{ 1 });
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

			CRASH_COND(is_server_change_event_triggered);
			CRASH_COND(!is_p1_change_event_triggered);
			CRASH_COND(!is_p2_change_event_triggered);

			// Now check it's triggered on the server too.
			// NOTE: processing after the clients, so we do not trigger the
			//       snapshot that would trigger the event.
			server_scene.process(delta);

			CRASH_COND(!is_server_change_event_triggered);

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
			CRASH_COND(is_server_change_event_triggered);
			CRASH_COND(is_p1_change_event_triggered);
			CRASH_COND(is_p2_change_event_triggered);

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
			CRASH_COND(is_server_change_event_triggered);
			CRASH_COND(is_p1_change_event_triggered);
			CRASH_COND(is_p2_change_event_triggered);
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
			CRASH_COND(is_server_change_event_triggered);
			// But it was on the peers.
			CRASH_COND(!is_p1_change_event_triggered);
			CRASH_COND(!is_p2_change_event_triggered);

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
						CRASH_COND(!server_scene.scene_sync->is_resetted());
					},
					NetEventFlag::SYNC_RESET);

			NS::ListenerHandle p1_lh = peer_1_scene.scene_sync->track_variable_changes(
					p1_obj_1_oh, "var_1", [&is_p1_change_event_triggered, &peer_1_scene](const std::vector<NS::VarData> &p_old_values) {
						is_p1_change_event_triggered = true;
						CRASH_COND(!peer_1_scene.scene_sync->is_resetted());
					},
					NetEventFlag::SYNC_RESET);

			NS::ListenerHandle p2_lh = peer_2_scene.scene_sync->track_variable_changes(
					p2_obj_1_oh, "var_1", [&is_p2_change_event_triggered, &peer_2_scene](const std::vector<NS::VarData> &p_old_values) {
						is_p2_change_event_triggered = true;
						CRASH_COND(!peer_2_scene.scene_sync->is_resetted());
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
			CRASH_COND(is_server_change_event_triggered);
			CRASH_COND(is_p1_change_event_triggered);
			CRASH_COND(is_p2_change_event_triggered);

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
			CRASH_COND(!is_p1_change_event_triggered);
			CRASH_COND(!is_p2_change_event_triggered);

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

			CRASH_COND(server_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index() != NS::FrameIndex{ 0 });
			CRASH_COND(peer_1_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index() != NS::FrameIndex{ 1 });
			// NOTE: No need to check the peer_2, because it's not an authoritative controller anyway.
		} else {
			// Make sure the controllers have been processed at this point.
			CRASH_COND(server_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index() == NS::FrameIndex{ 0 });
			CRASH_COND(server_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index() == NS::FrameIndex::NONE);
			CRASH_COND(peer_1_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index() == NS::FrameIndex{ 0 });
			CRASH_COND(peer_1_scene.fetch_object<LocalNetworkedController>("controller_1")->get_current_frame_index() == NS::FrameIndex::NONE);

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
*/

void test_scene_synchronizer() {
	//test_ids();
	//test_client_and_server_initialization();
	//test_state_notify();
	//test_processing_with_late_controller_registration();
	//test_snapshot_generation();
	//test_state_notify_for_no_rewind_properties();
	//test_variable_change_event();
	//test_controller_processing();
	//test_streaming();
}
}; //namespace NS_Test
