#pragma once

#include "core/string/ustring.h"
#include "core/variant/array.h"

namespace NetUtility {
class NodeData;
};

typedef uint32_t NetActionId;

struct NetActionProcessor {
	NetUtility::NodeData *nd;
	NetActionId action_id;
	Array vars;

	NetActionProcessor() = default;
	NetActionProcessor(
			NetUtility::NodeData *p_nd,
			NetActionId p_action_id,
			const Array &p_vars) :
			nd(p_nd),
			action_id(p_action_id),
			vars(p_vars) {}

	void execute();
	bool server_validate() const;
	operator String() const;
};

struct TokenizedNetActionProcessor {
	uint32_t action_token;
	NetActionProcessor processor;

	bool operator==(const TokenizedNetActionProcessor &p_other) const { return action_token == p_other.action_token; }

	TokenizedNetActionProcessor() = default;
	TokenizedNetActionProcessor(uint32_t p_at, NetActionProcessor p_p) :
			action_token(p_at), processor(p_p) {}
};
