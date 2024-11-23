#include "object_data.h"

#include "core.h"
#include "ensure.h"
#include "object_data_storage.h"
#include "peer_networked_controller.h"
#include "../scene_synchronizer.h"

NS_NAMESPACE_BEGIN
NameAndVar::NameAndVar(NameAndVar &&p_other) :
	name(std::move(p_other.name)),
	value(std::move(p_other.value)) {
}

NameAndVar &NameAndVar::operator=(NameAndVar &&p_other) {
	name = std::move(p_other.name);
	value = std::move(p_other.value);
	return *this;
}

void NameAndVar::copy(const NameAndVar &p_nav) {
	name = p_nav.name;
	value.copy(p_nav.value);
}

NameAndVar NameAndVar::make_copy(const NameAndVar &p_other) {
	NameAndVar named_var;
	named_var.name = p_other.name;
	named_var.value.copy(p_other.value);
	return named_var;
}

VarDescriptor::VarDescriptor(
		VarId p_id,
		const std::string &p_name,
		std::uint8_t p_type,
		VarData &&p_val,
		NS_VarDataSetFunc p_set_func,
		NS_VarDataGetFunc p_get_func,
		bool p_skip_rewinding,
		bool p_enabled) :
	id(p_id),
	type(p_type),
	set_func(p_set_func),
	get_func(p_get_func),
	skip_rewinding(p_skip_rewinding),
	enabled(p_enabled) {
	var.name = p_name;
	var.value = std::move(p_val);
	NS_ASSERT_COND(id != NS::VarId::NONE);
	NS_ASSERT_COND_MSG(set_func, "Please ensure that all the functions have a valid set function.");
	NS_ASSERT_COND_MSG(get_func, "Please ensure that all the functions have a valid get function.");
}

ObjectData::ObjectData(ObjectDataStorage &p_storage) :
	storage(p_storage) {
}

void ObjectData::set_net_id(ObjectNetId p_id) {
	storage.object_set_net_id(*this, p_id);
}

ObjectNetId ObjectData::get_net_id() const {
	return net_id;
}

ObjectLocalId ObjectData::get_local_id() const {
	return local_id;
}

bool ObjectData::has_registered_process_functions() const {
	for (int process_phase = PROCESS_PHASE_EARLY; process_phase < PROCESS_PHASE_COUNT; ++process_phase) {
		if (functions[process_phase].size() > 0) {
			return true;
		}
	}
	return false;
}

bool ObjectData::can_trickled_sync() const {
	return func_trickled_collect && func_trickled_apply;
}

void ObjectData::setup_controller(
		std::function<void(float /*delta*/, DataBuffer & /*r_data_buffer*/)> p_collect_input_func,
		std::function<bool(DataBuffer & /*p_data_buffer_A*/, DataBuffer & /*p_data_buffer_B*/)> p_are_inputs_different_func,
		std::function<void(float /*delta*/, DataBuffer & /*p_data_buffer*/)> p_process_func) {
	controller_funcs.collect_input = p_collect_input_func;
	controller_funcs.are_inputs_different = p_are_inputs_different_func;
	controller_funcs.process = p_process_func;
}

bool ObjectData::set_controlled_by_peer(SceneSynchronizerBase &synchronizer, int p_peer) {
	if (p_peer == controlled_by_peer) {
		return false;
	}

	const int old_peer = controlled_by_peer;
	controlled_by_peer = p_peer;
	storage.notify_set_controlled_by_peer(old_peer, *this);

	if (old_peer > 0) {
		PeerNetworkedController *prev_controller = synchronizer.get_controller_for_peer(old_peer);
		if (prev_controller) {
			prev_controller->notify_controllable_objects_changed();
		}
	}

	if (p_peer > 0) {
		PeerNetworkedController *controller = synchronizer.get_controller_for_peer(p_peer);
		if (controller) {
			controller->notify_controllable_objects_changed();
		}
	}

	if (synchronizer.get_synchronizer_internal()) {
		synchronizer.get_synchronizer_internal()->on_object_data_controller_changed(*this, old_peer);
	}

	return true;
}

