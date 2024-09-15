#include "scene_synchronizer_debugger.h"

#ifdef NS_DEBUG_ENABLED

// At the moment this debugger is disabled. We need an easier solution to write the UI debugger.
#ifdef UI_DEBUGGER_ENABLED
#include "__generated__debugger_ui.h"
#endif

#include "data_buffer.h"
#include "scene_synchronizer_debugger_json_storage.h"

#endif

#include "../scene_synchronizer.h"

NS_NAMESPACE_BEGIN
SceneSynchronizerDebugger::SceneSynchronizerDebugger() {
#ifdef NS_DEBUG_ENABLED
	frame_dump_storage = new SceneSynchronizerDebuggerJsonStorage;
#endif
}

SceneSynchronizerDebugger::~SceneSynchronizerDebugger() {
#ifdef NS_DEBUG_ENABLED
	delete frame_dump_storage;
	frame_dump_storage = nullptr;
#endif
}

SceneSynchronizerDebugger &SceneSynchronizerDebugger::get_debugger() const {
	return *const_cast<SceneSynchronizerDebugger *>(this);
}

void SceneSynchronizerDebugger::set_file_system(NS::FileSystem *p_file_system) {
#ifdef NS_DEBUG_ENABLED
	file_system = p_file_system;
#endif
}

void SceneSynchronizerDebugger::set_log_level(NS::PrintMessageType p_log_level) {
	log_level = p_log_level;
}

NS::PrintMessageType SceneSynchronizerDebugger::get_log_level() const {
	return log_level;
}

void SceneSynchronizerDebugger::set_dump_enabled(bool p_dump_enabled) {
#ifdef NS_DEBUG_ENABLED
	dump_enabled = p_dump_enabled;
#endif
}

bool SceneSynchronizerDebugger::get_dump_enabled() const {
#ifdef NS_DEBUG_ENABLED
	return dump_enabled;
#else
	return false;
#endif
}

void SceneSynchronizerDebugger::setup_debugger(const std::string &p_dump_name, int p_peer) {
#ifdef NS_DEBUG_ENABLED
	if (setup_done == false) {
		setup_done = true;
	}

	if (file_system) {
		// Setup directories.
		main_dump_directory_path = file_system->get_base_dir() + "/net-sync-debugs/dump";
		dump_name = p_dump_name;
	}

	prepare_dumping(p_peer);
	setup_debugger_python_ui();
#endif
}

void SceneSynchronizerDebugger::prepare_dumping(int p_peer) {
#ifdef NS_DEBUG_ENABLED
	if (!dump_enabled) {
		// Dumping is disabled, nothing to do.
		return;
	}

	NS_ENSURE_MSG(file_system, "Please set the FileSystem using the function set_file_system().");

	// Prepare the dir.
	{
		std::string path = main_dump_directory_path + "/" + dump_name;
		NS_ENSURE(file_system->make_dir_recursive(path, true));
	}

	// Store generic info about this dump.
	{
		nlohmann::json d;
		d["dump-name"] = dump_name;
		d["peer"] = p_peer;
		d["date"] = file_system->get_date();
		d["time"] = file_system->get_time();

		NS_ENSURE(file_system->store_file_string(
			main_dump_directory_path + "/" + "dump-info-" + dump_name + /*"-" + std::to_string(p_peer) +*/ ".json",
			d.dump()));
	}

#endif
}

void SceneSynchronizerDebugger::setup_debugger_python_ui() {
#ifdef UI_DEBUGGER_ENABLED
#ifdef NS_DEBUG_ENABLED
	NS_ENSURE_MSG(file_system, "Please set the FileSystem using the function set_file_system().");

	// Verify if file exists.
	const std::string path = main_dump_directory_path + "/debugger.py";

	if (file_system->file_exists(path.c_str())) {
		// Nothing to do.
		return;
	}

	// Copy the python UI into the directory.
	NS_ENSURE(file_system->store_file_buffer(path, (std::uint8_t *)__debugger_ui_code, __debugger_ui_code_size));
#endif
#endif
}

