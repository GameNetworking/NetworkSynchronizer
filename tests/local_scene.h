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
public:
	std::map<String, Variant> variables;
};

class LocalSceneSynchronizer : public SceneSynchronizer, public SynchronizerManager, public LocalSceneObject {
public:
	virtual Node *get_node_or_null(const NodePath &p_path) override {
	}

	virtual NS::NetworkedController *extract_network_controller(Node *p_node) const override {
	}

	virtual const NS::NetworkedController *extract_network_controller(const Node *p_node) const override {
	}
};

class LocalNetworkedController : public NetworkedController, public NetworkedControllerManager, public LocalSceneObject {
public:
	LocalNetworkedController() {}

	virtual void collect_inputs(double p_delta, DataBuffer &r_buffer) override {}
	virtual void controller_process(double p_delta, DataBuffer &p_buffer) override {}
	virtual bool are_inputs_different(DataBuffer &p_buffer_A, DataBuffer &p_buffer_B) override {}
	virtual uint32_t count_input_size(DataBuffer &p_buffer) override { return 0; }
};

class LocalScene {
	LocalNetwork network;
	std::map<std::string, std::shared_ptr<LocalSceneObject>> objects;

public:
	// Start the scene as server.
	void start_as_server();

	// Start the scene as client connected to the server.
	void start_as_client(LocalScene &p_server);

	int get_peer() const;

	template <class T>
	std::shared_ptr<T> add_object(const char *p_object_name, int p_authoritative_peer);

	void process(float p_delta);
};

template <class T>
std::shared_ptr<T> LocalScene::add_object(const char *p_object_name, int p_authoritative_peer) {
	const std::string name = p_object_name;
	CRASH_COND(objects.find(name) != objects.end());
	std::shared_ptr<T> object = std::make_shared<T>();
	objects[name] = object;
	return object;
}

NS_NAMESPACE_END
