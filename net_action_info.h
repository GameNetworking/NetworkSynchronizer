#pragma once

#include "core/string/string_name.h"
#include "input_network_encoder.h"
#include "net_action_processor.h"

struct NetActionInfo {
	NetActionId id = UINT32_MAX;
	/// The event function
	StringName act_func;
	/// The event function encoding
	StringName act_encoding_func;
	/// If true the client can trigger this action.
	bool can_client_trigger;
	/// If true the client who triggered the event will wait the server validation to execute the event.
	bool wait_server_validation;
	/// The function to validate the event: Only executed on the server.
	StringName server_action_validation_func;
	/// The network_encoder used to encode decode the environment data.
	Ref<InputNetworkEncoder> network_encoder;

	bool operator==(const NetActionInfo &p_other) const;
	bool operator<(const NetActionInfo &p_other) const;
};