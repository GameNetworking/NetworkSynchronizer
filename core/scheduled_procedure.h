#pragma once

#include "core.h"
#include "data_buffer.h"

NS_NAMESPACE_BEGIN
struct ScheduledProcedureExeInfo {
	union {
		std::uint32_t unique_id;

		struct {
			ObjectNetId object_net_id;
			ScheduledProcedureId procedure_id;
			ScheduledProcedureId _not_used;
		};
	} procedure_info_id;

	GlobalFrameIndex execute_at_frame;
	DataBuffer arguments;

	ScheduledProcedureExeInfo(
			ObjectNetId p_object_local_id,
			ScheduledProcedureId p_procedure_id,
			GlobalFrameIndex p_execute_at_frame,
			const DataBuffer &p_arguments = DataBuffer()) :
		execute_at_frame(p_execute_at_frame),
		arguments(p_arguments) {
		static_assert(sizeof(procedure_info_id) == sizeof(std::uint32_t));
		procedure_info_id.object_net_id = p_object_local_id;
		procedure_info_id.procedure_id = p_procedure_id;
		procedure_info_id._not_used = ScheduledProcedureId::NONE;
	}

	bool operator<(const ScheduledProcedureExeInfo &p_other) const {
		return procedure_info_id.unique_id < p_other.procedure_info_id.unique_id;
	}

	bool operator==(const ScheduledProcedureExeInfo &p_other) const {
		return procedure_info_id.unique_id == p_other.procedure_info_id.unique_id;
	}

	bool equals(const ScheduledProcedureExeInfo &p_other) const {
		return
				procedure_info_id.unique_id == p_other.procedure_info_id.unique_id
				&& execute_at_frame == p_other.execute_at_frame
				&& arguments == p_other.arguments;
	}

	ObjectNetId get_object_net_id() const {
		return procedure_info_id.object_net_id;
	}

	ScheduledProcedureId get_scheduled_procedure_id() const {
		return procedure_info_id.procedure_id;
	}
};

NS_NAMESPACE_END