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
	friend class LocalScene;
	class LocalScene *scene_owner = nullptr;

public:
	std::string name;
	std::map<std::string, Variant> variables;

	class LocalScene *get_scene() const;

	virtual void on_scene_entry() {}
	virtual void setup_synchronizer(class LocalSceneSynchronizer &p_scene_sync) {}
	virtual void on_scene_exit() {}
};

class LocalSceneSynchronizer : public SceneSynchronizer<LocalSceneObject>, public SynchronizerManager, public LocalSceneObject {
public:
	LocalNetworkInterface network_interface;

	LocalSceneSynchronizer();

	virtual void on_scene_entry() override;
	virtual void setup_synchronizer(class LocalSceneSynchronizer &p_scene_sync) override;
	virtual void on_scene_exit() override;

	virtual void *fetch_app_object(const std::string &p_object_name) override;
	virtual uint64_t get_object_id(const void *p_app_object) const override;
	virtual std::string get_object_name(const void *p_app_object) const override;
	virtual void setup_synchronizer_for(void *p_object) override;
	virtual void set_variable(void *p_object, const char *p_var_name, const Variant &p_val) override;
	virtual bool get_variable(const void *p_object, const char *p_var_name, Variant &p_val) const override;
	virtual NS::NetworkedController *extract_network_controller(void *p_app_object) const override;
	virtual const NS::NetworkedController *extract_network_controller(const void *p_app_object) const override;
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
	std::shared_ptr<T> add_object(const std::string &p_object_name, int p_authoritative_peer);
	std::shared_ptr<LocalSceneObject> fetch_object(const std::string &p_object_name);

	template <class T>
	std::shared_ptr<T> fetch_object(const char *p_object_name);

	void remove_object(const char *p_object_name);

	void process(float p_delta);
};

template <class T>
std::shared_ptr<T> LocalScene::add_object(const std::string &p_object_name, int p_authoritative_peer) {
	const std::string name = p_object_name;
	CRASH_COND(objects.find(name) != objects.end());
	std::shared_ptr<T> object = std::make_shared<T>();
	objects[name] = object;
	object->scene_owner = this;
	object->name = name;
	object->on_scene_entry();
	return object;
}

template <class T>
std::shared_ptr<T> LocalScene::fetch_object(const char *p_object_name) {
	std::shared_ptr<LocalSceneObject> obj = fetch_object(p_object_name);
	return obj;
}

NS_NAMESPACE_END
