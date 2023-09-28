#pragma once

#include "core.h"
#include "core/templates/local_vector.h"
#include "core/variant/variant.h"
#include "processor.h"
#include <string>
#include <vector>

NS_NAMESPACE_BEGIN

struct Var {
	StringName name;
	Variant value;
};

struct VarData {
	NetVarId id = UINT32_MAX;
	Var var;
	bool skip_rewinding = false;
	bool enabled = false;
	std::vector<struct ChangesListener *> changes_listeners;

	VarData() = default;
	VarData(const StringName &p_name);
	VarData(NetVarId p_id, const StringName &p_name, const Variant &p_val, bool p_skip_rewinding, bool p_enabled);

	bool operator==(const VarData &p_other) const;
	bool operator<(const VarData &p_other) const;
};

struct ObjectData {
	friend class ObjectDataStorage;

private:
	class ObjectDataStorage &storage;

	ObjectData(class ObjectDataStorage &p_storage);

private:
	// ID used to reference this NodeData in the networked calls.
	ObjectNetId net_id = UINT32_MAX;
	ObjectLocalId local_id = ObjectLocalId::ID_NONE;

	/// Associated controller.
	class NetworkedControllerBase *controller = nullptr;

public:
	uint64_t instance_id = 0; // TODO remove this?
	std::string object_name;
	// The local application object handle associated to this `NodeData`.
	ObjectHandle app_object_handle = ObjectHandle::NONE;

	bool realtime_sync_enabled_on_client = false;

	/// The sync variables of this node. The order of this vector matters
	/// because the index is the `NetVarId`.
	LocalVector<VarData> vars;
	NS::Processor<float> functions[PROCESSPHASE_COUNT];

	// func _collect_epoch_data(buffer: DataBuffer):
	Callable collect_epoch_func;

	// func _apply_epoch(delta: float, interpolation_alpha: float, past_buffer: DataBuffer, future_buffer: DataBuffer):
	Callable apply_epoch_func;

public:
	void set_net_id(ObjectNetId p_id);
	ObjectNetId get_net_id() const;

	ObjectLocalId get_local_id(ObjectLocalId p_id);

	bool has_registered_process_functions() const;
	bool can_deferred_sync() const;

	void set_controller(class NetworkedControllerBase *p_controller);
	class NetworkedControllerBase *get_controller() const;
};

NS_NAMESPACE_END
