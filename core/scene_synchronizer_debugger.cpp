#include "scene_synchronizer_debugger.h"

#ifdef DEBUG_ENABLED

#include "__generated__debugger_ui.h"

#include "data_buffer.h"
#include "net_utilities.h"

#endif

#include "../scene_synchronizer.h"

SceneSynchronizerDebugger *SceneSynchronizerDebugger::the_singleton = nullptr;

SceneSynchronizerDebugger *SceneSynchronizerDebugger::singleton() {
	return the_singleton;
}

SceneSynchronizerDebugger::SceneSynchronizerDebugger() {
	if (the_singleton == nullptr) {
		the_singleton = this;
	}
#ifdef DEBUG_ENABLED
// Code here
#endif
}

SceneSynchronizerDebugger::~SceneSynchronizerDebugger() {
	if (the_singleton == this) {
		the_singleton = nullptr;
	}

#ifdef DEBUG_ENABLED
	frame_dump__begin_state.clear();
	frame_dump__end_state.clear();
	frame_dump__node_log.clear();
	frame_dump__data_buffer_writes.clear();
	frame_dump__data_buffer_reads.clear();
	frame_dump__are_inputs_different_results.clear();
#endif
}

void SceneSynchronizerDebugger::set_file_system(NS::FileSystem *p_file_system) {
#ifdef DEBUG_ENABLED
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
#ifdef DEBUG_ENABLED
	dump_enabled = p_dump_enabled;
#endif
}

bool SceneSynchronizerDebugger::get_dump_enabled() const {
#ifdef DEBUG_ENABLED
	return dump_enabled;
#else
	return false;
#endif
}

void SceneSynchronizerDebugger::setup_debugger(const std::string &p_dump_name, int p_peer, SceneTree *p_scene_tree) {
#ifdef DEBUG_ENABLED
	if (setup_done == false) {
		setup_done = true;
	}

	ASSERT_COND_MSG(!file_system, "Please set the FileSystem using the function set_file_system().");

	// Setup directories.
	main_dump_directory_path = file_system->get_base_dir() + "/net-sync-debugs/dump";
	dump_name = p_dump_name;

	prepare_dumping(p_peer, p_scene_tree);
	setup_debugger_python_ui();
#endif
}

void SceneSynchronizerDebugger::prepare_dumping(int p_peer, SceneTree *p_scene_tree) {
#ifdef DEBUG_ENABLED
	if (!dump_enabled) {
		// Dumping is disabled, nothing to do.
		return;
	}

	ASSERT_COND_MSG(!file_system, "Please set the FileSystem using the function set_file_system().");

	// Prepare the dir.
	{
		std::string path = main_dump_directory_path + "/" + dump_name;
		ENSURE(file_system->make_dir_recursive(path, true));
	}

	// Store generic info about this dump.
	{
		nlohmann::json d;
		d["dump-name"] = dump_name;
		d["peer"] = p_peer;
		d["date"] = file_system->get_date();
		d["time"] = file_system->get_time();

		ENSURE(file_system->store_file_string(
				main_dump_directory_path + "/" + "dump-info-" + dump_name + /*"-" + std::to_string(p_peer) +*/ ".json",
				d.dump()));
	}

	scene_tree = p_scene_tree;
#endif
}

void SceneSynchronizerDebugger::setup_debugger_python_ui() {
#ifdef DEBUG_ENABLED
	ASSERT_COND_MSG(!file_system, "Please set the FileSystem using the function set_file_system().");

	// Verify if file exists.
	const std::string path = main_dump_directory_path + "/debugger.py";

	if (file_system->file_exists(path.c_str())) {
		// Nothing to do.
		return;
	}

	// Copy the python UI into the directory.
	ENSURE(file_system->store_file_buffer(path, (std::uint8_t *)__debugger_ui_code, __debugger_ui_code_size));
#endif
}

void SceneSynchronizerDebugger::write_dump(int p_peer, uint32_t p_frame_index) {
#ifdef DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	if (p_frame_index == UINT32_MAX) {
		// Nothing to write.
		return;
	}

	std::string file_path = "";
	{
		int iteration = 0;
		std::string iteration_mark = "";
		do {
			file_path = main_dump_directory_path + "/" + dump_name + "/fd-" /*+ itos(p_peer) + "-"*/ + std::to_string(p_frame_index) + iteration_mark + ".json";
			iteration_mark += "@";
			iteration += 1;
		} while (file_system->file_exists(file_path) && iteration < 100);
	}

	std::string frame_summary;

	if (frame_dump__has_warnings) {
		frame_summary += "* ";
	} else if (frame_dump__has_errors) {
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
	d["begin_state"] = frame_dump__begin_state;
	d["end_state"] = frame_dump__end_state;
	d["node_log"] = frame_dump__node_log;
	d["data_buffer_writes"] = frame_dump__data_buffer_writes;
	d["data_buffer_reads"] = frame_dump__data_buffer_reads;
	d["are_inputs_different_results"] = frame_dump__are_inputs_different_results;

	ENSURE(file_system->store_file_string(file_path, d.dump()));
#endif
}

