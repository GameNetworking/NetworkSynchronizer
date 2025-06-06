#include "core.h"

#include <limits>

std::string operator+(const char *p_chr, const std::string &p_str) {
	std::string tmp = p_chr;
	tmp += p_str;
	return tmp;
}

NS_NAMESPACE_BEGIN
const GlobalFrameIndex GlobalFrameIndex::NONE = GlobalFrameIndex{ { std::numeric_limits<std::uint32_t>::max() } };
const FrameIndex FrameIndex::NONE = FrameIndex{ { std::numeric_limits<std::uint32_t>::max() } };
const SyncGroupId SyncGroupId::NONE = SyncGroupId{ { std::numeric_limits<std::uint32_t>::max() } };
const SyncGroupId SyncGroupId::GLOBAL = SyncGroupId{ { 0 } };
const VarId VarId::NONE = VarId{ { std::numeric_limits<std::uint8_t>::max() } };
const ScheduledProcedureId ScheduledProcedureId::NONE = ScheduledProcedureId{ { std::numeric_limits<std::uint8_t>::max() } };
const ObjectLocalId ObjectLocalId::NONE = ObjectLocalId{ { std::numeric_limits<uint32_t>::max() } };
const ObjectNetId ObjectNetId::NONE = ObjectNetId{ { std::numeric_limits<std::uint16_t>::max() } };
const ObjectHandle ObjectHandle::NONE = ObjectHandle{ { 0 } };
const SchemeId SchemeId::DEFAULT = SchemeId{ { 0 } };

static const char *ProcessPhaseName[PROCESS_PHASE_COUNT] = {
	"EARLY PROCESS",
	"PRE PROCESS",
	"PROCESS",
	"POST PROCESS",
	"LATE PROCESS"
};

const char *get_process_phase_name(ProcessPhase pp) {
	return ProcessPhaseName[pp];
}

std::string get_log_level_txt(NS::PrintMessageType p_level) {
	std::string log_level_str = "";
	if (NS::PrintMessageType::VERBOSE == p_level) {
		log_level_str = "[VERBOSE] ";
	} else if (NS::PrintMessageType::INFO == p_level) {
		log_level_str = "[INFO] ";
	} else if (NS::PrintMessageType::WARNING == p_level) {
		log_level_str = "[WARNING] ";
	} else if (NS::PrintMessageType::ERROR == p_level) {
		log_level_str = "[ERROR] ";
	}
	return log_level_str;
}

NS_NAMESPACE_END