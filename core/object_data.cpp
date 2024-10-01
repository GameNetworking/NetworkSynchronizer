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
		synchronizer.get_synchronizer_internal()->on_object_data_controller_changed(this, old_peer);
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

NS_NAMESPACE_END