void SceneSynchronizerDebugger::write_dump(int p_peer, uint32_t p_frame_index) {
#ifdef NS_DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	if (p_frame_index == UINT32_MAX) {
		// Nothing to write.
		return;
	}

	NS_ENSURE_MSG(file_system, "Please set the FileSystem using the function set_file_system().");

	std::string file_path = "";
	{
		int iteration = 0;
		std::string iteration_mark = "";
		do {
			file_path = main_dump_directory_path + "/" + dump_name + "/fd-" /*+ std::to_string(p_peer) + "-"*/ + std::to_string(p_frame_index) + iteration_mark + ".json";
			iteration_mark += "@";
			iteration += 1;
		} while (file_system->file_exists(file_path) && iteration < 100);
	}

	std::string frame_summary;

	if (frame_dump_storage->frame_dump__has_warnings) {
		frame_summary += "* ";
	} else if (frame_dump_storage->frame_dump__has_errors) {
		frame_summary += "!️ ";
	}

	if ((frame_dump__frame_events & FrameEvent::CLIENT_DESYNC_DETECTED) > 0) {
		frame_summary += "Client desync; ";
	} else if ((frame_dump__frame_events & FrameEvent::CLIENT_DESYNC_DETECTED_SOFT) > 0) {
		frame_summary += "Client desync; No controller rewind; ";
	}

	nlohmann::json d;
	d["frame"] = p_frame_index;
	d["peer"] = p_peer;
	d["frame_summary"] = frame_summary;
	d["begin_state"] = frame_dump_storage->frame_dump__begin_state;
	d["end_state"] = frame_dump_storage->frame_dump__end_state;
	d["node_log"] = frame_dump_storage->frame_dump__node_log;
	d["data_buffer_writes"] = frame_dump_storage->frame_dump__data_buffer_writes;
	d["data_buffer_reads"] = frame_dump_storage->frame_dump__data_buffer_reads;
	d["are_inputs_different_results"] = frame_dump_storage->frame_dump__are_inputs_different_results;

	NS_ENSURE(file_system->store_file_string(file_path, d.dump()));
#endif
}

void SceneSynchronizerDebugger::start_new_frame() {
#ifdef NS_DEBUG_ENABLED
	frame_dump_storage->frame_dump__node_log.clear();
	frame_dump__frame_events = FrameEvent::EMPTY;
	frame_dump_storage->frame_dump__has_warnings = false;
	frame_dump_storage->frame_dump__has_errors = false;
	frame_dump_storage->frame_dump__data_buffer_writes.clear();
	frame_dump_storage->frame_dump__data_buffer_reads.clear();
	frame_dump_storage->frame_dump__are_inputs_different_results.clear();
	log_counter = 0;
#endif
}

#ifdef NS_DEBUG_ENABLED
std::string data_type_to_string(uint32_t p_type) {
	switch (p_type) {
		case NS::DataBuffer::DATA_TYPE_BOOL:
			return "Bool";
		case NS::DataBuffer::DATA_TYPE_INT:
			return "Int";
		case NS::DataBuffer::DATA_TYPE_UINT:
			return "Uint";
		case NS::DataBuffer::DATA_TYPE_REAL:
			return "Real";
		case NS::DataBuffer::DATA_TYPE_POSITIVE_UNIT_REAL:
			return "Positive Unit Real";
		case NS::DataBuffer::DATA_TYPE_UNIT_REAL:
			return "Unit Real";
		case NS::DataBuffer::DATA_TYPE_VECTOR2:
			return "Vector2";
		case NS::DataBuffer::DATA_TYPE_NORMALIZED_VECTOR2:
			return "Normalized Vector2";
		case NS::DataBuffer::DATA_TYPE_VECTOR3:
			return "Vector3";
		case NS::DataBuffer::DATA_TYPE_NORMALIZED_VECTOR3:
			return "Normalized Vector3";
		case NS::DataBuffer::DATA_TYPE_DATABUFFER:
			return "Variant";
	}

	return "UNDEFINED";
}

