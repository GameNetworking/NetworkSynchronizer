#pragma once

#include "core.h"
#include <functional>
#include <vector>

NS_NAMESPACE_BEGIN

typedef int PHandler;
static inline PHandler NullPHandler = -1;

// A simple but yet effective event system.
template <typename... ARGS>
class Processor {
	typedef void(func_sub_type)(ARGS...);
	typedef std::function<func_sub_type> func_type;

	int index_counter = 0;
	struct ProcessorData {
		PHandler handler;
		func_type function;
	};

	std::vector<ProcessorData> binded_functions;

public:
	/// Bind a function and returns it's handler.
	PHandler bind(func_type p_func);
	void append(const Processor<ARGS...> &p_other, std::vector<PHandler> *p_added_handlers = nullptr);
	void unbind(PHandler p_handler);
	bool is_bind(PHandler p_handler) const;
	void clear();
	void broadcast(ARGS... p_args);

	int find_function(PHandler p_handler) const;
	int size() const;
};

template <typename... ARGS>
PHandler Processor<ARGS...>::bind(func_type p_func) {
	// Make sure this function was not bind already.
	const PHandler handler = index_counter;
	binded_functions.push_back({ handler, p_func });
	index_counter += 1;
	return handler;
}

template <typename... ARGS>
void Processor<ARGS...>::append(const Processor<ARGS...> &p_other, std::vector<PHandler> *p_added_handlers) {
	for (auto &func_data : p_other.binded_functions) {
		const PHandler handler = bind(func_data.function);
		if (p_added_handlers) {
			p_added_handlers->push_back(handler);
		}
	}
}

template <typename... ARGS>
void Processor<ARGS...>::unbind(PHandler p_handler) {
	const int index = find_function(p_handler);
	if (index >= 0) {
		auto it = binded_functions.begin() + index;
		binded_functions.erase(it);
	}
}

template <typename... ARGS>
bool Processor<ARGS...>::is_bind(PHandler p_handler) const {
	return find_function(p_handler) != NullPHandler;
}

template <typename... ARGS>
void Processor<ARGS...>::clear() {
	binded_functions.clear();
	index_counter = 0;
}

template <typename... ARGS>
void Processor<ARGS...>::broadcast(ARGS... p_args) {
	for (auto &func_data : binded_functions) {
		func_data.function(p_args...);
	}
}

template <typename... ARGS>
int Processor<ARGS...>::find_function(PHandler p_handler) const {
	int i = 0;
	for (auto &func_data : binded_functions) {
		if (func_data.handler == p_handler) {
			return i;
		}
		i += 1;
	}
	return -1;
}

template <typename... ARGS>
int Processor<ARGS...>::size() const {
	return int(binded_functions.size());
}

NS_NAMESPACE_END
