#include "object_data_storage.h"

#include "../scene_synchronizer.h"
#include "ensure.h"
#include "scene_synchronizer_debugger.h"

NS_NAMESPACE_BEGIN
ObjectDataStorage::ObjectDataStorage(SceneSynchronizerBase &p_sync) :
	sync(p_sync) {
}

ObjectDataStorage::~ObjectDataStorage() {
	for (auto od : objects_data) {
		if (od) {
			delete od;
		}
	}

	free_local_indices.clear();
	objects_data.clear();
	objects_data_organized_by_netid.clear();
	objects_data_controlled_by_peers.clear();
}

ObjectData *ObjectDataStorage::allocate_object_data() {
	ObjectData *od = new ObjectData(*this);

	if (free_local_indices.empty()) {
		od->local_id.id = ObjectLocalId::IdType(objects_data.size());
		objects_data.push_back(od);
	} else {
		od->local_id.id = free_local_indices.back().id;
		free_local_indices.pop_back();
		NS_ASSERT_COND(objects_data.size() > od->local_id.id);
		NS_ASSERT_COND(objects_data[od->local_id.id] == nullptr);
		objects_data[od->local_id.id] = od;
	}

	NS_ASSERT_COND(objects_data[od->local_id.id] == od);

	return od;
}

void ObjectDataStorage::deallocate_object_data(ObjectData &p_object_data) {
	const ObjectLocalId local_id = p_object_data.local_id;
	const ObjectNetId net_id = p_object_data.net_id;

	// The allocate function guarantee the validity of this check.
	NS_ASSERT_COND(objects_data[local_id.id] == (&p_object_data));
	objects_data[local_id.id] = nullptr;

	if (objects_data_organized_by_netid.size() > net_id.id) {
		NS_ASSERT_COND(objects_data_organized_by_netid[net_id.id] == (&p_object_data));
		objects_data_organized_by_netid[net_id.id] = nullptr;
	}

	// Clear the peers array.
	const int cbp = p_object_data.controlled_by_peer;
	p_object_data.controlled_by_peer = -1;
	notify_set_controlled_by_peer(cbp, p_object_data);

	// Remove from unnamed_objects_data if set
	VecFunc::remove_unordered(unnamed_objects_data, &p_object_data);

	delete (&p_object_data);

	free_local_indices.push_back(local_id);
}

void ObjectDataStorage::object_set_net_id(ObjectData &p_object_data, ObjectNetId p_new_id) {
	if (p_object_data.net_id == p_new_id) {
		return;
	}

	if (objects_data_organized_by_netid.size() > p_object_data.net_id.id) {
		objects_data_organized_by_netid[p_object_data.net_id.id] = nullptr;
	}

	p_object_data.net_id = ObjectNetId::NONE;

	if (p_new_id == ObjectNetId::NONE) {
		sync.notify_object_data_net_id_changed(p_object_data);
		return;
	}

	if (objects_data_organized_by_netid.size() > p_new_id.id) {
		if (objects_data_organized_by_netid[p_new_id.id] && objects_data_organized_by_netid[p_new_id.id] != (&p_object_data)) {
			SceneSynchronizerDebugger::singleton()->print(ERROR, "[NET] The object `" + p_object_data.object_name + "` was associated with to a new NetId that was used by `" + objects_data_organized_by_netid[p_new_id.id]->object_name + "`. THIS IS NOT SUPPOSED TO HAPPEN.");
		}
	} else {
		// Expand the array
		const std::size_t new_size = p_new_id.id + 1;
		const std::size_t old_size = objects_data_organized_by_netid.size();
		objects_data_organized_by_netid.resize(new_size);
		// Set the new pointers to nullptr.
		memset(objects_data_organized_by_netid.data() + old_size, 0, sizeof(void *) * (new_size - old_size));
	}

	objects_data_organized_by_netid[p_new_id.id] = &p_object_data;
	p_object_data.net_id = p_new_id;
	sync.notify_object_data_net_id_changed(p_object_data);
}

ObjectLocalId ObjectDataStorage::find_object_local_id(ObjectHandle p_handle) const {
	for (auto od : objects_data) {
		if (od && od->app_object_handle == p_handle) {
			return od->local_id;
		}
	}
	return ObjectLocalId::NONE;
}