void SceneSynchronizerDebugger::start_new_frame() {
#ifdef DEBUG_ENABLED
	frame_dump__node_log.clear();
	frame_dump__frame_events = FrameEvent::EMPTY;
	frame_dump__has_warnings = false;
	frame_dump__has_errors = false;
	frame_dump__data_buffer_writes.clear();
	frame_dump__data_buffer_reads.clear();
	frame_dump__are_inputs_different_results.clear();
	log_counter = 0;
#endif
}

#ifdef DEBUG_ENABLED
std::string type_to_string(Variant::Type p_type) {
	switch (p_type) {
		case Variant::NIL:
			return "NIL";
		case Variant::BOOL:
			return "BOOL";
		case Variant::INT:
			return "INT";
		case Variant::FLOAT:
			return "FLOAT";
		case Variant::STRING:
			return "STRING";
		case Variant::VECTOR2:
			return "VECTOR2";
		case Variant::VECTOR2I:
			return "VECTOR2I";
		case Variant::RECT2:
			return "RECT2";
		case Variant::RECT2I:
			return "RECT2I";
		case Variant::VECTOR3:
			return "VECTOR3";
		case Variant::VECTOR3I:
			return "VECTOR3I";
		case Variant::TRANSFORM2D:
			return "TRANSFORM2D";
		case Variant::VECTOR4:
			return "VECTOR4";
		case Variant::VECTOR4I:
			return "VECTOR4I";
		case Variant::PLANE:
			return "PLANE";
		case Variant::QUATERNION:
			return "QUATERNION";
		case Variant::AABB:
			return "AABB";
		case Variant::BASIS:
			return "BASIS";
		case Variant::TRANSFORM3D:
			return "TRANSFORM3D";
		case Variant::PROJECTION:
			return "PROJECTION";
		case Variant::COLOR:
			return "COLOR";
		case Variant::STRING_NAME:
			return "STRING_NAME";
		case Variant::NODE_PATH:
			return "NODE_PATH";
		case Variant::RID:
			return "RID";
		case Variant::OBJECT:
			return "OBJECT";
		case Variant::CALLABLE:
			return "CALLABLE";
		case Variant::SIGNAL:
			return "SIGNAL";
		case Variant::DICTIONARY:
			return "DICTIONARY";
		case Variant::ARRAY:
			return "ARRAY";
		case Variant::PACKED_BYTE_ARRAY:
			return "PACKED_BYTE_ARRAY";
		case Variant::PACKED_INT32_ARRAY:
			return "PACKED_INT32_ARRAY";
		case Variant::PACKED_INT64_ARRAY:
			return "PACKED_INT64_ARRAY";
		case Variant::PACKED_FLOAT32_ARRAY:
			return "PACKED_FLOAT32_ARRAY";
		case Variant::PACKED_FLOAT64_ARRAY:
			return "PACKED_FLOAT64_ARRAY";
		case Variant::PACKED_STRING_ARRAY:
			return "PACKED_STRING_ARRAY";
		case Variant::PACKED_VECTOR2_ARRAY:
			return "PACKED_VECTOR2_ARRAY";
		case Variant::PACKED_VECTOR3_ARRAY:
			return "PACKED_VECTOR3_ARRAY";
		case Variant::PACKED_COLOR_ARRAY:
			return "PACKED_COLOR_ARRAY";
		case Variant::PACKED_VECTOR4_ARRAY:
			return "PACKED_VECTOR4_ARRAY";
		case Variant::VARIANT_MAX:
			return "VARIANT_MAX";
	}
	return "";
}

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
		case NS::DataBuffer::DATA_TYPE_VARIANT:
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

void SceneSynchronizerDebugger::scene_sync_process_start(const NS::SceneSynchronizerBase *p_scene_sync) {
#ifdef DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	dump_tracked_objects(p_scene_sync, frame_dump__begin_state);
#endif
}

void SceneSynchronizerDebugger::scene_sync_process_end(const NS::SceneSynchronizerBase *p_scene_sync) {
#ifdef DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	dump_tracked_objects(p_scene_sync, frame_dump__end_state);
#endif
}

void SceneSynchronizerDebugger::databuffer_operation_begin_record(int p_peer, DataBufferDumpMode p_mode) {
#ifdef DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	frame_dump__data_buffer_name = "CONTROLLER-" + std::to_string(p_peer);
	frame_dump_data_buffer_dump_mode = p_mode;

	if (frame_dump_data_buffer_dump_mode == DataBufferDumpMode::WRITE) {
		print(NS::PrintMessageType::VERBOSE, "[WRITE] DataBuffer start write.", frame_dump__data_buffer_name);
	} else {
		print(NS::PrintMessageType::VERBOSE, "[READ] DataBuffer start read.", frame_dump__data_buffer_name);
	}
#endif
}

