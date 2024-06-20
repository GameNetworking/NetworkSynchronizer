#include "object_data.h"

#include "core.h"
#include "ensure.h"
#include "object_data_storage.h"

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
		VarDataSetFunc p_set_func,
		VarDataGetFunc p_get_func,
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
	ASSERT_COND_MSG(set_func, "Please ensure that all the functions have a valid set function.");
	ASSERT_COND_MSG(get_func, "Please ensure that all the functions have a valid get function.");
}

bool VarDescriptor::operator<(const VarDescriptor &p_other) const {
	return id < p_other.id;
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

bool ObjectData::setup_controller(
		std::function<void(float /*delta*/, DataBuffer & /*r_data_buffer*/)> p_collect_input_func,
		std::function<int(DataBuffer & /*p_data_buffer*/)> p_count_input_size_func,
		std::function<bool(DataBuffer & /*p_data_buffer_A*/, DataBuffer & /*p_data_buffer_B*/)> p_are_inputs_different_func,
		std::function<void(float /*delta*/, DataBuffer & /*p_data_buffer*/)> p_process_func) {

	if (controlled_by_peer != -1) {
		NS_ENSURE_V_MSG(p_collect_input_func, false, "The function collect_input_func is not valid.");
		NS_ENSURE_V_MSG(p_count_input_size_func, false, "The function count_input_size is not valid.");
		NS_ENSURE_V_MSG(p_are_inputs_different_func, false, "The function are_inputs_different is not valid.");
		NS_ENSURE_V_MSG(p_process_func, false, "The function process is not valid.");
	}

	controller_funcs.collect_input = p_collect_input_func;
	controller_funcs.count_input_size = p_count_input_size_func;
	controller_funcs.are_inputs_different = p_are_inputs_different_func;
	controller_funcs.process = p_process_func;
	
	return true;
}

bool ObjectData::set_controlled_by_peer(int p_peer) {
	if (p_peer == controlled_by_peer) {
		return false;
	}
	
	const int old_peer = controlled_by_peer;
	controlled_by_peer = p_peer;
	storage.notify_set_controlled_by_peer(old_peer, *this);
	
	return true;
}

int ObjectData::get_controlled_by_peer() const {
	return controlled_by_peer;
}

VarId ObjectData::find_variable_id(const std::string &p_var_name) const {
	for (const auto &v : vars) {
		if (v.var.name == p_var_name) {
			return v.id;
		}
	}

	return VarId::NONE;
}

NS_NAMESPACE_END
