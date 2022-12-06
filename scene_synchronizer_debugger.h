/*************************************************************************/
/*  scene_synchronizer_debugger.h                                        */
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

#pragma once

#include "scene/main/node.h"

#ifdef DEBUG_ENABLED

#include "core/templates/oa_hash_map.h"

#endif

class SceneSynchronizer;
class SceneTree;

class SceneSynchronizerDebugger : public Node {
	GDCLASS(SceneSynchronizerDebugger, Node);

	static SceneSynchronizerDebugger *the_singleton;

public:
	struct TrackedNode {
		Node *node;
		List<PropertyInfo> *properties;
		TrackedNode() = default;
		TrackedNode(Node *p_node) :
				node(p_node) {}
		TrackedNode(Node *p_node, List<PropertyInfo> *p_properties) :
				node(p_node), properties(p_properties) {}
		bool operator==(const TrackedNode &p_t) const { return p_t.node == node; }
	};

	enum DataBufferDumpMode {
		NONE,
		WRITE,
		READ
	};

	enum FrameEvent : uint32_t {
		EMPTY = 0,
		CLIENT_DESYNC_DETECTED = 1 << 0,
		CLIENT_DESYNC_DETECTED_SOFT = 1 << 0,
	};

public:
	static SceneSynchronizerDebugger *singleton();
	static void _bind_methods();

private:
#ifdef DEBUG_ENABLED
	bool dump_enabled = false;
	LocalVector<StringName> dump_classes;
	bool setup_done = false;

	SceneTree *scene_tree = nullptr;
	String main_dump_directory_path;
	String dump_name;

	LocalVector<TrackedNode> tracked_nodes;
	// HaskMap between class name and property list: to avoid fetching the property list per object each frame.
	OAHashMap<StringName, List<PropertyInfo>> classes_property_lists;

	// Dictionary of dictionary containing nodes info.
	Dictionary frame_dump__begin_state;

	// Dictionary of dictionary containing nodes info.
	Dictionary frame_dump__end_state;

	// The dictionary containing the data buffer operations performed by the controllers.
	Dictionary frame_dump__node_log;

	// The controller name for which the data buffer operations is in progress.
	String frame_dump__data_buffer_name;

	// A really small description about what happens on this frame.
	FrameEvent frame_dump__frame_events = FrameEvent::EMPTY;

	// This Array contains all the inputs (stringified) written on the `DataBuffer` from the
	// `_controller_process` function
	Array frame_dump__data_buffer_writes;

	// This Array contains all the inputs (stringified) read on the `DataBuffer` from the
	// `_controller_process` function
	Array frame_dump__data_buffer_reads;

	// This Dictionary contains the comparison (`_are_inputs_different`) fetched by this frame, and
	// the result.
	Dictionary frame_dump__are_inputs_different_results;

	DataBufferDumpMode frame_dump_data_buffer_dump_mode = NONE;

	bool frame_dump__has_warnings = false;
	bool frame_dump__has_errors = false;
#endif

public:
	SceneSynchronizerDebugger();
	~SceneSynchronizerDebugger();

	void set_dump_enabled(bool p_dump_enabled);
	bool get_dump_enabled() const;

	void register_class_for_node_to_dump(Node *p_node);
	void register_class_to_dump(const StringName &p_class);
	void unregister_class_to_dump(const StringName &p_class);

	void setup_debugger(const String &p_dump_name, int p_peer, SceneTree *p_scene_tree);

private:
	void prepare_dumping(int p_peer, SceneTree *p_scene_tree);
	void setup_debugger_python_ui();
	void track_node(Node *p_node, bool p_recursive);
	void on_node_added(Node *p_node);
	void on_node_removed(Node *p_node);

public:
	void write_dump(int p_peer, uint32_t p_frame_index);
	void start_new_frame();

	void scene_sync_process_start(const SceneSynchronizer *p_scene_sync);
	void scene_sync_process_end(const SceneSynchronizer *p_scene_sync);

	void databuffer_operation_begin_record(Node *p_node, DataBufferDumpMode p_mode);
	void databuffer_operation_end_record();
	void databuffer_write(uint32_t p_data_type, uint32_t p_compression_level, Variant p_variable);
	void databuffer_read(uint32_t p_data_type, uint32_t p_compression_level, Variant p_variable);

	void notify_input_sent_to_server(Node *p_node, uint32_t p_frame_index, uint32_t p_input_index);
	void notify_are_inputs_different_result(Node *p_node, uint32_t p_other_frame_index, bool p_is_similar);

	void add_node_message(Node *p_node, const String &p_message);
	void add_node_message_by_path(const String &p_node_path, const String &p_message);

	void debug_print(Node *p_node, const String &p_message, bool p_silent = false);
	void debug_warning(Node *p_node, const String &p_message, bool p_silent = false);
	void debug_error(Node *p_node, const String &p_message, bool p_silent = false);

	void notify_event(FrameEvent p_event);

private:
	void dump_tracked_objects(const SceneSynchronizer *p_scene_sync, Dictionary &p_dump);
};