int ObjectData::get_controlled_by_peer() const {
	return controlled_by_peer;
}

void ObjectData::set_object_name(const std::string &name, bool p_force_set) {
	if (name == object_name && !p_force_set) {
		return;
	}
	bool need_update = object_name.empty();
	object_name = name;
	need_update = need_update || object_name.empty();
	if (need_update) {
		storage.notify_object_name_unnamed_changed(*this);
	}
}

const std::string &ObjectData::get_object_name() const {
	return object_name;
}

VarId ObjectData::find_variable_id(const std::string &p_var_name) const {
	for (const auto &v : vars) {
		if (v.var.name == p_var_name) {
			return v.id;
		}
	}

	return VarId::NONE;
}

ScheduledProcedureId ObjectData::scheduled_procedure_add(NS_ScheduledProcedureFunc p_func) {
	ScheduledProcedureId id = ScheduledProcedureId::NONE;

	for (int i = 0; i < scheduled_procedures.size(); i++) {
		if (scheduled_procedures[i].func == nullptr) {
			scheduled_procedures[i].func = p_func;
			id.id = ScheduledProcedureId::IdType(i);
			return id;
		}
	}

	NS_ASSERT_COND(scheduled_procedures.size() < std::numeric_limits<ScheduledProcedureId::IdType>::max());
	id.id = ScheduledProcedureId::IdType(scheduled_procedures.size());
	scheduled_procedures.push_back(ScheduledProcedureInfo{ p_func });

	return id;
}

bool ObjectData::scheduled_procedure_exist(ScheduledProcedureId p_id) const {
	return p_id.id < scheduled_procedures.size() && scheduled_procedures[p_id.id].func != nullptr;
}

void ObjectData::scheduled_procedure_remove(ScheduledProcedureId p_id) {
	scheduled_procedures[p_id.id].func = nullptr;
	scheduled_procedures[p_id.id].execute_frame = GlobalFrameIndex{ 0 };
	scheduled_procedures[p_id.id].paused_frame = GlobalFrameIndex{ 0 };
	scheduled_procedures[p_id.id].args = DataBuffer();
	storage.notify_scheduled_procedure_updated(*this, p_id, false);
}


void ObjectData::scheduled_procedure_fetch_args(ScheduledProcedureId p_id, const SynchronizerManager &p_sync_manager, SceneSynchronizerDebugger &p_debugger) {
	scheduled_procedures[p_id.id].args.begin_write(p_debugger, 0);
	scheduled_procedures[p_id.id].func(
			p_sync_manager,
			app_object_handle,
			ScheduledProcedurePhase::COLLECTING_ARGUMENTS,
			scheduled_procedures[p_id.id].args);
#ifdef NS_DEBUG_ENABLED
	NS_ASSERT_COND(!scheduled_procedures[p_id.id].args.is_buffer_failed());
#endif
}

void ObjectData::scheduled_procedure_set_args(ScheduledProcedureId p_id, const DataBuffer &p_args) {
	scheduled_procedures[p_id.id].args.copy(p_args);
}

void ObjectData::scheduled_procedure_reset_to(ScheduledProcedureId p_id, const ScheduledProcedureSnapshot &p_snapshot) {
	if (p_snapshot.execute_frame.id != 0 && p_snapshot.paused_frame.id == 0) {
		scheduled_procedure_set_args(p_id, p_snapshot.args);
		scheduled_procedure_start(p_id, p_snapshot.execute_frame);
	} else if (p_snapshot.paused_frame.id != 0) {
		scheduled_procedure_pause(p_id, p_snapshot.execute_frame, p_snapshot.paused_frame);
	} else {
		scheduled_procedure_stop(p_id);
	}
}

