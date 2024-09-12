#pragma once

#include "core.h"
#include <string>

#ifdef __GNUC__
//#define FUNCTION_STR __PRETTY_FUNCTION__ - too annoying
#define FUNCTION_STR __FUNCTION__
#else
#define FUNCTION_STR __FUNCTION__
#endif

#ifdef _MSC_VER
/**
 * Don't use GENERATE_TRAP() directly, should only be used be the macros below.
 */
#define GENERATE_TRAP() __debugbreak()
#else
/**
 * Don't use GENERATE_TRAP() directly, should only be used be the macros below.
 */
#define GENERATE_TRAP() __builtin_trap()
#endif

// Turn argument to string constant:
// https://gcc.gnu.org/onlinedocs/cpp/Stringizing.html#Stringizing
#ifndef _STR
#define _STR(m_x) #m_x
#define _MKSTR(m_x) _STR(m_x)
#endif

namespace NS {
class SceneSynchronizerDebugger;
};

void _ns_print_code_message(
		NS::SceneSynchronizerDebugger &p_debugger,
		const char *p_function,
		const char *p_file,
		int p_line,
		const std::string &p_error,
		const std::string &p_message,
		NS::PrintMessageType p_type);

void _ns_print_code_message(
		const char *p_function,
		const char *p_file,
		int p_line,
		const std::string &p_error,
		const std::string &p_message,
		NS::PrintMessageType p_type);

void _ns_print_flush_stdout();

/// Ensures `m_cond` is true.
/// If `m_cond` is false the current function returns.
#define NS_ENSURE(m_cond)                                                                                                                         \
	if make_likely (m_cond) {                                                                                                                  \
	} else {                                                                                                                                   \
		_ns_print_code_message(get_debugger(), FUNCTION_STR, __FILE__, __LINE__, "Condition \"" _STR(m_cond) "\" is false.", "", NS::PrintMessageType::ERROR); \
		return;                                                                                                                                \
	}

/// Ensures `m_cond` is true.
/// If `m_cond` is false, prints `m_msg` and the current function returns.
#define NS_ENSURE_MSG(m_cond, m_msg)                                                                                                                                            \
	if make_likely (m_cond) {                                                                                                                                                \
	} else {                                                                                                                                                                 \
		_ns_print_code_message(get_debugger(), FUNCTION_STR, __FILE__, __LINE__, "Condition \"" _STR(m_cond) "\" is false. Returning: " _STR(m_retval), m_msg, NS::PrintMessageType::ERROR); \
		return;                                                                                                                                                              \
	}

/// Ensures `m_cond` is true.
/// If `m_cond` is false, the current function returns `m_retval`.
#define NS_ENSURE_V(m_cond, m_retval)                                                                                                                                        \
	if make_likely (m_cond) {                                                                                                                                             \
	} else {                                                                                                                                                              \
		_ns_print_code_message(get_debugger(), FUNCTION_STR, __FILE__, __LINE__, "Condition \"" _STR(m_cond) "\" is false. Returning: " _STR(m_retval), "", NS::PrintMessageType::ERROR); \
		return m_retval;                                                                                                                                                  \
	}

/// Ensures `m_cond` is true.
/// If `m_cond` is false, prints `m_msg` and the current function returns `m_retval`.
#define NS_ENSURE_V_MSG(m_cond, m_retval, m_msg)                                                                                                                                \
	if make_likely (m_cond) {                                                                                                                                                \
	} else {                                                                                                                                                                 \
		_ns_print_code_message(get_debugger(), FUNCTION_STR, __FILE__, __LINE__, "Condition \"" _STR(m_cond) "\" is false. Returning: " _STR(m_retval), m_msg, NS::PrintMessageType::ERROR); \
		return m_retval;                                                                                                                                                     \
	}

/// Ensures no entry
#define NS_ENSURE_NO_ENTRY()                                                                                         \
	_ns_print_code_message(get_debugger(), FUNCTION_STR, __FILE__, __LINE__, "No entry triggered", "", NS::PrintMessageType::ERROR); \
	return;

