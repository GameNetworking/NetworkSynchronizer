#include "net_action_processor.h"

#include "input_network_encoder.h"
#include "net_action_info.h"
#include "net_utilities.h"
#include "scene/main/node.h"

void NetActionProcessor::execute() {
	const NetActionInfo &info = nd->net_actions[action_id];
	nd->node->callv(info.act_func, vars);
}

bool NetActionProcessor::server_validate() const {
	const NetActionInfo &info = nd->net_actions[action_id];

	if (info.server_action_validation_func == StringName()) {
		// Always valid when the func is not set!
		return true;
	}

	const Variant is_valid = nd->node->callv(info.server_action_validation_func, vars);

	ERR_FAIL_COND_V_MSG(is_valid.get_type() != Variant::BOOL, false, "[FATAL] The function `" + nd->node->get_path() + "::" + info.server_action_validation_func + "` MUST return a bool.");

	return is_valid.operator bool();
}

NetActionProcessor::operator String() const {
	const NetActionInfo &info = nd->net_actions[action_id];
	String v = Variant(vars);
	// Strip `[]` from the Array string.
	v = v.substr(1, v.size() - 3);
	return String(nd->node->get_path()) + "::" + info.act_func + "(" + v + ")";
}