void ObjectData::scheduled_procedure_execute(ScheduledProcedureId p_id, ScheduledProcedurePhase p_phase, const SynchronizerManager &p_sync_manager, SceneSynchronizerDebugger &p_debugger) {
#ifdef NS_DEBUG_ENABLED
	NS_ASSERT_COND(!scheduled_procedures[p_id.id].args.is_buffer_failed());
#endif
	scheduled_procedures[p_id.id].args.begin_read(p_debugger);
	scheduled_procedures[p_id.id].func(
			p_sync_manager,
			app_object_handle,
			p_phase,
			scheduled_procedures[p_id.id].args);
}

void ObjectData::scheduled_procedure_start(ScheduledProcedureId p_id, GlobalFrameIndex p_executes_at_frame) {
	scheduled_procedures[p_id.id].execute_frame = p_executes_at_frame;
	scheduled_procedures[p_id.id].paused_frame = GlobalFrameIndex{ 0 };
	storage.notify_scheduled_procedure_updated(*this, p_id, true);
}

void ObjectData::scheduled_procedure_pause(ScheduledProcedureId p_id, GlobalFrameIndex p_current_frame) {
	scheduled_procedure_pause(p_id, scheduled_procedures[p_id.id].execute_frame, p_current_frame);
}

void ObjectData::scheduled_procedure_pause(ScheduledProcedureId p_id, GlobalFrameIndex p_executes_at_frame, GlobalFrameIndex p_current_frame) {
	scheduled_procedures[p_id.id].execute_frame = p_executes_at_frame;
	scheduled_procedures[p_id.id].paused_frame = p_current_frame;
	storage.notify_scheduled_procedure_updated(*this, p_id, false);
}

void ObjectData::scheduled_procedure_stop(ScheduledProcedureId p_id) {
	scheduled_procedures[p_id.id].execute_frame = GlobalFrameIndex{ 0 };
	scheduled_procedures[p_id.id].paused_frame = GlobalFrameIndex{ 0 };
	storage.notify_scheduled_procedure_updated(*this, p_id, false);
}

bool ObjectData::scheduled_procedure_is_inprogress(ScheduledProcedureId p_id) const {
	return scheduled_procedures[p_id.id].paused_frame == GlobalFrameIndex{ 0 }
			&& scheduled_procedures[p_id.id].execute_frame > GlobalFrameIndex{ 0 };
}

bool ObjectData::scheduled_procedure_is_paused(ScheduledProcedureId p_id) const {
	return scheduled_procedures[p_id.id].paused_frame > GlobalFrameIndex{ 0 };
}

std::uint32_t ObjectData::scheduled_procedure_remaining_frames(ScheduledProcedureId p_id, GlobalFrameIndex p_current_frame) const {
	if (scheduled_procedures[p_id.id].paused_frame.id > 0) {
		if (scheduled_procedures[p_id.id].paused_frame < scheduled_procedures[p_id.id].execute_frame) {
			return scheduled_procedures[p_id.id].execute_frame.id - scheduled_procedures[p_id.id].paused_frame.id;
		}
	} else if (scheduled_procedures[p_id.id].execute_frame.id > 0) {
		if (p_current_frame.id < scheduled_procedures[p_id.id].execute_frame.id) {
			return scheduled_procedures[p_id.id].execute_frame.id - p_current_frame.id;
		}
	}
	return 0;
}

GlobalFrameIndex ObjectData::scheduled_procedure_get_execute_frame(ScheduledProcedureId p_id) const {
	if (scheduled_procedures[p_id.id].paused_frame.id == 0) {
		return scheduled_procedures[p_id.id].execute_frame;
	}
	return GlobalFrameIndex{ 0 };
}

const DataBuffer &ObjectData::scheduled_procedure_get_args(ScheduledProcedureId p_id) const {
	return scheduled_procedures[p_id.id].args;
}

NS_NAMESPACE_END