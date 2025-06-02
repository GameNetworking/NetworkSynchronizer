#pragma once

#include "core.h"
#include "data_buffer.h"
#include "network_interface_define.h"
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
	const VarId id;
	NameAndVar var;
	/// The variable type.
	const std::uint8_t type;
	NS_VarDataSetFunc set_func = nullptr;
	NS_VarDataGetFunc get_func = nullptr;
	bool skip_rewinding = false;
	bool enabled = false;
	std::vector<struct ChangesListener *> changes_listeners;

	VarDescriptor() = delete;
	VarDescriptor(
			VarId p_id,
			const std::string &p_name,
			std::uint8_t p_type,
			VarData &&p_val,
			NS_VarDataSetFunc p_set_func,
			NS_VarDataGetFunc p_get_func,
			bool p_skip_rewinding,
			bool p_enabled);
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

	std::string object_name;

	/// The scheme_id is used to identify the type of object when the
	/// synchronized variables change dynamically based on the represented type.
	/// This function is very useful for synchronizing characters, since
	/// the class is the same but the synchronized variables change depending on
	/// the loaded abilities.
	SchemeId scheme_id = SchemeId::DEFAULT;

public:
	struct {
		std::function<void(float /*delta*/, DataBuffer & /*r_data_buffer*/)> collect_input;
		std::function<bool(DataBuffer & /*p_data_buffer_A*/, DataBuffer & /*p_data_buffer_B*/)> are_inputs_different;
		std::function<void(float /*delta*/, DataBuffer & /*p_data_buffer*/)> process;
	} controller_funcs;

public:
#ifdef NS_DEBUG_ENABLED
	uint64_t debug_object_id = 0;
#endif
	// The local application object handle associated to this `NodeData`.
	ObjectHandle app_object_handle = ObjectHandle::NONE;


	bool realtime_sync_enabled_on_client = false;

	/// The sync variables of this node. The order of this vector matters
	/// because the index is the `VarId`.
	std::vector<VarDescriptor> vars;
	Processor<float> functions[PROCESS_PHASE_COUNT];

	struct ScheduledProcedureInfo {
		NS_ScheduledProcedureFunc func = nullptr;
		GlobalFrameIndex execute_frame = GlobalFrameIndex{ 0 };
		GlobalFrameIndex paused_frame = GlobalFrameIndex{ 0 };
		DataBuffer args;
	};

private:
	std::vector<ScheduledProcedureInfo> scheduled_procedures;

public:
	std::vector<RPCInfo> rpcs_info;

public:
	std::function<void(DataBuffer & /*out_buffer*/, float /*update_rate*/)> func_trickled_collect;
	std::function<void(float /*delta*/, float /*interpolation_alpha*/, DataBuffer & /*past_buffer*/, DataBuffer & /*future_buffer*/)> func_trickled_apply;

public:
	void flush_everything_registered();

	void set_net_id(ObjectNetId p_id);
	ObjectNetId get_net_id() const;

	ObjectLocalId get_local_id() const;

	void set_scheme_id(SchemeId p_scheme_id) {
		scheme_id = p_scheme_id;
	}

	SchemeId get_scheme_id() const {
		return scheme_id;
	}

	bool has_registered_process_functions() const;
	bool can_trickled_sync() const;

	void setup_controller(
			std::function<void(float /*delta*/, DataBuffer & /*r_data_buffer*/)> p_collect_input_func = nullptr,
			std::function<bool(DataBuffer & /*p_data_buffer_A*/, DataBuffer & /*p_data_buffer_B*/)> p_are_inputs_different_func = nullptr,
			std::function<void(float /*delta*/, DataBuffer & /*p_data_buffer*/)> p_process_func = nullptr);
	bool set_controlled_by_peer(class SceneSynchronizerBase &Synchronizer, int p_peer);
	int get_controlled_by_peer() const;

	void set_object_name(const std::string &name, bool p_force_set = false);
	const std::string &get_object_name() const;

	VarId find_variable_id(const std::string &p_var_name) const;

	/// Adds a new scheduled procedure and returns its handle.
	ScheduledProcedureId scheduled_procedure_add(NS_ScheduledProcedureFunc p_func);
	/// Returns true if the ScheduledProcedureId points to a valid procedure.
	bool scheduled_procedure_exist(ScheduledProcedureId p_id) const;
	/// Removes a procedure.
	void scheduled_procedure_remove(ScheduledProcedureId p_id);

	/// Calls the procedure and initialize the args. This is usually called on the server.
	void scheduled_procedure_fetch_args(ScheduledProcedureId p_id, const SynchronizerManager &p_sync_manager, SceneSynchronizerDebugger &p_debugger);
	void scheduled_procedure_set_args(ScheduledProcedureId p_id, const DataBuffer &p_args);
	void scheduled_procedure_reset_to(ScheduledProcedureId p_id, const struct ScheduledProcedureSnapshot &p_snapshot);

	void scheduled_procedure_execute(ScheduledProcedureId p_id, ScheduledProcedurePhase p_phase, const SynchronizerManager &p_sync_manager, SceneSynchronizerDebugger &p_debugger);

	/// Starts a procedure. Notice this function calls the procedure to initialize the args DataBuffer.
	void scheduled_procedure_start(ScheduledProcedureId p_id, GlobalFrameIndex p_executes_at_frame);
	/// Pause the procedure.
	void scheduled_procedure_pause(ScheduledProcedureId p_id, GlobalFrameIndex p_current_frame);
	void scheduled_procedure_pause(ScheduledProcedureId p_id, GlobalFrameIndex p_executes_at_frame, GlobalFrameIndex p_current_frame);
	/// Stop the procedure.
	void scheduled_procedure_stop(ScheduledProcedureId p_id);

	/// Returns true if the procedure is paused.
	bool scheduled_procedure_is_inprogress(ScheduledProcedureId p_id) const;
	bool scheduled_procedure_is_paused(ScheduledProcedureId p_id) const;
	/// Returns the remaining frames of this procedure according to its status (Playing, Paused, Stop)
	std::uint32_t scheduled_procedure_remaining_frames(ScheduledProcedureId p_id, GlobalFrameIndex p_current_frame) const;
	/// Returns true when the procedure is outdated
	bool scheduled_procedure_is_outdated(ScheduledProcedureId p_id, GlobalFrameIndex p_current_frame) const;

	GlobalFrameIndex scheduled_procedure_get_execute_frame(ScheduledProcedureId p_id) const;
	const DataBuffer &scheduled_procedure_get_args(ScheduledProcedureId p_id) const;

	const std::vector<ScheduledProcedureInfo> &get_scheduled_procedures() const {
		return scheduled_procedures;
	}
};

NS_NAMESPACE_END