NS::ObjectData *ObjectDataStorage::get_object_data(ObjectNetId p_net_id, bool p_expected) {
	if (p_expected) {
		NS_ENSURE_V_MSG(p_net_id.id < objects_data_organized_by_netid.size(), nullptr, "The ObjectData with NetID `" + std::to_string(p_net_id.id) + "` was not found.");
	} else {
		if (objects_data_organized_by_netid.size() <= p_net_id.id) {
			return nullptr;
		}
	}

	return objects_data_organized_by_netid[p_net_id.id];
}

const NS::ObjectData *ObjectDataStorage::get_object_data(ObjectNetId p_net_id, bool p_expected) const {
	if (p_expected) {
		NS_ENSURE_V_MSG(p_net_id.id < objects_data_organized_by_netid.size(), nullptr, "The ObjectData with NetID `" + std::to_string(p_net_id.id) + "` was not found.");
	} else {
		if (objects_data_organized_by_netid.size() <= p_net_id.id) {
			return nullptr;
		}
	}

	return objects_data_organized_by_netid[p_net_id.id];
}

NS::ObjectData *ObjectDataStorage::get_object_data(ObjectLocalId p_handle, bool p_expected) {
	if (p_expected) {
		NS_ENSURE_V_MSG(p_handle.id < objects_data.size(), nullptr, "The ObjectData with LocalID `" + std::to_string(p_handle.id) + "` was not found.");
	} else {
		if (p_handle.id < 0 || objects_data.size() <= p_handle.id) {
			return nullptr;
		}
	}

	return objects_data[p_handle.id];
}

const NS::ObjectData *ObjectDataStorage::get_object_data(ObjectLocalId p_handle, bool p_expected) const {
	if (p_expected) {
		NS_ENSURE_V_MSG(p_handle.id < objects_data.size(), nullptr, "The ObjectData with LocalID `" + std::to_string(p_handle.id) + "` was not found.");
	} else {
		if (p_handle.id < 0 || objects_data.size() <= p_handle.id) {
			return nullptr;
		}
	}

	return objects_data[p_handle.id];
}

void ObjectDataStorage::reserve_net_ids(int p_count) {
	objects_data_organized_by_netid.reserve(p_count);
}

const std::vector<ObjectData *> &ObjectDataStorage::get_objects_data() const {
	return objects_data;
}

const std::vector<ObjectData *> &ObjectDataStorage::get_sorted_objects_data() const {
	return objects_data_organized_by_netid;
}

const std::map<int, std::vector<ObjectData *>> &ObjectDataStorage::get_peers_controlled_objects_data() const {
	return objects_data_controlled_by_peers;
}

const std::vector<ObjectData *> *ObjectDataStorage::get_peer_controlled_objects_data(int p_peer) const {
	return NS::MapFunc::get_or_null(objects_data_controlled_by_peers, p_peer);
}

const std::vector<ObjectData *> &ObjectDataStorage::get_unnamed_objects_data() const {
	return unnamed_objects_data;
}

ObjectNetId ObjectDataStorage::generate_net_id() const {
	ObjectNetId::IdType i = 0;
	for (auto od : objects_data_organized_by_netid) {
		if (!od) {
			// This position is empty, can be used as NetId.
			return ObjectNetId{ { i } };
		}
		i++;
	}

	// Create a new NetId.
	return ObjectNetId{ { ObjectNetId::IdType(objects_data_organized_by_netid.size()) } };
}

bool ObjectDataStorage::is_empty() const {
	for (auto od : objects_data) {
		if (od) {
			return false;
		}
	}

	return true;
}

void ObjectDataStorage::notify_set_controlled_by_peer(int p_old_peer, ObjectData &p_object) {
	if (p_old_peer != -1) {
		std::vector<ObjectData *> *objects = NS::MapFunc::get_or_null(objects_data_controlled_by_peers, p_old_peer);
		if (objects) {
			NS::VecFunc::remove_unordered(*objects, &p_object);
		}
	}

	if (p_object.get_controlled_by_peer() != -1) {
		std::map<int, std::vector<ObjectData *>>::iterator objects_it = NS::MapFunc::insert_if_new(
				objects_data_controlled_by_peers,
				p_object.get_controlled_by_peer(),
				std::vector<ObjectData *>());
		NS::VecFunc::insert_unique(objects_it->second, &p_object);
	}
}

void ObjectDataStorage::notify_object_name_unnamed_changed(ObjectData &p_object) {
	if (p_object.object_name.empty()) {
		VecFunc::insert_unique(unnamed_objects_data, &p_object);
	} else {
		VecFunc::remove_unordered(unnamed_objects_data, &p_object);
	}
}

NS_NAMESPACE_END