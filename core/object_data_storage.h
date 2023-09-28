#pragma once

#include "core.h"
#include "modules/network_synchronizer/net_utilities.h"
#include <vector>

//namespace NetUtility {
//struct ObjectData;
//};

NS_NAMESPACE_BEGIN

class ObjectDataStorage {
	// All allocated object data.
	std::vector<NetUtility::ObjectData *> objects_data;

	// All registered objects, that have the NetId assigned, organized per NetId.
	std::vector<NetUtility::ObjectData *> object_data_organized_by_netid;

	// The object data organized per ObjectDataId
	std::vector<NetUtility::ObjectData *> object_data_organized_by_odid;

	// All the controller nodes.
	std::vector<NetUtility::ObjectData *> node_data_controllers;

public:
	NetUtility::ObjectData *allocate_object_data();

	NetUtility::ObjectData *find_object_data(ObjectHandle p_handle);
	const NetUtility::ObjectData *find_object_data(ObjectHandle p_handle) const;

	NetUtility::ObjectData *get_object_data(ObjectNetId p_net_id, bool p_expected = true);
	const NetUtility::ObjectData *find_object_data(ObjectNetId p_net_id, bool p_expected = true) const;

	NetUtility::ObjectData *get_object_data(ObjectLocalId p_handle, bool p_expected = true);
	const NetUtility::ObjectData *find_object_data(ObjectLocalId p_handle, bool p_expected = true) const;
};

NS_NAMESPACE_END
