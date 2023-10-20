#pragma once

#include "../data_buffer.h"
#include "core.h"
#include "core/templates/local_vector.h"
#include "core/variant/callable.h"
#include "processor.h"
#include "var_data.h"
#include <functional>
#include <string>
#include <vector>

NS_NAMESPACE_BEGIN

struct NameAndVar {
	std::string name;
	VarData value;

	void copy(const NameAndVar &p_nav);
};

struct VarDescriptor {
	VarId id = VarId::NONE;
	NameAndVar var;
	bool skip_rewinding = false;
	bool enabled = false;
	std::vector<struct ChangesListener *> changes_listeners;

	VarDescriptor() = default;
	VarDescriptor(VarId p_id, const std::string &p_name, VarData &&p_val, bool p_skip_rewinding, bool p_enabled);

	bool operator<(const VarDescriptor &p_other) const;
};

struct ObjectData {
	friend class ObjectDataStorage;

private:
	class ObjectDataStorage &storage;

	ObjectData(class ObjectDataStorage &p_storage);

private:
	// ID used to reference this NodeData in the networked calls.
	ObjectNetId net_id = ObjectNetId::NONE;
	ObjectLocalId local_id = ObjectLocalId::NONE;

	/// Associated controller.
	class NetworkedControllerBase *controller = nullptr;

public:
	uint64_t instance_id = 0; // TODO remove this?
	std::string object_name;
	// The local application object handle associated to this `NodeData`.
	ObjectHandle app_object_handle = ObjectHandle::NONE;

	bool realtime_sync_enabled_on_client = false;

	/// The sync variables of this node. The order of this vector matters
	/// because the index is the `VarId`.
	std::vector<VarDescriptor> vars;
	NS::Processor<float> functions[PROCESSPHASE_COUNT];

	std::function<void(DataBuffer & /*out_buffer*/)> func_trickled_collect;
	std::function<void(float /*delta*/, float /*interpolation_alpha*/, DataBuffer & /*past_buffer*/, DataBuffer & /*future_buffer*/)> func_trickled_apply;

public:
	void set_net_id(ObjectNetId p_id);
	ObjectNetId get_net_id() const;

	ObjectLocalId get_local_id() const;

	bool has_registered_process_functions() const;
	bool can_trickled_sync() const;

	void set_controller(class NetworkedControllerBase *p_controller);
	class NetworkedControllerBase *get_controller() const;

	VarId find_variable_id(const std::string &p_var_name) const;
};

NS_NAMESPACE_END
