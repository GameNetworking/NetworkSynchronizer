#include "local_scene.h"
#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/networked_controller.h"
#include <memory>

NS_NAMESPACE_BEGIN

LocalSceneSynchronizer::LocalSceneSynchronizer() {
}

void LocalSceneSynchronizer::on_scene_entry() {
	network_interface.init(get_scene()->get_network(), name);
	setup(network_interface, *this);
	register_app_object(this);
}

void LocalSceneSynchronizer::setup_synchronizer(class LocalSceneSynchronizer &p_scene_sync) {
}

void LocalSceneSynchronizer::on_scene_exit() {
	on_app_object_removed(this);
}

void *LocalSceneSynchronizer::fetch_app_object(const std::string &p_object_name) {
	std::shared_ptr<LocalSceneObject> lso = get_scene()->fetch_object(p_object_name);
	if (lso) {
		return lso.get();
	}
}

uint64_t LocalSceneSynchronizer::get_object_id(const void *p_app_object) const {
	// No Object ID.
	return 0;
}

std::string LocalSceneSynchronizer::get_object_name(const void *p_app_object) const {
	return static_cast<const LocalSceneObject *>(p_app_object)->name;
}

void LocalSceneSynchronizer::setup_synchronizer_for(void *p_app_object) {
	static_cast<LocalSceneObject *>(p_app_object)->setup_synchronizer(*get_scene()->scene_sync);
}

void LocalSceneSynchronizer::set_variable(void *p_app_object, const char *p_var_name, const Variant &p_val) {
	LocalSceneObject *lso = static_cast<LocalSceneObject *>(p_app_object);
	auto element = lso->variables.find(std::string(p_var_name));
	if (element != lso->variables.end()) {
		element->second = p_val;
	}
}

bool LocalSceneSynchronizer::get_variable(const void *p_app_object, const char *p_var_name, Variant &p_val) const {
	const LocalSceneObject *lso = static_cast<const LocalSceneObject *>(p_app_object);
	auto element = lso->variables.find(std::string(p_var_name));
	if (element != lso->variables.end()) {
		p_val = element->second;
		return true;
	} else {
		return false;
	}
}

NS::NetworkedController *LocalSceneSynchronizer::extract_network_controller(void *p_app_object) const {
	return dynamic_cast<NS::NetworkedController *>(static_cast<LocalSceneObject *>(p_app_object));
}

const NS::NetworkedController *LocalSceneSynchronizer::extract_network_controller(const void *p_app_object) const {
	return dynamic_cast<const NS::NetworkedController *>(static_cast<const LocalSceneObject *>(p_app_object));
}

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

std::shared_ptr<LocalSceneObject> LocalScene::fetch_object(const std::string &p_object_name) {
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
		obj->on_scene_exit();
		obj->scene_owner = nullptr;
		objects.erase(obj_it);
	}
}

void LocalScene::process(float p_delta) {
	network.process(p_delta);
	scene_sync->process();
}

NS_NAMESPACE_END
