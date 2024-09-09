#pragma once

#include "../core/core.h"
#include "../core/network_interface.h"
#include "../core/peer_networked_controller.h"
#include "../scene_synchronizer.h"
#include "local_network.h"
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

	virtual ~LocalSceneObject();

	class LocalScene *get_scene() const;

	virtual void on_scene_entry() {
	}

	virtual void setup_synchronizer(class LocalSceneSynchronizer &p_scene_sync, ObjectLocalId p_id) {
	}

	virtual void on_scene_exit() {
	}

	NS::ObjectLocalId find_local_id() const;
};

class LocalSceneSynchronizer : public SceneSynchronizer<LocalSceneObject, LocalNetworkInterface>, public SynchronizerManager, public LocalSceneObject {
public:
	LocalSceneSynchronizer(bool p_disable_sub_ticking = false);
	virtual ~LocalSceneSynchronizer();

	static void install_local_scene_sync();
	static void uninstall_local_scene_sync();

	virtual void on_scene_entry() override;
	virtual void setup_synchronizer(class LocalSceneSynchronizer &p_scene_sync, ObjectLocalId p_id) override;
	virtual void on_scene_exit() override;

	/// NOTE: THIS FUNCTION MUST RETURN THE POINTER THAT POINTS TO `BaseType` SPECIFIED IN `SceneSynchronizer<BaseType>`.
	/// 		If you have a pointer pointing to a parent class, cast it using
	///			`static_cast` first, or you will cause a segmentation fault.
	virtual ObjectHandle fetch_app_object(const std::string &p_object_name) override;
	virtual uint64_t debug_only_get_object_id(ObjectHandle p_app_object_handle) const override;
	virtual std::string fetch_object_name(ObjectHandle p_app_object_handle) const override;
	virtual void setup_synchronizer_for(ObjectHandle p_app_object_handle, ObjectLocalId p_id) override;

	std::function<bool(const SyncGroup *p_group,
			bool p_is_partial_update,
			const std::vector<std::size_t> &p_partial_update_simulated_objects_info_indices,
			VarData &r_custom_data)> snapshot_get_custom_data_func;

	std::function<bool(const std::vector<ObjectNetId> &p_partial_update_objects,
			VarData &r_custom_data,
			const VarData &p_custom_data_from_server_snapshot)> snapshot_merge_custom_data_for_partial_update_func;

	std::function<std::uint8_t()> snapshot_get_custom_data_type_func;

	std::function<void(const VarData &r_custom_data)> snapshot_set_custom_data_func;

	virtual bool snapshot_get_custom_data(
			const SyncGroup *p_group,
			bool p_is_partial_update,
			const std::vector<std::size_t> &p_partial_update_simulated_objects_info_indices,
			VarData &r_custom_data) override {
		if (snapshot_get_custom_data_func) {
			return snapshot_get_custom_data_func(p_group, p_is_partial_update, p_partial_update_simulated_objects_info_indices, r_custom_data);
		} else {
			return SynchronizerManager::snapshot_get_custom_data(p_group, p_is_partial_update, p_partial_update_simulated_objects_info_indices, r_custom_data);
		}
	}

	virtual bool snapshot_merge_custom_data_for_partial_update(
			const std::vector<ObjectNetId> &p_partial_update_objects,
			VarData &r_custom_data,
			const VarData &p_custom_data_from_server_snapshot) override {
		if (snapshot_merge_custom_data_for_partial_update_func) {
			return snapshot_merge_custom_data_for_partial_update_func(p_partial_update_objects, r_custom_data, p_custom_data_from_server_snapshot);
		} else {
			return SynchronizerManager::snapshot_merge_custom_data_for_partial_update(p_partial_update_objects, r_custom_data, p_custom_data_from_server_snapshot);
		}
	}

	virtual std::uint8_t snapshot_get_custom_data_type() const override {
		if (snapshot_get_custom_data_type_func) {
			return snapshot_get_custom_data_type_func();
		} else {
			return SynchronizerManager::snapshot_get_custom_data_type();
		}
	}

	virtual void snapshot_set_custom_data(const VarData &r_custom_data) override {
		if (snapshot_set_custom_data_func) {
			snapshot_set_custom_data_func(r_custom_data);
		}
	}

	void clear_scene();
};

class LocalSceneSynchronizerNoSubTicks : public LocalSceneSynchronizer {
public:
	LocalSceneSynchronizerNoSubTicks() :
		LocalSceneSynchronizer(true) {
	}
};

class LocalScene {
	LocalNetwork network;
	std::vector<std::shared_ptr<LocalSceneObject>> objects;

public:
	LocalSceneSynchronizer *scene_sync = nullptr;

public:
	LocalNetwork &get_network() {
		return network;
	}

	const LocalNetwork &get_network() const {
		return network;
	}

	// Start the scene as no network.
	void start_as_no_net();

	// Start the scene as server.
	void start_as_server();

	// Start the scene as client connected to the server.
	void start_as_client(LocalScene &p_server);

	int get_peer() const;

	template <class T>
	T *add_existing_object(const std::shared_ptr<T> &p_object, const std::string &p_object_name, int p_authoritative_peer);

	template <class T>
	T *add_object(const std::string &p_object_name, int p_authoritative_peer);

	template <class T>
	T *fetch_object(const char *p_object_name);

	int fetch_object_index(const char *p_object_name) const;

	void remove_object(const char *p_object_name);

	void process(float p_delta);
};

template <class T>
T *LocalScene::add_object(const std::string &p_object_name, int p_authoritative_peer) {
	// Make sure the name are unique.
	NS_ASSERT_COND(!fetch_object<T>(p_object_name.c_str()));

	std::shared_ptr<T> object = std::make_shared<T>();
	return add_existing_object(object, p_object_name, p_authoritative_peer);
}

template <class T>
T *LocalScene::add_existing_object(const std::shared_ptr<T> &p_object, const std::string &p_object_name, int p_authoritative_peer) {
	// Make sure the name are unique.
	NS_ASSERT_COND(!fetch_object<T>(p_object_name.c_str()));

	objects.push_back(p_object);
	p_object->scene_owner = this;
	p_object->name = p_object_name;
	p_object->authoritative_peer_id = p_authoritative_peer;
	p_object->on_scene_entry();
	return p_object.get();
}

template <class T>
T *LocalScene::fetch_object(const char *p_object_name) {
	for (auto o : objects) {
		if (o->name == p_object_name) {
			return dynamic_cast<T *>(o.get());
		}
	}
	return nullptr;
}


NS_NAMESPACE_END