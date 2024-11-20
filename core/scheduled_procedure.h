#pragma once

#include "core.h"
#include "data_buffer.h"

NS_NAMESPACE_BEGIN
struct ScheduledProcedureInfo {
	ObjectLocalId object_local_id;
	ScheduledProcedureId procedure_id;
	FrameIndex execute_at_frame;
	DataBuffer arguments;

	bool operator<(const ScheduledProcedureInfo &p_other) const {
		return procedure_id < p_other.procedure_id;
	}

	bool operator==(const ScheduledProcedureInfo &p_other) const {
		return object_local_id == p_other.object_local_id
				&& procedure_id == p_other.procedure_id
				&& execute_at_frame == p_other.execute_at_frame
				&& arguments == p_other.arguments;
	}
};

NS_NAMESPACE_END