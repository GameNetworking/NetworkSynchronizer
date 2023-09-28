#pragma once

#include "core.h"
#include "modules/network_synchronizer/core/object_data.h"
#include "modules/network_synchronizer/net_utilities.h"
#include <vector>

NS_NAMESPACE_BEGIN

class ObjectDataStorage {
	class SceneSynchronizerBase &sync;

	std::vector<ObjectLocalId> free_local_indices;

	// All allocated object data.
	std::vector<ObjectData *> objects_data;

	// All registered objects, that have the NetId assigned, organized per NetId.
	std::vector<ObjectData *> objects_data_organized_by_netid;

	// All the controller nodes.
	std::vector<ObjectData *> objects_data_controllers;

public:
	ObjectDataStorage(class SceneSynchronizerBase &p_sync);
	~ObjectDataStorage();

	ObjectData *allocate_object_data();
	void deallocate_object_data(ObjectData &p_object_data);

	void object_set_net_id(ObjectData &p_object_data, ObjectNetId p_new_id);

	ObjectData *find_object_data(ObjectHandle p_handle);
	const ObjectData *find_object_data(ObjectHandle p_handle) const;

	ObjectData *find_object_data(class NetworkedControllerBase &p_controller);
	const ObjectData *find_object_data(const class NetworkedControllerBase &p_controller) const;

	ObjectData *get_object_data(ObjectNetId p_net_id, bool p_expected = true);
	const ObjectData *get_object_data(ObjectNetId p_net_id, bool p_expected = true) const;

	ObjectData *get_object_data(ObjectLocalId p_handle, bool p_expected = true);
	const ObjectData *get_object_data(ObjectLocalId p_handle, bool p_expected = true) const;

	void reserve_net_ids(int p_count);

	const std::vector<ObjectData *> &get_objects_data() const;
	const std::vector<ObjectData *> &get_controllers_objects_data() const;
	const std::vector<ObjectData *> &get_sorted_objects_data() const;

	ObjectNetId generate_net_id() const;
	bool is_empty() const;

	void notify_set_controller(ObjectData &p_object);
};

NS_NAMESPACE_END
