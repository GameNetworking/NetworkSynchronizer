#pragma once

#include "core.h"
#include <functional>
#include <vector>

NS_NAMESPACE_BEGIN

typedef const void *EventFuncHandler;
static inline EventFuncHandler NullEventHandler = nullptr;

// A simple but yet effective event system.
template <typename... ARGS>
class Event {
	typedef std::function<void(ARGS...)> func_type;

	std::vector<func_type> binded_handlers;

public:
	EventFuncHandler get_function_handler(func_type p_func) const;
	/// Bind a function and returns it's handler.
	EventFuncHandler bind(func_type p_func);
	void unbind(EventFuncHandler p_handler);
	void clear();
	void broadcast(ARGS... p_args);
};

template <typename... ARGS>
EventFuncHandler Event<ARGS...>::get_function_handler(func_type p_func) const {
	return static_cast<const void *>(p_func.template target<void (*)(ARGS...)>());
}

template <typename... ARGS>
EventFuncHandler Event<ARGS...>::bind(func_type p_func) {
	// Make sure this function was not bind already.
	unbind(get_function_handler(p_func));
	binded_handlers.push_back(p_func);
	return get_function_handler(p_func);
}

template <typename... ARGS>
void Event<ARGS...>::unbind(EventFuncHandler p_handler) {
	int i = 0;
	for (auto &func : binded_handlers) {
		if (get_function_handler(func) == p_handler) {
			auto it = binded_handlers.begin() + i;
			binded_handlers.erase(it);
			break;
		}
		i++;
	}
}

template <typename... ARGS>
void Event<ARGS...>::clear() {
	binded_handlers.clear();
}

template <typename... ARGS>
void Event<ARGS...>::broadcast(ARGS... p_args) {
	for (auto &func : binded_handlers) {
		func(p_args...);
	}
}

NS_NAMESPACE_END
