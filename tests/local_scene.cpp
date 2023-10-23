#include "local_scene.h"
#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/networked_controller.h"
#include <memory>

NS_NAMESPACE_BEGIN

LocalSceneObject::~LocalSceneObject() {}

NS::ObjectLocalId LocalSceneObject::find_local_id() const {
	if (get_scene()) {
		return get_scene()->scene_sync->find_object_local_id(get_scene()->scene_sync->to_handle(this));
	}
	return NS::ObjectLocalId::NONE;
}

LocalSceneSynchronizer::LocalSceneSynchronizer() {
}

LocalSceneSynchronizer::~LocalSceneSynchronizer() {}

void LocalSceneSynchronizer::register_local_sync() {
	register_var_data_functions(
			[](DataBuffer &r_buffer, const NS::VarData &p_val) {
				r_buffer.add(p_val.type);
				r_buffer.add_bits(reinterpret_cast<const uint8_t *>(&p_val.data), sizeof(p_val.data) * 8);
				// Not supported right now.
				CRASH_COND(p_val.shared_buffer);
			},
			[](NS::VarData &r_val, DataBuffer &p_buffer) {
				p_buffer.read(r_val.type);
				p_buffer.read_bits(reinterpret_cast<uint8_t *>(&r_val.data), sizeof(r_val.data) * 8);
			},
			[](const NS::VarData &p_A, const NS::VarData &p_B) -> bool {
				return p_A.data.i32 == p_B.data.i32;
			},
			[](const NS::VarData &p_var_data) -> std::string {
				return std::string("[No stringify supported by the NS test]");
			});
}

void LocalSceneSynchronizer::on_scene_entry() {
	get_network_interface().init(get_scene()->get_network(), name, authoritative_peer_id);
	setup(*this);
	register_app_object(to_handle(this));
}

void LocalSceneSynchronizer::setup_synchronizer(class LocalSceneSynchronizer &p_scene_sync, ObjectLocalId p_id) {
}

void LocalSceneSynchronizer::on_scene_exit() {
	on_app_object_removed(to_handle(this));
}

ObjectHandle LocalSceneSynchronizer::fetch_app_object(const std::string &p_object_name) {
	LocalSceneObject *lso = get_scene()->fetch_object<LocalSceneObject>(p_object_name.c_str());
	if (lso) {
		return to_handle(lso);
	}
	return ObjectHandle::NONE;
}

uint64_t LocalSceneSynchronizer::get_object_id(ObjectHandle p_app_object_handle) const {
	// No Object ID.
	return 0;
}

std::string LocalSceneSynchronizer::get_object_name(ObjectHandle p_app_object_handle) const {
	return from_handle(p_app_object_handle)->name;
}

void LocalSceneSynchronizer::setup_synchronizer_for(ObjectHandle p_app_object_handle, ObjectLocalId p_id) {
	from_handle(p_app_object_handle)->setup_synchronizer(*get_scene()->scene_sync, p_id);
}

void LocalSceneSynchronizer::set_variable(ObjectHandle p_app_object_handle, const char *p_var_name, const VarData &p_val) {
	LocalSceneObject *lso = from_handle(p_app_object_handle);
	auto element = lso->variables.find(std::string(p_var_name));
	if (element != lso->variables.end()) {
		element->second.copy(p_val);
	}
}

bool LocalSceneSynchronizer::get_variable(const ObjectHandle p_app_object_handle, const char *p_var_name, VarData &r_val) const {
	const LocalSceneObject *lso = from_handle(p_app_object_handle);
	auto element = lso->variables.find(std::string(p_var_name));
	if (element != lso->variables.end()) {
		r_val.copy(element->second);
		return true;
	} else {
		// For convenience, this never fails.
		r_val = VarData();
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
