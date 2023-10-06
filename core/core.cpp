
#include "core.h"
#include <limits>

NS_NAMESPACE_BEGIN

const VarId VarId::NONE = { std::numeric_limits<uint32_t>::max() };
const ObjectLocalId ObjectLocalId::NONE = { std::numeric_limits<uint32_t>::max() };
const ObjectNetId ObjectNetId::NONE = { std::numeric_limits<uint32_t>::max() };
const ObjectHandle ObjectHandle::NONE = { 0 };

NS_NAMESPACE_END