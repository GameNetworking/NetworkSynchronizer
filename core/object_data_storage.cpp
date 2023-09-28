#include "object_data_storage.h"
#include "modules/network_synchronizer/net_utilities.h"

NS_NAMESPACE_BEGIN

NetUtility::ObjectData *ObjectDataStorage::allocate_object_data() {
	NetUtility::ObjectData *od = memnew(NetUtility::ObjectData);

}

NetUtility::ObjectData *ObjectDataStorage::find_object_data(ObjectHandle p_handle) {
	for (uint32_t i = 0; i < objects_data.size(); i += 1) {
		if (objects_data[i] == nullptr) {
			continue;
		}
		if (objects_data[i]->app_object_handle == p_handle) {
			return objects_data[i];
		}
	}
	return nullptr;
}

const NetUtility::ObjectData *ObjectDataStorage::find_object_data(ObjectHandle p_handle) const {
	for (uint32_t i = 0; i < objects_data.size(); i += 1) {
		if (objects_data[i] == nullptr) {
			continue;
		}
		if (objects_data[i]->app_object_handle == p_handle) {
			return objects_data[i];
		}
	}
	return nullptr;
}

NetUtility::ObjectData *ObjectDataStorage::get_object_data(ObjectNetId p_net_id, bool p_expected) {
	if (p_expected) {
		ERR_FAIL_UNSIGNED_INDEX_V(p_net_id, object_data_organized_by_netid.size(), nullptr);
	} else {
		if (object_data_organized_by_netid.size() <= p_net_id) {
			return nullptr;
		}
	}

	return object_data_organized_by_netid[p_net_id];
}

const NetUtility::ObjectData *ObjectDataStorage::find_object_data(ObjectNetId p_net_id, bool p_expected) const {
	if (p_expected) {
		ERR_FAIL_UNSIGNED_INDEX_V(p_net_id, object_data_organized_by_netid.size(), nullptr);
	} else {
		if (object_data_organized_by_netid.size() <= p_net_id) {
			return nullptr;
		}
	}

	return object_data_organized_by_netid[p_net_id];
}

NetUtility::ObjectData *ObjectDataStorage::get_object_data(ObjectLocalId p_handle, bool p_expected) {
	if (p_expected) {
		ERR_FAIL_UNSIGNED_INDEX_V(p_handle.id, object_data_organized_by_netid.size(), nullptr);
	} else {
		if (object_data_organized_by_netid.size() <= p_handle.id) {
			return nullptr;
		}
	}

	return object_data_organized_by_odid[p_handle.id];
}

const NetUtility::ObjectData *ObjectDataStorage::find_object_data(ObjectLocalId p_handle, bool p_expected) const {
	if (p_expected) {
		ERR_FAIL_UNSIGNED_INDEX_V(p_handle.id, object_data_organized_by_netid.size(), nullptr);
	} else {
		if (object_data_organized_by_netid.size() <= p_handle.id) {
			return nullptr;
		}
	}

	return object_data_organized_by_odid[p_handle.id];
}

NS_NAMESPACE_END
