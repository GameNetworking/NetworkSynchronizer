#pragma once

#include "core.h"

NS_NAMESPACE_BEGIN
class SceneSynchronizerBase;
class NetworkInterface;

class FileSystem {
public:
	virtual std::string get_base_dir() const = 0;
	virtual std::string get_date() const = 0;
	virtual std::string get_time() const = 0;
	virtual bool make_dir_recursive(const std::string &p_dir_path, bool p_erase_content) const = 0;
	virtual bool store_file_string(const std::string &p_path, const std::string &p_string_file) const = 0;
	virtual bool store_file_buffer(const std::string &p_path, const std::uint8_t *p_src, uint64_t p_length) const = 0;
	virtual bool file_exists(const std::string &p_path) const = 0;
};
NS_NAMESPACE_END

class SceneSynchronizerDebugger {
	static SceneSynchronizerDebugger *the_singleton;

public:
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

private:
	NS::PrintMessageType log_level = NS::PrintMessageType::ERROR;

#ifdef NS_DEBUG_ENABLED
	bool dump_enabled = false;
	bool setup_done = false;

	NS::FileSystem *file_system = nullptr;

	uint32_t log_counter = 0;
	std::string main_dump_directory_path;
	std::string dump_name;

	// NOTICE: This is created at runtime when the object spawns.
	//         The reason for this mechanism is to keep the Json header
	//         inside the CPP and avoid clutter the includes, since that header includes many non wanted includes.
	struct SceneSynchronizerDebuggerJsonStorage *frame_dump_storage = nullptr;

	// A really small description about what happens on this frame.
	FrameEvent frame_dump__frame_events = FrameEvent::EMPTY;

	DataBufferDumpMode frame_dump_data_buffer_dump_mode = NONE;
#endif

public:
	SceneSynchronizerDebugger();
	~SceneSynchronizerDebugger();

	void set_file_system(NS::FileSystem *p_file_system);
	NS::FileSystem *get_file_system() const {
#ifdef NS_DEBUG_ENABLED
		return file_system;
#else
		return nullptr;
#endif
	}

	void set_log_level(NS::PrintMessageType p_log_level);
	NS::PrintMessageType get_log_level() const;

	void set_dump_enabled(bool p_dump_enabled);
	bool get_dump_enabled() const;

	void setup_debugger(const std::string &p_dump_name, int p_peer);

private:
	void prepare_dumping(int p_peer);
	void setup_debugger_python_ui();

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

	void print(
			NS::PrintMessageType p_level,
			const std::string &p_message,
			const std::string &p_object_name = "GLOBAL",
			bool p_force_print_to_log = false);

	void notify_event(FrameEvent p_event);

	void __add_message(const std::string &p_message, const std::string &p_object_name);
};
