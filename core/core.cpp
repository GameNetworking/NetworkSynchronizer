
#include "core.h"
#include <limits>

NS_NAMESPACE_BEGIN

const VarId VarId::NONE = { std::numeric_limits<uint32_t>::max() };
const ObjectLocalId ObjectLocalId::NONE = { std::numeric_limits<uint32_t>::max() };
const ObjectNetId ObjectNetId::NONE = { std::numeric_limits<uint32_t>::max() };
const ObjectHandle ObjectHandle::NONE = { 0 };

static const char *ProcessPhaseName[PROCESSPHASE_COUNT] = {
	"EARLY PROCESS",
	"PRE PROCESS",
	"PROCESS",
	"POST PROCESS",
	"LATE PROCESS"
};

const char *get_process_phase_name(ProcessPhase pp) {
	return ProcessPhaseName[pp];
}

NS_NAMESPACE_END