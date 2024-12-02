#pragma once

#include "core.h"

NS_NAMESPACE_BEGIN
struct ScheduledProcedureHandle {
	ObjectNetId object_net_id;
	ScheduledProcedureId procedure_id;

	ScheduledProcedureHandle(
			ObjectNetId p_object_local_id,
			ScheduledProcedureId p_procedure_id) {
		object_net_id = p_object_local_id;
		procedure_id = p_procedure_id;
	}

	bool operator<(const ScheduledProcedureHandle &p_other) const {
		if (get_object_net_id() == p_other.get_object_net_id()) {
			return get_scheduled_procedure_id() < p_other.get_scheduled_procedure_id();
		} else {
			return get_object_net_id() < p_other.get_object_net_id();
		}
	}

	bool operator==(const ScheduledProcedureHandle &p_other) const {
		return object_net_id == p_other.object_net_id && procedure_id == p_other.procedure_id;
	}

	ObjectNetId get_object_net_id() const {
		return object_net_id;
	}

	ScheduledProcedureId get_scheduled_procedure_id() const {
		return procedure_id;
	}
};

NS_NAMESPACE_END