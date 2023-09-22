#include "local_scene.h"
#include "modules/network_synchronizer/core/core.h"

NS_NAMESPACE_BEGIN

void LocalScene::start_as_server() {
	network.start_as_server();
}

void LocalScene::start_as_client(LocalScene &p_server) {
	network.start_as_client(p_server.network);

	// TODO clone the server scene now.
}

int LocalScene::get_peer() const {
	return network.get_peer();
}

void LocalScene::process(float p_delta) {
}

NS_NAMESPACE_END
