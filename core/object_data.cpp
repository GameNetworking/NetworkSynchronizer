#include "object_data.h"

#include "modules/network_synchronizer/core/core.h"
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

void ObjectData::set_controlled_by_peer(int p_peer) {
	if (p_peer == controlled_by_peer) {
		return;
	}

	const int old_peer = controlled_by_peer;
	controlled_by_peer = p_peer;

	storage.notify_set_controlled_by_peer(old_peer, *this);
}

int ObjectData::get_controlled_by_peer() const {
	return controlled_by_peer;
}

void ObjectData::set_controller(NetworkedControllerBase *p_controller) {
	if (controller == p_controller) {
		return;
	}

	controller = p_controller;
	storage.notify_set_controller(*this);
}

NetworkedControllerBase *ObjectData::get_controller() const {
	return controller;
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
