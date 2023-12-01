#pragma once

#include "core.h"
#include <string>

#ifdef __GNUC__
//#define FUNCTION_STR __PRETTY_FUNCTION__ - too annoying
#define FUNCTION_STR __FUNCTION__
#else
#define FUNCTION_STR __FUNCTION__
#endif

// Turn argument to string constant:
// https://gcc.gnu.org/onlinedocs/cpp/Stringizing.html#Stringizing
#ifndef _STR
#define _STR(m_x) #m_x
#define _MKSTR(m_x) _STR(m_x)
#endif

NS_NAMESPACE_BEGIN

void print_line(const std::string &p_str);

NS_NAMESPACE_END

void _ns_print_code_message(
		const char *p_function,
		const char *p_file,
		int p_line,
		const std::string &p_error,
		const std::string &p_message,
		NS::PrintMessageType p_type);

/// Ensures `m_cond` is true.
/// If `m_cond` is false the current function returns.
#define ENSURE(m_cond)       \
	if (m_cond) [[likely]] { \
	} else {                 \
		return;              \
	}

/// Ensures `m_cond` is true.
/// If `m_cond` is false, prints `m_msg` and the current function returns.
#define ENSURE_MSG(m_cond, m_msg)                                                                                                                                           \
	if (m_cond) [[likely]] {                                                                                                                                                \
	} else {                                                                                                                                                                \
		_ns_print_code_message(FUNCTION_STR, __FILE__, __LINE__, "Condition \"" _STR(m_cond) "\" is true. Returning: " _STR(m_retval), m_msg, NS::PrintMessageType::ERROR); \
		return;                                                                                                                                                             \
	}

/// Ensures `m_cond` is true.
/// If `m_cond` is false, the current function returns `m_retval`.
#define ENSURE_V(m_cond, m_retval) \
	if (m_cond) [[likely]] {       \
	} else {                       \
		return m_retval;           \
	}

/// Ensures `m_cond` is true.
/// If `m_cond` is false, prints `m_msg` and the current function returns `m_retval`.
#define ENSURE_V_MSG(m_cond, m_retval, m_msg)                                                                                                                               \
	if (m_cond) [[likely]] {                                                                                                                                                \
	} else {                                                                                                                                                                \
		_ns_print_code_message(FUNCTION_STR, __FILE__, __LINE__, "Condition \"" _STR(m_cond) "\" is true. Returning: " _STR(m_retval), m_msg, NS::PrintMessageType::ERROR); \
		return m_retval;                                                                                                                                                    \
	}
