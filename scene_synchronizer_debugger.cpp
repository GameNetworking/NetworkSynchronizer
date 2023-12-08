/*************************************************************************/
/*  scene_synchronizer_debugger.cpp                                      */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2021 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2021 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "scene_synchronizer_debugger.h"
#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/scene_synchronizer_debugger.h"

#ifdef DEBUG_ENABLED

#include "__generated__debugger_ui.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/network_interface.h"
#include "core/os/os.h"
#include "data_buffer.h"
#include "net_utilities.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "scene_synchronizer.h"
#include <string>

#endif

SceneSynchronizerDebugger *SceneSynchronizerDebugger::the_singleton = nullptr;

SceneSynchronizerDebugger *SceneSynchronizerDebugger::singleton() {
	return the_singleton;
}

void SceneSynchronizerDebugger::_bind_methods() {
	ClassDB::bind_method(D_METHOD("on_node_added"), &SceneSynchronizerDebugger::on_node_added);
	ClassDB::bind_method(D_METHOD("on_node_removed"), &SceneSynchronizerDebugger::on_node_removed);
}

SceneSynchronizerDebugger::SceneSynchronizerDebugger() :
		Node() {
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
	tracked_nodes.clear();
	classes_property_lists.clear();
	frame_dump__begin_state.clear();
	frame_dump__end_state.clear();
	frame_dump__node_log.clear();
	frame_dump__data_buffer_writes.clear();
	frame_dump__data_buffer_reads.clear();
	frame_dump__are_inputs_different_results.clear();
#endif
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

void SceneSynchronizerDebugger::register_class_for_node_to_dump(Node *p_node) {
#ifdef DEBUG_ENABLED
	register_class_to_dump(p_node->get_class_name());
	track_node(p_node, false);
#endif
}

void SceneSynchronizerDebugger::register_class_to_dump(const StringName &p_class) {
#ifdef DEBUG_ENABLED
	ERR_FAIL_COND(p_class == StringName());

	if (dump_classes.find(p_class) == -1) {
		dump_classes.push_back(p_class);
	}
#endif
}

void SceneSynchronizerDebugger::unregister_class_to_dump(const StringName &p_class) {
#ifdef DEBUG_ENABLED
	const int64_t index = dump_classes.find(p_class);
	if (index >= 0) {
		dump_classes.remove_at_unordered(index);
	}
#endif
}

void SceneSynchronizerDebugger::setup_debugger(const std::string &p_dump_name, int p_peer, SceneTree *p_scene_tree) {
#ifdef DEBUG_ENABLED
	if (setup_done == false) {
		setup_done = true;

		// Setup `dump_enabled`
		if (!dump_enabled) {
			dump_enabled = GLOBAL_GET("NetworkSynchronizer/debugger/dump_enabled");
		}

		// Setup `dump_classes`.
		{
			Array classes = GLOBAL_GET("NetworkSynchronizer/debugger/dump_classes");
			for (uint32_t i = 0; i < uint32_t(classes.size()); i += 1) {
				if (classes[i].get_type() == Variant::STRING) {
					register_class_to_dump(classes[i]);
				}
			}
		}
	}

	// Setup directories.
	main_dump_directory_path = (OS::get_singleton()->get_executable_path().get_base_dir() + "/net-sync-debugs/dump").utf8().ptr();
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

	// Prepare the dir.
	{
		std::string path = main_dump_directory_path + "/" + dump_name;

		Ref<DirAccess> dir = DirAccess::create_for_path(path.c_str());

		Error e;
		e = dir->make_dir_recursive(path.c_str());

		ERR_FAIL_COND(e != OK);

		e = dir->change_dir(path.c_str());

		ERR_FAIL_COND(e != OK);

		// Empty the directory making sure we are ready to write.
		dir->erase_contents_recursive();
		//if (dir->list_dir_begin() == OK) {
		//	for (String name = dir->get_next(); name != String(); name = dir->get_next()) {
		//		if (name.begins_with("fd-" + itos(p_peer) + "-")) {
		//			dir->remove(name);
		//		}
		//	}
		//	dir->list_dir_end();
		//}
	}

	// Store generic info about this dump.
	{
		Error e;
		Ref<FileAccess> file = FileAccess::open(String((main_dump_directory_path + "/" + "dump-info-" + dump_name + /*"-" + itos(p_peer) +*/ ".json").c_str()), FileAccess::WRITE, &e);

		ERR_FAIL_COND(e != OK);

		OS::DateTime date = OS::get_singleton()->get_datetime();

		nlohmann::json d;
		d["dump-name"] = dump_name;
		d["peer"] = p_peer;
		d["date"] = std::to_string(date.day) + "/" + std::to_string(date.month) + "/" + std::to_string(date.year);
		d["time"] = std::to_string(date.hour) + "::" + std::to_string(date.minute);

		file->flush();
		file->store_string(d.dump().c_str());
	}

	if (scene_tree) {
		scene_tree->disconnect(SNAME("node_added"), Callable(this, SNAME("on_node_added")));
		scene_tree->disconnect(SNAME("node_removed"), Callable(this, SNAME("on_node_removed")));
	}

	tracked_nodes.clear();
	classes_property_lists.clear();
	scene_tree = p_scene_tree;

	if (scene_tree) {
		scene_tree->connect(SNAME("node_added"), Callable(this, SNAME("on_node_added")));
		scene_tree->connect(SNAME("node_removed"), Callable(this, SNAME("on_node_removed")));

		// Start by tracking the existing node.
		track_node(scene_tree->get_root(), true);
	}
#endif
}

void SceneSynchronizerDebugger::setup_debugger_python_ui() {
#ifdef DEBUG_ENABLED
	// Verify if file exists.
	const std::string path = main_dump_directory_path + "/debugger.py";

	if (FileAccess::exists(path.c_str())) {
		// Nothing to do.
		return;
	}

	// Copy the python UI into the directory.
	Ref<FileAccess> f = FileAccess::open(path.c_str(), FileAccess::WRITE);
	ENSURE_MSG(!f.is_null(), "Can't create the `" + path + "` file.");

	f->store_buffer((uint8_t *)__debugger_ui_code, __debugger_ui_code_size);
#endif
}

void SceneSynchronizerDebugger::track_node(Node *p_node, bool p_recursive) {
#ifdef DEBUG_ENABLED
	if (!NS::VecFunc::has(tracked_nodes, p_node)) {
		const bool is_tracked = dump_classes.find(p_node->get_class_name()) != -1;
		if (is_tracked) {
			// Verify if the property list already exists.
			if (!classes_property_lists.has(p_node->get_class_name())) {
				// Property list not yet cached, fetch it now.
				classes_property_lists.insert(p_node->get_class_name(), List<PropertyInfo>());
				List<PropertyInfo> *properties = classes_property_lists.lookup_ptr(p_node->get_class_name());

				p_node->get_property_list(properties);
			}

			List<PropertyInfo> *properties = classes_property_lists.lookup_ptr(p_node->get_class_name());

			// Can't happen, as it was just created.
			CRASH_COND(properties == nullptr);

			// Assign the property list pointer for fast access.
			tracked_nodes.push_back(TrackedNode(p_node, properties));
		}
	}

	if (p_recursive) {
		for (int i = 0; i < p_node->get_child_count(); i += 1) {
			Node *child = p_node->get_child(i);
			track_node(child, true);
		}
	}
#endif
}

void SceneSynchronizerDebugger::on_node_added(Node *p_node) {
#ifdef DEBUG_ENABLED
	track_node(p_node, false);
#endif
}

void SceneSynchronizerDebugger::on_node_removed(Node *p_node) {
#ifdef DEBUG_ENABLED
	NS::VecFunc::remove_unordered(tracked_nodes, p_node);
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

	Ref<FileAccess> file = nullptr;
	{
		std::string file_path = "";

		int iteration = 0;
		std::string iteration_mark = "";
		do {
			file_path = main_dump_directory_path + "/" + dump_name + "/fd-" /*+ itos(p_peer) + "-"*/ + std::to_string(p_frame_index) + iteration_mark + ".json";
			iteration_mark += "@";
			iteration += 1;
		} while (FileAccess::exists(file_path.c_str()) && iteration < 100);

		Error e;
		file = FileAccess::open(file_path.c_str(), FileAccess::WRITE, &e);
		ENSURE(e == OK);
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

	file->store_string(String(d.dump().c_str()));
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
		case Variant::VARIANT_MAX:
			return "VARIANT_MAX";
	}
	return "";
}

std::string data_type_to_string(uint32_t p_type) {
	switch (p_type) {
		case DataBuffer::DATA_TYPE_BOOL:
			return "Bool";
		case DataBuffer::DATA_TYPE_INT:
			return "Int";
		case DataBuffer::DATA_TYPE_UINT:
			return "Uint";
		case DataBuffer::DATA_TYPE_REAL:
			return "Real";
		case DataBuffer::DATA_TYPE_POSITIVE_UNIT_REAL:
			return "Positive Unit Real";
		case DataBuffer::DATA_TYPE_UNIT_REAL:
			return "Unit Real";
		case DataBuffer::DATA_TYPE_VECTOR2:
			return "Vector2";
		case DataBuffer::DATA_TYPE_NORMALIZED_VECTOR2:
			return "Normalized Vector2";
		case DataBuffer::DATA_TYPE_VECTOR3:
			return "Vector3";
		case DataBuffer::DATA_TYPE_NORMALIZED_VECTOR3:
			return "Normalized Vector3";
		case DataBuffer::DATA_TYPE_VARIANT:
			return "Variant";
	}

	return "UNDEFINED";
}

std::string compression_level_to_string(uint32_t p_type) {
	switch (p_type) {
		case DataBuffer::COMPRESSION_LEVEL_0:
			return "Compression Level 0";
		case DataBuffer::COMPRESSION_LEVEL_1:
			return "Compression Level 1";
		case DataBuffer::COMPRESSION_LEVEL_2:
			return "Compression Level 2";
		case DataBuffer::COMPRESSION_LEVEL_3:
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

void SceneSynchronizerDebugger::databuffer_operation_begin_record(NS::NetworkInterface *p_network_interface, DataBufferDumpMode p_mode) {
#ifdef DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	frame_dump__data_buffer_name = p_network_interface->get_name().utf8().ptr();
	frame_dump_data_buffer_dump_mode = p_mode;

	if (frame_dump_data_buffer_dump_mode == DataBufferDumpMode::WRITE) {
		print("[WRITE] DataBuffer start write.", frame_dump__data_buffer_name, NS::PrintMessageType::__INTERNAL);
	} else {
		print("[READ] DataBuffer start read.", frame_dump__data_buffer_name, NS::PrintMessageType::__INTERNAL);
	}
#endif
}

void SceneSynchronizerDebugger::databuffer_operation_end_record() {
#ifdef DEBUG_ENABLED
	if (!dump_enabled) {
		return;
	}

	if (frame_dump_data_buffer_dump_mode == DataBufferDumpMode::WRITE) {
		print("[WRITE] end.", frame_dump__data_buffer_name, NS::PrintMessageType::__INTERNAL);
	} else {
		print("[READ] end.", frame_dump__data_buffer_name, NS::PrintMessageType::__INTERNAL);
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

	print(operation, frame_dump__data_buffer_name, NS::PrintMessageType::__INTERNAL);
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

	print(operation, frame_dump__data_buffer_name, NS::PrintMessageType::__INTERNAL);
#endif
}

void SceneSynchronizerDebugger::notify_input_sent_to_server(NS::NetworkInterface *p_network_interface, uint32_t p_frame_index, uint32_t p_input_index) {
#ifdef DEBUG_ENABLED
	debug_print(p_network_interface, "The client sent to server the input `" + itos(p_input_index) + "` for frame:`" + itos(p_frame_index) + "`.", true);
#endif
}

void SceneSynchronizerDebugger::notify_are_inputs_different_result(
		NS::NetworkInterface *p_network_interface,
		uint32_t p_other_frame_index,
		bool p_is_similar) {
#ifdef DEBUG_ENABLED
	if (p_is_similar) {
		debug_print(p_network_interface, "This frame input is SIMILAR to `" + itos(p_other_frame_index) + "`", true);
	} else {
		debug_print(p_network_interface, "This frame input is DIFFERENT to `" + itos(p_other_frame_index) + "`", true);
	}
	frame_dump__are_inputs_different_results[std::to_string(p_other_frame_index)] = p_is_similar;
#endif
}

void SceneSynchronizerDebugger::debug_print(NS::NetworkInterface *p_network_interface, const String &p_message, bool p_silent) {
#ifdef DEBUG_ENABLED
	print(
			p_message.utf8().ptr(),
			p_network_interface ? p_network_interface->get_name().utf8().ptr() : "GLOBAL",
			NS::PrintMessageType::INFO);
#endif
}

void SceneSynchronizerDebugger::debug_warning(NS::NetworkInterface *p_network_interface, const String &p_message, bool p_silent) {
#ifdef DEBUG_ENABLED
	print(
			p_message.utf8().ptr(),
			p_network_interface ? p_network_interface->get_name().utf8().ptr() : "GLOBAL",
			NS::PrintMessageType::WARNING);
#endif
}

void SceneSynchronizerDebugger::debug_error(NS::NetworkInterface *p_network_interface, const String &p_message, bool p_silent) {
#ifdef DEBUG_ENABLED
	print(
			p_message.utf8().ptr(),
			p_network_interface ? p_network_interface->get_name().utf8().ptr() : "GLOBAL",
			NS::PrintMessageType::ERROR);
#endif
}

void SceneSynchronizerDebugger::print(const std::string &p_message, const std::string &p_object_name, NS::PrintMessageType p_level, bool p_force_print_to_log) {
#ifdef DEBUG_ENABLED

	if (NS::PrintMessageType::WARNING & p_level) {
		frame_dump__has_warnings = true;
	}

	if (NS::PrintMessageType::ERROR & p_level) {
		frame_dump__has_errors = true;
	}

	const std::string log_level_str = NS::get_log_level_txt(p_level);

	if ((log_level & p_level) || p_force_print_to_log) {
		NS::SceneSynchronizerBase::__print_line(log_level_str + "[" + p_object_name + "] " + p_message);
	}

	__add_message(log_level_str + p_message, p_object_name);
#else
	if ((log_level & p_level) || p_force_print_to_log) {
		const std::string log_level_str = NS::get_log_level_txt(p_level);
		NS::SceneSynchronizerBase::print_line(log_level_str + "[" + p_object_name + "] " + p_message);
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

	for (uint32_t i = 0; i < tracked_nodes.size(); i += 1) {
		nlohmann::json object_dump;

		std::string node_path = String(tracked_nodes[i].node->get_path()).utf8().ptr();
		object_dump["node_path"] = node_path;

		for (List<PropertyInfo>::Element *e = tracked_nodes[i].properties->front(); e; e = e->next()) {
			std::string prefix;
			// TODO the below cast is an unsafe cast. Please refactor this.
			if (p_scene_sync->is_variable_registered(p_scene_sync->find_object_local_id({ reinterpret_cast<std::intptr_t>(tracked_nodes[i].node) }), e->get().name)) {
				prefix = "* ";
			}

			object_dump[prefix + String(e->get().name).utf8().ptr() + "::" + type_to_string(e->get().type)] = tracked_nodes[i].node->get(e->get().name).stringify().utf8().ptr();
		}

		p_dump[node_path] = object_dump;
	}
#endif
}
