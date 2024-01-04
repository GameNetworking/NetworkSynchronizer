#pragma once

#include "scene/main/node.h"

#ifdef DEBUG_ENABLED

#include "core/core.h"
#include "core/json.hpp"
#include "core/templates/oa_hash_map.h"
#include <string>

#endif

namespace NS {
class SceneSynchronizerBase;
};
class SceneTree;

namespace NS {
class NetworkInterface;
};

class SceneSynchronizerDebugger : public Node {
	GDCLASS(SceneSynchronizerDebugger, Node);

	static SceneSynchronizerDebugger *the_singleton;

public:
	struct TrackedNode {
		Node *node = nullptr;
		List<PropertyInfo> *properties = nullptr;
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
		CLIENT_DESYNC_DETECTED_SOFT = 1 << 1,
	};

public:
	static SceneSynchronizerDebugger *singleton();
	static void _bind_methods();

private:
	NS::PrintMessageType log_level = NS::PrintMessageType::ERROR;

#ifdef DEBUG_ENABLED
	bool dump_enabled = false;
	LocalVector<StringName> dump_classes;
	bool setup_done = false;

	uint32_t log_counter = 0;
	SceneTree *scene_tree = nullptr;
	std::string main_dump_directory_path;
	std::string dump_name;

	std::vector<TrackedNode> tracked_nodes;

	// HashMap between class name and property list: to avoid fetching the property list per object each frame.
	OAHashMap<StringName, List<PropertyInfo>> classes_property_lists;

	// JSON of dictionary containing nodes info.
	nlohmann::json::object_t frame_dump__begin_state;

	// JSON of dictionary containing nodes info.
	nlohmann::json::object_t frame_dump__end_state;

	// The JSON containing the data buffer operations performed by the controllers.
	nlohmann::json::object_t frame_dump__node_log;

	// The controller name for which the data buffer operations is in progress.
	std::string frame_dump__data_buffer_name;

	// A really small description about what happens on this frame.
	FrameEvent frame_dump__frame_events = FrameEvent::EMPTY;

	// This Array contains all the inputs (stringified) written on the `DataBuffer` from the
	// `_controller_process` function
	nlohmann::json::array_t frame_dump__data_buffer_writes;

	// This Array contains all the inputs (stringified) read on the `DataBuffer` from the
	// `_controller_process` function
	nlohmann::json::array_t frame_dump__data_buffer_reads;

	// This JSON contains the comparison (`_are_inputs_different`) fetched by this frame, and
	// the result.
	nlohmann::json::object_t frame_dump__are_inputs_different_results;

	DataBufferDumpMode frame_dump_data_buffer_dump_mode = NONE;

	bool frame_dump__has_warnings = false;
	bool frame_dump__has_errors = false;
#endif

public:
	SceneSynchronizerDebugger();
	~SceneSynchronizerDebugger();

	void set_log_level(NS::PrintMessageType p_log_level);
	NS::PrintMessageType get_log_level() const;

	void set_dump_enabled(bool p_dump_enabled);
	bool get_dump_enabled() const;

	void register_class_for_node_to_dump(Node *p_node);
	void register_class_to_dump(const StringName &p_class);
	void unregister_class_to_dump(const StringName &p_class);

	void setup_debugger(const std::string &p_dump_name, int p_peer, SceneTree *p_scene_tree);

private:
	void prepare_dumping(int p_peer, SceneTree *p_scene_tree);
	void setup_debugger_python_ui();
	void track_node(Node *p_node, bool p_recursive);
	void on_node_added(Node *p_node);
	void on_node_removed(Node *p_node);

public:
	void write_dump(int p_peer, uint32_t p_frame_index);
	void start_new_frame();

	void scene_sync_process_start(const NS::SceneSynchronizerBase *p_scene_sync);
	void scene_sync_process_end(const NS::SceneSynchronizerBase *p_scene_sync);

	void databuffer_operation_begin_record(int p_peer, DataBufferDumpMode p_mode);
	void databuffer_operation_end_record();
	void databuffer_write(uint32_t p_data_type, uint32_t p_compression_level, int p_new_bit_offset, const char *p_variable);
	void databuffer_read(uint32_t p_data_type, uint32_t p_compression_level, int p_new_bit_offset, const char *p_variable);

	void notify_input_sent_to_server(int p_peer, uint32_t p_frame_index, uint32_t p_input_index);
	void notify_are_inputs_different_result(int p_peer, uint32_t p_other_frame_index, bool p_is_similar);

	void debug_print(NS::NetworkInterface *p_network_interface, const String &p_message, bool p_silent = false);
	void debug_warning(NS::NetworkInterface *p_network_interface, const String &p_message, bool p_silent = false);

	void print(
			NS::PrintMessageType p_level,
			const std::string &p_message,
			const std::string &p_object_name = "GLOBAL",
			bool p_force_print_to_log = false);

	void notify_event(FrameEvent p_event);

	void __add_message(const std::string &p_message, const std::string &p_object_name);

private:
	void dump_tracked_objects(const NS::SceneSynchronizerBase *p_scene_sync, nlohmann::json::object_t &p_dump);
};
