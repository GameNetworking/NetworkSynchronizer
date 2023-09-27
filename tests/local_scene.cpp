#include "local_scene.h"
#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/networked_controller.h"
#include <memory>

NS_NAMESPACE_BEGIN

LocalSceneSynchronizer::LocalSceneSynchronizer() {
}

void LocalSceneSynchronizer::on_scene_entry() {
	get_network_interface().init(get_scene()->get_network(), name, authoritative_peer_id);
	setup(*this);
	register_app_object(to_handle(this));
}

void LocalSceneSynchronizer::setup_synchronizer(class LocalSceneSynchronizer &p_scene_sync) {
}

void LocalSceneSynchronizer::on_scene_exit() {
	on_app_object_removed(to_handle(this));
}

ObjectHandle LocalSceneSynchronizer::fetch_app_object(const std::string &p_object_name) {
	LocalSceneObject *lso = get_scene()->fetch_object<LocalSceneObject>(p_object_name.c_str());
	if (lso) {
		return to_handle(lso);
	}
}

uint64_t LocalSceneSynchronizer::get_object_id(ObjectHandle p_app_object_handle) const {
	// No Object ID.
	return 0;
}

std::string LocalSceneSynchronizer::get_object_name(ObjectHandle p_app_object_handle) const {
	return from_handle(p_app_object_handle)->name;
}

void LocalSceneSynchronizer::setup_synchronizer_for(ObjectHandle p_app_object_handle) {
	from_handle(p_app_object_handle)->setup_synchronizer(*get_scene()->scene_sync);
}

void LocalSceneSynchronizer::set_variable(ObjectHandle p_app_object_handle, const char *p_var_name, const Variant &p_val) {
	LocalSceneObject *lso = from_handle(p_app_object_handle);
	auto element = lso->variables.find(std::string(p_var_name));
	if (element != lso->variables.end()) {
		element->second = p_val;
	}
}

bool LocalSceneSynchronizer::get_variable(const ObjectHandle p_app_object_handle, const char *p_var_name, Variant &p_val) const {
	const LocalSceneObject *lso = from_handle(p_app_object_handle);
	auto element = lso->variables.find(std::string(p_var_name));
	if (element != lso->variables.end()) {
		p_val = element->second;
		return true;
	} else {
		// For convenience, this never fails.
		p_val = Variant();
		return true;
	}
}

NS::NetworkedControllerBase *LocalSceneSynchronizer::extract_network_controller(ObjectHandle p_app_object_handle) {
	return dynamic_cast<NS::NetworkedControllerBase *>(from_handle(p_app_object_handle));
}

const NS::NetworkedControllerBase *LocalSceneSynchronizer::extract_network_controller(const ObjectHandle p_app_object_handle) const {
	return dynamic_cast<const NS::NetworkedControllerBase *>(from_handle(p_app_object_handle));
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
	scene_sync->process();
	// Clear any pending RPC.
	// NOTE: The network process is executed after the scene_sync so any pending
	// 			rpc is dispatched right away. When the rpc is sent, it's received
	// 			right away, so it's not needed to process it before the scene_sync.
	network.process(p_delta);
}

NS_NAMESPACE_END
