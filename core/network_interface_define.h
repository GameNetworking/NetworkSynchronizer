#pragma once

#include "core.h"
#include "data_buffer.h"
#include <functional>

NS_NAMESPACE_BEGIN
	struct RPCInfo {
		bool is_reliable = false;
		bool call_local = false;
		std::function<void(DataBuffer &p_db)> func;
	};
NS_NAMESPACE_END