std::string compression_level_to_string(uint32_t p_type) {
	switch (p_type) {
		case NS::DataBuffer::COMPRESSION_LEVEL_0:
			return "Compression Level 0";
		case NS::DataBuffer::COMPRESSION_LEVEL_1:
			return "Compression Level 1";
		case NS::DataBuffer::COMPRESSION_LEVEL_2:
			return "Compression Level 2";
		case NS::DataBuffer::COMPRESSION_LEVEL_3:
			return "Compression Level 3";
	}

	return "Compression Level UNDEFINED";
}
#endif

#ifdef NS_DEBUG_ENABLED
void dump_tracked_objects(const SceneSynchronizerBase *p_scene_sync, nlohmann::json::object_t &p_dump) {
	p_dump.clear();

	for (ObjectData *od : p_scene_sync->get_sorted_objects_data()) {
		nlohmann::json object_dump;

		const std::string object_name = p_scene_sync->get_synchronizer_manager().fetch_object_name(od->app_object_handle);
		object_dump["object_name"] = object_name;

		for (VarDescriptor &var_desc : od->vars) {
			std::string prefix;
			// Notice at the moment we are reading only the registered vars, so this is always true.
			// In the past we were also reading all other variables associated to this class.
			const bool is_variable_registered_as_sync = true;
			if (is_variable_registered_as_sync) {
				prefix = "* ";
			}

			VarData value;
			var_desc.get_func(
					p_scene_sync->get_synchronizer_manager(),
					od->app_object_handle,
					var_desc.var.name,
					value);
			object_dump[prefix + var_desc.var.name + "::" + std::to_string(var_desc.type)] = p_scene_sync->var_data_stringify(value, true);
		}

		p_dump[object_name] = object_dump;
	}
}
#endif

void SceneSynchronizerDebugger::scene_sync_process_start(const SceneSynchronizerBase *p_scene_sync) {
#ifdef NS_DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	dump_tracked_objects(p_scene_sync, frame_dump_storage->frame_dump__begin_state);
#endif
}

void SceneSynchronizerDebugger::scene_sync_process_end(const NS::SceneSynchronizerBase *p_scene_sync) {
#ifdef NS_DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	dump_tracked_objects(p_scene_sync, frame_dump_storage->frame_dump__end_state);
#endif
}

void SceneSynchronizerDebugger::databuffer_operation_begin_record(int p_peer, DataBufferDumpMode p_mode) {
#ifdef NS_DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	frame_dump_storage->frame_dump__data_buffer_name = "CONTROLLER-" + std::to_string(p_peer);
	frame_dump_data_buffer_dump_mode = p_mode;

	if (frame_dump_data_buffer_dump_mode == DataBufferDumpMode::WRITE) {
		print(NS::PrintMessageType::VERBOSE, "[WRITE] DataBuffer start write.", frame_dump_storage->frame_dump__data_buffer_name);
	} else {
		print(NS::PrintMessageType::VERBOSE, "[READ] DataBuffer start read.", frame_dump_storage->frame_dump__data_buffer_name);
	}
#endif
}

void SceneSynchronizerDebugger::databuffer_operation_end_record() {
#ifdef NS_DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	if (frame_dump_data_buffer_dump_mode == DataBufferDumpMode::WRITE) {
		print(NS::PrintMessageType::VERBOSE, "[WRITE] end.", frame_dump_storage->frame_dump__data_buffer_name);
	} else {
		print(NS::PrintMessageType::VERBOSE, "[READ] end.", frame_dump_storage->frame_dump__data_buffer_name);
	}

	frame_dump_data_buffer_dump_mode = DataBufferDumpMode::NONE;
	frame_dump_storage->frame_dump__data_buffer_name = "";
#endif
}