/// Ensures no entry with message
#define NS_ENSURE_NO_ENTRY_MSG(m_msg)                                                                                                     \
	_ns_print_code_message(get_debugger(), FUNCTION_STR, __FILE__, __LINE__, "No entry. Returning: " _STR(m_retval), m_msg, NS::PrintMessageType::ERROR); \
	return;

/// Ensures no entry with return value.
#define NS_ENSURE_NO_ENTRY_V(m_retval)                                                                                                 \
	_ns_print_code_message(get_debugger(), FUNCTION_STR, __FILE__, __LINE__, "No entry. Returning: " _STR(m_retval), "", NS::PrintMessageType::ERROR); \
	return m_retval;

/// Ensures no entry with return value and with message.
#define NS_ENSURE_NO_ENTRY_V_MSG(m_retval, m_msg)                                                                                        \
	_ns_print_code_message(get_debugger(), FUNCTION_STR, __FILE__, __LINE__, "No entry. Returning: " _STR(m_retval), m_msg, NS::PrintMessageType::ERROR); \
	return m_retval;

/// Ensures `m_cond` is true.
/// If `m_cond` is false the current function returns.
#define NS_ENSURE_CONTINUE(m_cond)                                                                                                                \
	if make_likely (m_cond) {                                                                                                                  \
	} else {                                                                                                                                   \
		_ns_print_code_message(get_debugger(), FUNCTION_STR, __FILE__, __LINE__, "Condition \"" _STR(m_cond) "\" is false.", "", NS::PrintMessageType::ERROR); \
		continue;                                                                                                                              \
	}

/// Ensures `m_cond` is true.
/// If `m_cond` is false the current function returns.
#define NS_ENSURE_CONTINUE_MSG(m_cond, m_msg)                                                                                                        \
	if make_likely (m_cond) {                                                                                                                     \
	} else {                                                                                                                                      \
		_ns_print_code_message(get_debugger(), FUNCTION_STR, __FILE__, __LINE__, "Condition \"" _STR(m_cond) "\" is false.", m_msg, NS::PrintMessageType::ERROR); \
		continue;                                                                                                                                 \
	}

#define NS_ASSERT_NO_ENTRY()                                                                                                    \
	_ns_print_code_message(FUNCTION_STR, __FILE__, __LINE__, "FATAL: Ne entry triggered.", "", NS::PrintMessageType::ERROR); \
	_ns_print_flush_stdout();                                                                                                \
	GENERATE_TRAP();

#define NS_ASSERT_NO_ENTRY_MSG(m_msg)                                                                                              \
	_ns_print_code_message(FUNCTION_STR, __FILE__, __LINE__, "FATAL: Ne entry triggered.", m_msg, NS::PrintMessageType::ERROR); \
	_ns_print_flush_stdout();                                                                                                   \
	GENERATE_TRAP();

/// Ensures `m_cond` is true.
/// If `m_cond` is true, the application crashes.
#define NS_ASSERT_COND(m_cond)                                                                                                                          \
	if make_likely (m_cond) {                                                                                                                        \
	} else {                                                                                                                                         \
		_ns_print_code_message(FUNCTION_STR, __FILE__, __LINE__, "FATAL: Condition \"" _STR(m_cond) "\" is false", "", NS::PrintMessageType::ERROR); \
		_ns_print_flush_stdout();                                                                                                                    \
		GENERATE_TRAP();                                                                                                                             \
	}

/// Ensures `m_cond` is true.
/// If `m_cond` is true, prints `m_msg` and the application crashes.
#define NS_ASSERT_COND_MSG(m_cond, m_msg)                                                                                                                   \
	if make_likely (m_cond) {                                                                                                                            \
	} else {                                                                                                                                             \
		_ns_print_code_message(FUNCTION_STR, __FILE__, __LINE__, "FATAL: Condition \"" _STR(m_cond) "\" is false.", m_msg, NS::PrintMessageType::ERROR); \
		_ns_print_flush_stdout();                                                                                                                        \
		GENERATE_TRAP();                                                                                                                                 \
	}