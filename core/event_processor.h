#pragma once

#include "core.h"
#include "net_utilities.h"
#include <functional>

NS_NAMESPACE_BEGIN
// A simple but yet effective event system.
template <typename... ARGS>
class EventProcessor {
	typedef void (func_sub_type)(ARGS...);
	typedef std::function<func_sub_type> func_type;

public:
	class Handler {
		int id;
		EventProcessor<ARGS...> *processor;

	public:
		Handler():
			id(-1),
			processor(nullptr) {
		}

		Handler(int p_id, EventProcessor<ARGS...> *p_processor):
			id(p_id),
			processor(p_processor) {
		}

		~Handler() {
			clear();
		}

		bool is_valid() const {
			return id != -1;
		}

		void clear() {
			if (processor) {
				processor->unbind(id);
				id = -1;
				processor = nullptr;
			}
		}
	};

	struct EventProcessorData {
		int id;
		Handler *handler = nullptr;
		func_type function = nullptr;

		bool operator==(const EventProcessorData &o) const {
			return id == o.id;
		}
	};

private:
	std::vector<EventProcessorData> binded_functions;
	int id_counter = 0;

private:
	void unbind(int id);

public:
	~EventProcessor();
	/// Bind a function and returns it's handler.
	std::unique_ptr<Handler> bind(func_type p_func);
	void broadcast(ARGS... p_args);
	void clear();
	int bind_count() const;
	/// Returns true if any function is bound to this.
	bool is_bound() const;
};

template <typename... ARGS>
void EventProcessor<ARGS...>::unbind(int id) {
	const EventProcessorData epd{ id, nullptr, nullptr };
	VecFunc::remove_unordered(binded_functions, epd);
}

template <typename... ARGS>
EventProcessor<ARGS...>::~EventProcessor() {
	clear();
}

template <typename... ARGS>
std::unique_ptr<typename EventProcessor<ARGS...>::Handler> EventProcessor<ARGS...>::bind(func_type p_func) {
	// Make sure this function was not bind already.
	std::unique_ptr<Handler> handler = std::make_unique<Handler>(Handler(id_counter, this));
	binded_functions.push_back({ id_counter, handler.get(), p_func });
	id_counter += 1;
	return std::move(handler);
}

template <typename... ARGS>
void EventProcessor<ARGS...>::clear() {
	while (binded_functions.size() > 0) {
		auto h = binded_functions.back().handler;
		h->clear();
	}
	id_counter = 0;
}

template <typename... ARGS>
void EventProcessor<ARGS...>::broadcast(ARGS... p_args) {
	for (auto &func_data : binded_functions) {
		func_data.function(p_args...);
	}
}

template <typename... ARGS>
int EventProcessor<ARGS...>::bind_count() const {
	return int(binded_functions.size());
}

template <typename... ARGS>
bool EventProcessor<ARGS...>::is_bound() const {
	return binded_functions.size() > 0;
}

NS_NAMESPACE_END