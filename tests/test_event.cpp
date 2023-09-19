
#include "core/error/error_macros.h"
#include "modules/network_synchronizer/core/event.h"

void test_event() {
	NS::Event<int, int> test_event;

	int event_execution_counter = 0;
	int last_event_arg_a = 0;
	int last_event_arg_b = 0;

	// Test the `bind` & `breoadcast` function.
	NS::EventFuncHandler handler_index = test_event.bind([&](int a, int b) {
		event_execution_counter++;
		last_event_arg_a = a;
		last_event_arg_b = b;
	});

	test_event.broadcast(1, 2);

	CRASH_COND_MSG(event_execution_counter != 1, "The event should have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_a != 1, "The event should have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_b != 2, "The event should have called the handler at this point.");

	// Test the `unbind` function.
	test_event.unbind(handler_index);
	test_event.broadcast(3, 4);

	CRASH_COND_MSG(event_execution_counter != 1, "The event should NOT have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_a != 1, "The event should NOT have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_b != 2, "The event should NOT have called the handler at this point.");

	// Test the `clear` function.
	handler_index = test_event.bind([&](int a, int b) {
		event_execution_counter++;
		last_event_arg_a = a;
		last_event_arg_b = b;
	});

	test_event.broadcast(5, 6);

	CRASH_COND_MSG(event_execution_counter != 2, "The event should have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_a != 5, "The event should have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_b != 6, "The event should have called the handler at this point.");

	test_event.clear();
	test_event.broadcast(7, 8);

	CRASH_COND_MSG(event_execution_counter != 2, "The event should NOT have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_a != 5, "The event should NOT have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_b != 6, "The event should NOT have called the handler at this point.");
}
