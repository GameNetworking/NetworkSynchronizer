#pragma once

#include "local_network.h"
#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/core/network_interface.h"
#include "modules/network_synchronizer/networked_controller.h"
#include "modules/network_synchronizer/scene_synchronizer.h"
#include <memory>
#include <string>

NS_NAMESPACE_BEGIN

class LocalSceneObject {
protected:
	friend class LocalScene;
	class LocalScene *scene_owner = nullptr;
	int authoritative_peer_id = 0;

public:
	std::string name;
	std::map<std::string, Variant> variables;

	class LocalScene *get_scene() const;

	virtual void on_scene_entry() {}
	virtual void setup_synchronizer(class LocalSceneSynchronizer &p_scene_sync) {}
	virtual void on_scene_exit() {}
};

class LocalSceneSynchronizer : public SceneSynchronizer<LocalSceneObject, LocalNetworkInterface>, public SynchronizerManager, public LocalSceneObject {
public:
	LocalSceneSynchronizer();

	virtual void on_scene_entry() override;
	virtual void setup_synchronizer(class LocalSceneSynchronizer &p_scene_sync) override;
	virtual void on_scene_exit() override;

	/// NOTE: THIS FUNCTION MUST RETURN THE POINTER THAT POINTS TO `BaseType` SPECIFIED IN `SceneSynchronizer<BaseType>`.
	/// 		If you have a pointer pointing to a parent class, cast it using
	///			`static_cast` first, or you will cause a segmentation fault.
	virtual ObjectHandle fetch_app_object(const std::string &p_object_name) override;
	virtual uint64_t get_object_id(ObjectHandle p_app_object_handle) const override;
	virtual std::string get_object_name(ObjectHandle p_app_object_handle) const override;
	virtual void setup_synchronizer_for(ObjectHandle p_app_object_handle) override;
	virtual void set_variable(ObjectHandle p_app_object_handle, const char *p_var_name, const Variant &p_val) override;
	virtual bool get_variable(ObjectHandle p_app_object_handle, const char *p_var_name, Variant &p_val) const override;
	virtual NS::NetworkedControllerBase *extract_network_controller(ObjectHandle p_app_object_handle) override;
	virtual const NS::NetworkedControllerBase *extract_network_controller(ObjectHandle p_app_object_handle) const override;
};

class LocalScene {
	LocalNetwork network;
	std::map<std::string, std::shared_ptr<LocalSceneObject>> objects;

public:
	LocalSceneSynchronizer *scene_sync = nullptr;

public:
	LocalNetwork &get_network() { return network; }
	const LocalNetwork &get_network() const { return network; }

	// Start the scene as server.
	void start_as_server();

	// Start the scene as client connected to the server.
	void start_as_client(LocalScene &p_server);

	int get_peer() const;

	template <class T>
	T *add_object(const std::string &p_object_name, int p_authoritative_peer);

	template <class T>
	T *fetch_object(const char *p_object_name);

	void remove_object(const char *p_object_name);

	void process(float p_delta);
};

template <class T>
T *LocalScene::add_object(const std::string &p_object_name, int p_authoritative_peer) {
	const std::string name = p_object_name;
	CRASH_COND(objects.find(name) != objects.end());
	std::shared_ptr<T> object = std::make_shared<T>();
	objects[name] = object;
	object->scene_owner = this;
	object->name = name;
	object->authoritative_peer_id = p_authoritative_peer;
	object->on_scene_entry();
	return object.get();
}

template <class T>
T *LocalScene::fetch_object(const char *p_object_name) {
	std::map<std::string, std::shared_ptr<LocalSceneObject>>::iterator obj_it = objects.find(p_object_name);
	if (obj_it != objects.end()) {
		return dynamic_cast<T *>(obj_it->second.get());
	}
	return nullptr;
}

NS_NAMESPACE_END
