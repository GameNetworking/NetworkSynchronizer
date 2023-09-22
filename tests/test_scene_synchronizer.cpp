
#include "core/error/error_macros.h"
#include "local_scene.h"

namespace NS_Test {

void test_scene_processing() {
	NS::LocalScene server;
	server.start_as_server();

	NS::LocalScene peer_1;
	peer_1.start_as_client(server);

	NS::LocalScene peer_2;
	peer_2.start_as_client(server);

	// Add the scene sync
	server.add_object<NS::LocalSceneSynchronizer>("sync", server.get_peer());
	peer_1.add_object<NS::LocalSceneSynchronizer>("sync", server.get_peer());
	peer_2.add_object<NS::LocalSceneSynchronizer>("sync", server.get_peer());

	// Add peer 1 controller.
	server.add_object<NS::LocalNetworkedController>("controller_1", peer_1.get_peer());
	peer_1.add_object<NS::LocalNetworkedController>("controller_1", peer_1.get_peer());
	peer_2.add_object<NS::LocalNetworkedController>("controller_1", peer_1.get_peer());

	// Add peer 2 controller.
	server.add_object<NS::LocalNetworkedController>("controller_2", peer_2.get_peer());
	peer_1.add_object<NS::LocalNetworkedController>("controller_2", peer_2.get_peer());
	peer_2.add_object<NS::LocalNetworkedController>("controller_2", peer_2.get_peer());
}

void test_scene_synchronizer() {
	test_scene_processing();
}
}; //namespace NS_Test
