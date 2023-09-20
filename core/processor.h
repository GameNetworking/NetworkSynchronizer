#pragma once

#include "core.h"
#include <functional>
#include <typeinfo>
#include <vector>

NS_NAMESPACE_BEGIN

typedef const char *FuncHandler;
static inline FuncHandler NullFuncHandler = nullptr;

// A simple but yet effective event system.
template <typename... ARGS>
class Processor {
	typedef void(func_sub_type)(ARGS...);
	typedef std::function<func_sub_type> func_type;

	std::vector<func_type> binded_functions;

public:
	FuncHandler get_function_handler(func_type p_func) const;
	/// Bind a function and returns it's handler.
	FuncHandler bind(func_type p_func);
	void append(const Processor<ARGS...> &p_other);
	void unbind(FuncHandler p_handler);
	bool is_bind(FuncHandler p_handler) const;
	void clear();
	void broadcast(ARGS... p_args);

	int size() const;
};

template <typename... ARGS>
FuncHandler Processor<ARGS...>::get_function_handler(func_type p_func) const {
	return p_func.target_type().name();
}

template <typename... ARGS>
//EventFuncHandler Event<ARGS...>::bind(void (*p_func)(ARGS...)) {
FuncHandler Processor<ARGS...>::bind(func_type p_func) {
	// Make sure this function was not bind already.
	unbind(get_function_handler(p_func));
	binded_functions.push_back(p_func);
	return get_function_handler(p_func);
}

template <typename... ARGS>
void Processor<ARGS...>::append(const Processor<ARGS...> &p_other) {
	for (auto &func : p_other.binded_functions) {
		bind(func);
	}
}

template <typename... ARGS>
void Processor<ARGS...>::unbind(FuncHandler p_handler) {
	int i = 0;
	for (auto &func : binded_functions) {
		if (get_function_handler(func) == p_handler) {
			auto it = binded_functions.begin() + i;
			binded_functions.erase(it);
			break;
		}
		i++;
	}
}

template <typename... ARGS>
bool Processor<ARGS...>::is_bind(FuncHandler p_handler) const {
	for (auto &func : binded_functions) {
		if (get_function_handler(func) == p_handler) {
			return true;
		}
	}
	return false;
}

template <typename... ARGS>
void Processor<ARGS...>::clear() {
	binded_functions.clear();
}

template <typename... ARGS>
void Processor<ARGS...>::broadcast(ARGS... p_args) {
	for (auto &func : binded_functions) {
		func(p_args...);
	}
}

template <typename... ARGS>
int Processor<ARGS...>::size() const {
	return int(binded_functions.size());
}

NS_NAMESPACE_END
