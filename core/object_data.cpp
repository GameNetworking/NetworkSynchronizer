#include "object_data.h"

#include "core.h"
#include "ensure.h"
#include "object_data_storage.h"

NS_NAMESPACE_BEGIN

void NameAndVar::copy(const NameAndVar &p_nav) {
	name = p_nav.name;
	value.copy(p_nav.value);
}

VarDescriptor::VarDescriptor(VarId p_id, const std::string &p_name, VarData &&p_val, bool p_skip_rewinding, bool p_enabled) :
		id(p_id),
		skip_rewinding(p_skip_rewinding),
		enabled(p_enabled) {
	var.name = p_name;
	var.value = std::move(p_val);
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
	for (int process_phase = PROCESSPHASE_EARLY; process_phase < PROCESSPHASE_COUNT; ++process_phase) {
		if (functions[process_phase].size() > 0) {
			return true;
		}
	}
	return false;
}

bool ObjectData::can_trickled_sync() const {
	return func_trickled_collect && func_trickled_apply;
}

void ObjectData::set_controlled_by_peer(
		int p_peer,
		std::function<void(double /*delta*/, DataBuffer & /*r_data_buffer*/)> p_collect_input_func,
		std::function<int(DataBuffer & /*p_data_buffer*/)> p_count_input_size_func,
		std::function<bool(DataBuffer & /*p_data_buffer_A*/, DataBuffer & /*p_data_buffer_B*/)> p_are_inputs_different_func,
		std::function<void(double /*delta*/, DataBuffer & /*p_data_buffer*/)> p_process_func) {
	if (p_peer == controlled_by_peer) {
		return;
	}

	const int old_peer = controlled_by_peer;
	controlled_by_peer = p_peer;

	storage.notify_set_controlled_by_peer(old_peer, *this);

	if (controlled_by_peer != -1) {
		ENSURE_MSG(p_collect_input_func, "The function collect_input_func is not valid.");
		ENSURE_MSG(p_count_input_size_func, "The function count_input_size is not valid.");
		ENSURE_MSG(p_are_inputs_different_func, "The function are_inputs_different is not valid.");
		ENSURE_MSG(p_process_func, "The function process is not valid.");
	}

	controller_funcs.collect_input = p_collect_input_func;
	controller_funcs.count_input_size = p_count_input_size_func;
	controller_funcs.are_inputs_different = p_are_inputs_different_func;
	controller_funcs.process = p_process_func;
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
