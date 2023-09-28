#include "object_data.h"

#include "object_data_storage.h"

NS_NAMESPACE_BEGIN

VarData::VarData(const StringName &p_name) {
	var.name = p_name;
}

VarData::VarData(NetVarId p_id, const StringName &p_name, const Variant &p_val, bool p_skip_rewinding, bool p_enabled) :
		id(p_id),
		skip_rewinding(p_skip_rewinding),
		enabled(p_enabled) {
	var.name = p_name;
	var.value = p_val.duplicate(true);
}

bool VarData::operator==(const VarData &p_other) const {
	return var.name == p_other.var.name;
}

bool VarData::operator<(const VarData &p_other) const {
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

ObjectLocalId ObjectData::get_local_id(ObjectLocalId p_id) {
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

bool ObjectData::can_deferred_sync() const {
	return collect_epoch_func.is_valid() && apply_epoch_func.is_valid();
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

NS_NAMESPACE_END