void SceneSynchronizerDebugger::databuffer_write(uint32_t p_data_type, uint32_t p_compression_level, int p_new_bit_offset, const char *val_string) {
#ifdef NS_DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	if (frame_dump_data_buffer_dump_mode != DataBufferDumpMode::WRITE) {
		return;
	}

	frame_dump_storage->frame_dump__data_buffer_writes.push_back(val_string);

	const std::string operation = "[WRITE]      [" + compression_level_to_string(p_compression_level) + "] [" + data_type_to_string(p_data_type) + "] [new offset: " + std::to_string(p_new_bit_offset) + "] " + val_string;

	print(NS::PrintMessageType::VERBOSE, operation, frame_dump_storage->frame_dump__data_buffer_name);
#endif
}

void SceneSynchronizerDebugger::databuffer_read(uint32_t p_data_type, uint32_t p_compression_level, int p_new_bit_offset, const char *val_string) {
#ifdef NS_DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	if (frame_dump_data_buffer_dump_mode != DataBufferDumpMode::READ) {
		return;
	}

	frame_dump_storage->frame_dump__data_buffer_reads.push_back(val_string);

	const std::string operation = "[READ]     [" + compression_level_to_string(p_compression_level) + "] [" + data_type_to_string(p_data_type) + "] [new offset: " + std::to_string(p_new_bit_offset) + "] " + val_string;

	print(NS::PrintMessageType::VERBOSE, operation, frame_dump_storage->frame_dump__data_buffer_name);
#endif
}

void SceneSynchronizerDebugger::notify_input_sent_to_server(int p_peer, uint32_t p_frame_index, uint32_t p_input_index) {
#ifdef NS_DEBUG_ENABLED
	print(NS::INFO, "The client sent to server the input `" + std::to_string(p_input_index) + "` for frame:`" + std::to_string(p_frame_index) + "`.", "CONTROLLER-" + std::to_string(p_peer));
#endif
}

void SceneSynchronizerDebugger::notify_are_inputs_different_result(
		int p_peer,
		uint32_t p_other_frame_index,
		bool p_is_similar) {
#ifdef NS_DEBUG_ENABLED
	if (p_is_similar) {
		print(NS::INFO, "This frame input is SIMILAR to `" + std::to_string(p_other_frame_index) + "`", "CONTROLLER-" + std::to_string(p_peer));
	} else {
		print(NS::INFO, "This frame input is DIFFERENT to `" + std::to_string(p_other_frame_index) + "`", "CONTROLLER-" + std::to_string(p_peer));
	}
	frame_dump_storage->frame_dump__are_inputs_different_results[std::to_string(p_other_frame_index)] = p_is_similar;
#endif
}

void SceneSynchronizerDebugger::print(NS::PrintMessageType p_level, const std::string &p_message, const std::string &p_object_name, bool p_force_print_to_log) {
#ifdef NS_DEBUG_ENABLED

	if (NS::PrintMessageType::WARNING & p_level) {
		frame_dump_storage->frame_dump__has_warnings = true;
	}

	if (NS::PrintMessageType::ERROR & p_level) {
		frame_dump_storage->frame_dump__has_errors = true;
	}

	const std::string log_level_str = NS::get_log_level_txt(p_level);

	if ((log_level <= p_level) || p_force_print_to_log) {
		NS::SceneSynchronizerBase::__print_line(log_level_str + "[" + p_object_name + "] " + p_message);
	}

	__add_message(log_level_str + p_message, p_object_name);
#else
	if ((log_level <= p_level) || p_force_print_to_log) {
		const std::string log_level_str = NS::get_log_level_txt(p_level);
		NS::SceneSynchronizerBase::__print_line(log_level_str + "[" + p_object_name + "] " + p_message);
	}
#endif
}

void SceneSynchronizerDebugger::notify_event(FrameEvent p_event) {
#ifdef NS_DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	frame_dump__frame_events = FrameEvent(frame_dump__frame_events | p_event);
#endif
}

void SceneSynchronizerDebugger::__add_message(const std::string &p_message, const std::string &p_object_name) {
#ifdef NS_DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	nlohmann::json m;
	m["i"] = log_counter;
	m["m"] = p_message;
	frame_dump_storage->frame_dump__node_log[p_object_name].push_back(m);

	log_counter += 1;
#endif
}

NS_NAMESPACE_END