void SceneSynchronizerDebugger::databuffer_operation_end_record() {
#ifdef DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	if (frame_dump_data_buffer_dump_mode == DataBufferDumpMode::WRITE) {
		print(NS::PrintMessageType::VERBOSE, "[WRITE] end.", frame_dump__data_buffer_name);
	} else {
		print(NS::PrintMessageType::VERBOSE, "[READ] end.", frame_dump__data_buffer_name);
	}

	frame_dump_data_buffer_dump_mode = DataBufferDumpMode::NONE;
	frame_dump__data_buffer_name = "";
#endif
}

void SceneSynchronizerDebugger::databuffer_write(uint32_t p_data_type, uint32_t p_compression_level, int p_new_bit_offset, const char *val_string) {
#ifdef DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	if (frame_dump_data_buffer_dump_mode != DataBufferDumpMode::WRITE) {
		return;
	}

	frame_dump__data_buffer_writes.push_back(val_string);

	const std::string operation = "[WRITE]      [" + compression_level_to_string(p_compression_level) + "] [" + data_type_to_string(p_data_type) + "] [new offset: " + std::to_string(p_new_bit_offset) + "] " + val_string;

	print(NS::PrintMessageType::VERBOSE, operation, frame_dump__data_buffer_name);
#endif
}

void SceneSynchronizerDebugger::databuffer_read(uint32_t p_data_type, uint32_t p_compression_level, int p_new_bit_offset, const char *val_string) {
#ifdef DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	if (frame_dump_data_buffer_dump_mode != DataBufferDumpMode::READ) {
		return;
	}

	frame_dump__data_buffer_reads.push_back(val_string);

	const std::string operation = "[READ]     [" + compression_level_to_string(p_compression_level) + "] [" + data_type_to_string(p_data_type) + "] [new offset: " + std::to_string(p_new_bit_offset) + "] " + val_string;

	print(NS::PrintMessageType::VERBOSE, operation, frame_dump__data_buffer_name);
#endif
}

void SceneSynchronizerDebugger::notify_input_sent_to_server(int p_peer, uint32_t p_frame_index, uint32_t p_input_index) {
#ifdef DEBUG_ENABLED
	print(NS::INFO, "The client sent to server the input `" + std::to_string(p_input_index) + "` for frame:`" + std::to_string(p_frame_index) + "`.", "CONTROLLER-" + std::to_string(p_peer));
#endif
}

void SceneSynchronizerDebugger::notify_are_inputs_different_result(
		int p_peer,
		uint32_t p_other_frame_index,
		bool p_is_similar) {
#ifdef DEBUG_ENABLED
	if (p_is_similar) {
		print(NS::INFO, "This frame input is SIMILAR to `" + std::to_string(p_other_frame_index) + "`", "CONTROLLER-" + std::to_string(p_peer));
	} else {
		print(NS::INFO, "This frame input is DIFFERENT to `" + std::to_string(p_other_frame_index) + "`", "CONTROLLER-" + std::to_string(p_peer));
	}
	frame_dump__are_inputs_different_results[std::to_string(p_other_frame_index)] = p_is_similar;
#endif
}

void SceneSynchronizerDebugger::print(NS::PrintMessageType p_level, const std::string &p_message, const std::string &p_object_name, bool p_force_print_to_log) {
#ifdef DEBUG_ENABLED

	if (NS::PrintMessageType::WARNING & p_level) {
		frame_dump__has_warnings = true;
	}

	if (NS::PrintMessageType::ERROR & p_level) {
		frame_dump__has_errors = true;
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
#ifdef DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	frame_dump__frame_events = FrameEvent(frame_dump__frame_events | p_event);
#endif
}

void SceneSynchronizerDebugger::__add_message(const std::string &p_message, const std::string &p_object_name) {
#ifdef DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	nlohmann::json m;
	m["i"] = log_counter;
	m["m"] = p_message;
	frame_dump__node_log[p_object_name].push_back(m);

	log_counter += 1;
#endif
}

void SceneSynchronizerDebugger::dump_tracked_objects(const NS::SceneSynchronizerBase *p_scene_sync, nlohmann::json::object_t &p_dump) {
#ifdef DEBUG_ENABLED
	p_dump.clear();

	/*
	for (uint32_t i = 0; i < tracked_nodes.size(); i += 1) {
		nlohmann::json object_dump;

		std::string node_path = String(tracked_nodes[i].node->get_path()).utf8().ptr();
		object_dump["node_path"] = node_path;

		for (List<PropertyInfo>::Element *e = tracked_nodes[i].properties->front(); e; e = e->next()) {
			std::string prefix;
			// TODO the below cast is an unsafe cast. Please refactor this.
			if (p_scene_sync->is_variable_registered(p_scene_sync->find_object_local_id({ reinterpret_cast<std::intptr_t>(tracked_nodes[i].node) }), std::string(e->get().name.utf8()))) {
				prefix = "* ";
			}

			object_dump[prefix + String(e->get().name).utf8().ptr() + "::" + type_to_string(e->get().type)] = tracked_nodes[i].node->get(e->get().name).stringify().utf8().ptr();
		}

		p_dump[node_path] = object_dump;
	}
	*/
#endif
}
