#include "local_scene.h"
#include "modules/network_synchronizer/core/core.h"
#include <memory>

NS_NAMESPACE_BEGIN

LocalScene *LocalSceneObject::get_scene() const {
	return scene_owner;
}

void LocalScene::start_as_server() {
	network.start_as_server();
}

void LocalScene::start_as_client(LocalScene &p_server) {
	network.start_as_client(p_server.network);
}

int LocalScene::get_peer() const {
	return network.get_peer();
}

std::shared_ptr<LocalSceneObject> LocalScene::fetch_object(const char *p_object_name) {
	std::map<std::string, std::shared_ptr<LocalSceneObject>>::iterator obj_it = objects.find(p_object_name);
	if (obj_it != objects.end()) {
		return obj_it->second;
	}
	return std::shared_ptr<LocalSceneObject>();
}

void LocalScene::remove_object(const char *p_object_name) {
	std::map<std::string, std::shared_ptr<LocalSceneObject>>::iterator obj_it = objects.find(p_object_name);
	if (obj_it != objects.end()) {
		std::shared_ptr<LocalSceneObject> obj = obj_it->second;
		obj->scene_owner = nullptr;
		obj->on_scene_exit();
		objects.erase(obj_it);
	}
}

void LocalScene::process(float p_delta) {
}

NS_NAMESPACE_END
