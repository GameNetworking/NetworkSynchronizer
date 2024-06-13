#pragma once

#include "json.hpp"

struct SceneSynchronizerDebuggerJsonStorage {
#ifdef DEBUG_ENABLED
	nlohmann::json::object_t frame_dump__begin_state;

	// JSON of dictionary containing nodes info.
	nlohmann::json::object_t frame_dump__end_state;

	// The JSON containing the data buffer operations performed by the controllers.
	nlohmann::json::object_t frame_dump__node_log;

	// This Array contains all the inputs (stringified) written on the `DataBuffer` from the
	// `_controller_process` function
	nlohmann::json::array_t frame_dump__data_buffer_writes;

	// This Array contains all the inputs (stringified) read on the `DataBuffer` from the
	// `_controller_process` function
	nlohmann::json::array_t frame_dump__data_buffer_reads;

	// This JSON contains the comparison (`_are_inputs_different`) fetched by this frame, and
	// the result.
	nlohmann::json::object_t frame_dump__are_inputs_different_results;

	// The controller name for which the data buffer operations is in progress.
	std::string frame_dump__data_buffer_name;

	bool frame_dump__has_warnings = false;
	bool frame_dump__has_errors = false;
#endif
};
