#pragma once

#include "core.h"
#include "scheduled_procedure.h"

#include <map>
#include <vector>

NS_NAMESPACE_BEGIN
class ObjectDataStorage {
	class SceneSynchronizerBase &sync;

	std::vector<ObjectLocalId> free_local_indices;

	// All allocated object data.
	std::vector<struct ObjectData *> objects_data;

	// All registered objects, that have the NetId assigned, organized per NetId.
	std::vector<ObjectData *> objects_data_organized_by_netid;

	std::map<int, std::vector<ObjectData *>> objects_data_controlled_by_peers;

	std::vector<ObjectData *> unnamed_objects_data;

	std::vector<ScheduledProcedureHandle> sorted_active_scheduled_procedures;

public:
	ObjectDataStorage(class SceneSynchronizerBase &p_sync);
	~ObjectDataStorage();

	class SceneSynchronizerDebugger &get_debugger() const;

	ObjectData *allocate_object_data();
	void deallocate_object_data(ObjectData &p_object_data);

	void object_set_net_id(ObjectData &p_object_data, ObjectNetId p_new_id);

	ObjectLocalId find_object_local_id(ObjectHandle p_handle) const;

	ObjectData *get_object_data(ObjectNetId p_net_id, bool p_expected = true);
	const ObjectData *get_object_data(ObjectNetId p_net_id, bool p_expected = true) const;

	ObjectData *get_object_data(ObjectLocalId p_handle, bool p_expected = true);
	const ObjectData *get_object_data(ObjectLocalId p_handle, bool p_expected = true) const;

	void reserve_net_ids(int p_count);

	const std::vector<ObjectData *> &get_objects_data() const;
	const std::vector<ObjectData *> &get_sorted_objects_data() const;
	const std::map<int, std::vector<ObjectData *>> &get_peers_controlled_objects_data() const;
	const std::vector<ObjectData *> *get_peer_controlled_objects_data(int p_peer) const;
	const std::vector<ObjectData *> &get_unnamed_objects_data() const;

	ObjectNetId generate_net_id() const;
	bool is_empty() const;

	void notify_set_controlled_by_peer(int p_old_peer, ObjectData &p_object);

	void notify_object_name_unnamed_changed(ObjectData &p_object);

	void notify_scheduled_procedure_updated(ObjectData &p_object, const ScheduledProcedureId p_procedure_id, bool p_active);

	const std::vector<ScheduledProcedureHandle> &get_sorted_active_scheduled_procedures() const {
		return sorted_active_scheduled_procedures;
	}
};

NS_NAMESPACE_END