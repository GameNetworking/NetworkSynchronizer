#include "print.h"
#include "modules/network_synchronizer/scene_synchronizer.h"

NS_NAMESPACE_BEGIN

void print_line(const std::string &p_str) {
	SceneSynchronizerBase::print_line(p_str);
}

NS_NAMESPACE_END

void _ns_print_code_message(
		const char *p_function,
		const char *p_file,
		int p_line,
		const std::string &p_error,
		const std::string &p_message,
		NS::PrintMessageType p_type) {
	NS::SceneSynchronizerBase::print_code_message(
			p_function,
			p_file,
			p_line,
			p_error,
			p_message,
			p_type);
}