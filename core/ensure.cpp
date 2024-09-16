#include "ensure.h"

#include "../scene_synchronizer.h"

void _ns_print_code_message(
		NS::SceneSynchronizerDebugger &p_debugger,
		const char *p_function,
		const char *p_file,
		int p_line,
		const std::string &p_error,
		const std::string &p_message,
		NS::PrintMessageType p_type) {
	NS::SceneSynchronizerBase::print_code_message(
			&p_debugger,
			p_function,
			p_file,
			p_line,
			p_error,
			p_message,
			p_type);
}

void _ns_print_code_message(
		const char *p_function,
		const char *p_file,
		int p_line,
		const std::string &p_error,
		const std::string &p_message,
		NS::PrintMessageType p_type) {
	NS::SceneSynchronizerBase::print_code_message(
			nullptr,
			p_function,
			p_file,
			p_line,
			p_error,
			p_message,
			p_type);
}

void _ns_print_flush_stdout() {
	NS::SceneSynchronizerBase::print_flush_stdout();
}