
#include "core/error/error_macros.h"
#include "local_scene.h"
#include "modules/network_synchronizer/tests/local_network.h"

namespace NS_Test {

void test_client_server_sync() {
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

	const float delta = 1.0 / 60.0;
	for (float t = 0.0; t < 2.0; t += delta) {
		server.process(delta);
		peer_1.process(delta);
		peer_2.process(delta);
	}
}

void test_scene_synchronizer() {
	test_client_server_sync();
}
}; //namespace NS_Test
