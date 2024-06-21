#pragma once

#include "core.h"
#include "data_buffer.h"
#include "processor.h"
#include "var_data.h"
#include <functional>
#include <string>
#include <vector>

NS_NAMESPACE_BEGIN
struct NameAndVar {
	std::string name;
	VarData value;

	NameAndVar() = default;

	NameAndVar(const NameAndVar &) = delete;
	NameAndVar &operator=(const NameAndVar &) = delete;

	NameAndVar(NameAndVar &&p_other);
	NameAndVar &operator=(NameAndVar &&p_other);

	void copy(const NameAndVar &p_nav);
	static NameAndVar make_copy(const NameAndVar &p_other);
};

struct VarDescriptor {
	VarId id = VarId::NONE;
	NameAndVar var;
	/// The variable type.
	std::uint8_t type = 0;
	VarDataSetFunc set_func = nullptr;
	VarDataGetFunc get_func = nullptr;
	bool skip_rewinding = false;
	bool enabled = false;
	std::vector<struct ChangesListener *> changes_listeners;

	VarDescriptor() = default;
	VarDescriptor(
			VarId p_id,
			const std::string &p_name,
			std::uint8_t p_type,
			VarData &&p_val,
			VarDataSetFunc p_set_func,
			VarDataGetFunc p_get_func,
			bool p_skip_rewinding,
			bool p_enabled);

	bool operator<(const VarDescriptor &p_other) const;
};

struct ObjectData {
	friend class ObjectDataStorage;

private:
	class ObjectDataStorage &storage;

	ObjectData(class ObjectDataStorage &p_storage);

private:
	/// ID used to reference this ObjectData in the networked calls.
	/// This id is set by the server and the client may not have it yet.
	ObjectNetId net_id = ObjectNetId::NONE;

	/// ID used to reference this ObjectData locally. This id is always set.
	ObjectLocalId local_id = ObjectLocalId::NONE;

	// TODO consider uint8 or uint16
	int controlled_by_peer = -1;

public:
	struct {
		std::function<void(float /*delta*/, DataBuffer & /*r_data_buffer*/)> collect_input;
		std::function<int(DataBuffer & /*p_data_buffer*/)> count_input_size;
		std::function<bool(DataBuffer & /*p_data_buffer_A*/, DataBuffer & /*p_data_buffer_B*/)> are_inputs_different;
		std::function<void(float /*delta*/, DataBuffer & /*p_data_buffer*/)> process;
	} controller_funcs;

public:
#ifdef NS_DEBUG_ENABLED
	uint64_t debug_object_id = 0;
#endif
	std::string object_name;
	// The local application object handle associated to this `NodeData`.
	ObjectHandle app_object_handle = ObjectHandle::NONE;

	bool realtime_sync_enabled_on_client = false;

	/// The sync variables of this node. The order of this vector matters
	/// because the index is the `VarId`.
	std::vector<VarDescriptor> vars;
	NS::Processor<float> functions[PROCESS_PHASE_COUNT];

	std::function<void(DataBuffer & /*out_buffer*/, float /*update_rate*/)> func_trickled_collect;
	std::function<void(float /*delta*/, float /*interpolation_alpha*/, DataBuffer & /*past_buffer*/, DataBuffer & /*future_buffer*/)> func_trickled_apply;

public:
	void set_net_id(ObjectNetId p_id);
	ObjectNetId get_net_id() const;

	ObjectLocalId get_local_id() const;

	bool has_registered_process_functions() const;
	bool can_trickled_sync() const;

	void setup_controller(
			std::function<void(float /*delta*/, DataBuffer & /*r_data_buffer*/)> p_collect_input_func = nullptr,
			std::function<int(DataBuffer & /*p_data_buffer*/)> p_count_input_size_func = nullptr,
			std::function<bool(DataBuffer & /*p_data_buffer_A*/, DataBuffer & /*p_data_buffer_B*/)> p_are_inputs_different_func = nullptr,
			std::function<void(float /*delta*/, DataBuffer & /*p_data_buffer*/)> p_process_func = nullptr);
	bool set_controlled_by_peer(int p_peer);
	int get_controlled_by_peer() const;

	VarId find_variable_id(const std::string &p_var_name) const;
};

NS_NAMESPACE_END