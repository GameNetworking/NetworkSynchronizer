
#include "test_processor.h"

#include "core/error/error_macros.h"
#include "core/string/print_string.h"
#include "modules/network_synchronizer/core/processor.h"
#include <functional>

void NS::test_processor() {
	NS::Processor<int, int> test_event;
	CRASH_COND(test_event.size() != 0);

	int event_execution_counter = 0;
	int last_event_arg_a = 0;
	int last_event_arg_b = 0;

	// Test the `get_function_handler` is able to return a valid handler.
	{
		std::function<void(int, int)> func_a = [&](int a, int b) {
			event_execution_counter++;
			last_event_arg_a = a;
			last_event_arg_b = b;
		};
		std::function<void(int, int)> func_aa = func_a;
		std::function<void(int, int)> func_b = [&](int a, int b) {
			event_execution_counter++;
			last_event_arg_a = a;
			last_event_arg_b = b;
		};
		NS::FuncHandler h1_1 = test_event.get_function_handler(func_a);
		NS::FuncHandler h1_2 = test_event.get_function_handler(func_a);
		NS::FuncHandler h1_3 = test_event.get_function_handler(func_aa);
		NS::FuncHandler h2_1 = test_event.get_function_handler(func_b);
		NS::FuncHandler h2_2 = test_event.get_function_handler(func_b);
		CRASH_COND(h1_1 != h1_2);
		CRASH_COND(h1_1 != h1_3);
		CRASH_COND(h2_1 != h2_2);
		CRASH_COND(h1_1 == h2_1);
	}

	// Test the `bind` & `breoadcast` APIs.
	NS::FuncHandler handler_index = test_event.bind([&](int a, int b) {
		event_execution_counter++;
		last_event_arg_a = a;
		last_event_arg_b = b;
	});

	CRASH_COND(handler_index == NS::NullFuncHandler);
	CRASH_COND(!test_event.is_bind(handler_index));
	CRASH_COND(test_event.size() != 1);

	test_event.broadcast(1, 2);

	CRASH_COND_MSG(event_execution_counter != 1, "The event should have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_a != 1, "The event should have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_b != 2, "The event should have called the handler at this point.");

	// Test the `unbind` API.
	test_event.unbind(handler_index);
	CRASH_COND(test_event.is_bind(handler_index));
	CRASH_COND(test_event.size() != 0);

	test_event.broadcast(3, 4);

	CRASH_COND_MSG(event_execution_counter != 1, "The event should NOT have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_a != 1, "The event should NOT have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_b != 2, "The event should NOT have called the handler at this point.");

	// Test the `clear` API.
	handler_index = test_event.bind([&](int a, int b) {
		event_execution_counter++;
		last_event_arg_a = a;
		last_event_arg_b = b;
	});
	CRASH_COND(test_event.size() != 1);
	CRASH_COND(!test_event.is_bind(handler_index));

	test_event.broadcast(5, 6);

	CRASH_COND_MSG(event_execution_counter != 2, "The event should have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_a != 5, "The event should have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_b != 6, "The event should have called the handler at this point.");

	test_event.clear();
	CRASH_COND(test_event.size() != 0);
	CRASH_COND(test_event.is_bind(handler_index));
	test_event.broadcast(7, 8);

	CRASH_COND_MSG(event_execution_counter != 2, "The event should NOT have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_a != 5, "The event should NOT have called the handler at this point.");
	CRASH_COND_MSG(last_event_arg_b != 6, "The event should NOT have called the handler at this point.");

	// Test the `append` API.
	{
		handler_index = test_event.bind([&](int a, int b) {
			event_execution_counter++;
			last_event_arg_a = a;
			last_event_arg_b = b;
			// Check execution order.
			CRASH_COND(event_execution_counter != 2);
		});
		CRASH_COND(test_event.size() != 1);

		NS::Processor<int, int> test_event_2;

		NS::FuncHandler handler_index_2 = test_event_2.bind([&](int a, int b) {
			event_execution_counter++;
			last_event_arg_a = a + 1;
			last_event_arg_b = b + 1;
			// Check execution order.
			CRASH_COND(event_execution_counter != 1);
		});
		CRASH_COND(test_event_2.size() != 1);
		CRASH_COND(!test_event_2.is_bind(handler_index_2));

		test_event_2.append(test_event);
		CRASH_COND(!test_event_2.is_bind(handler_index));
		CRASH_COND(!test_event_2.is_bind(handler_index_2));
		CRASH_COND(!test_event.is_bind(handler_index));
		CRASH_COND(test_event.is_bind(handler_index_2));
		CRASH_COND(test_event_2.size() != 2);
		CRASH_COND(test_event.size() != 1);

		event_execution_counter = 0;
		test_event_2.broadcast(1, 1);

		CRASH_COND_MSG(event_execution_counter != 2, "The event should have called two handlers at this point.");
		CRASH_COND_MSG(last_event_arg_a != 1, "The event should have called the two handlers at this point.");
		CRASH_COND_MSG(last_event_arg_b != 1, "The event should have called the two handlers at this point.");

		// Make sure the initial `Event` works just fine.
		// But first make sure the CRASH_COND inside the function is not triggered
		// by set `event_execution_counter` to 1.
		event_execution_counter = 1;
		test_event.broadcast(2, 3);

		CRASH_COND(event_execution_counter != 2);
		CRASH_COND(last_event_arg_a != 2);
		CRASH_COND(last_event_arg_b != 3);
	}

	// Test the execution order after using `unbind`.
	{
		test_event.clear();
		event_execution_counter = 0;

		NS::FuncHandler h = test_event.bind([&](int a, int b) {
			// Under proper considition this doesn't run.
			event_execution_counter++;
		});

		test_event.bind([&](int a, int b) {
			//  So this function shoul be the first one executing.
			event_execution_counter++;
			CRASH_COND(event_execution_counter != 1);
		});

		test_event.bind([&](int a, int b) {
			// Then this.
			event_execution_counter++;
			CRASH_COND(event_execution_counter != 2);
		});

		test_event.bind([&](int a, int b) {
			// Then this.
			event_execution_counter++;
			// Check execution order.
			CRASH_COND(event_execution_counter != 3);
		});

		test_event.unbind(h);
		test_event.broadcast(0, 0);
		CRASH_COND(event_execution_counter != 3);
	}

	// Test the execution order after using `append`.
	{
		test_event.clear();
		event_execution_counter = 0;

		NS::FuncHandler h = test_event.bind([&](int a, int b) {
			// Under proper considition this doesn't run.
			event_execution_counter++;
		});

		test_event.bind([&](int a, int b) {
			//  So this function this is the second executing function.
			event_execution_counter++;
			CRASH_COND(event_execution_counter != 2);
		});

		test_event.bind([&](int a, int b) {
			// Then this.
			event_execution_counter++;
			CRASH_COND(event_execution_counter != 3);
		});

		test_event.bind([&](int a, int b) {
			// Then this.
			event_execution_counter++;
			// Check execution order.
			CRASH_COND(event_execution_counter != 4);
		});

		test_event.unbind(h);

		NS::Processor<int, int> test_event_2;
		test_event_2.bind([&](int a, int b) {
			// Under proper considition this is the first functon to be executed
			event_execution_counter++;
			CRASH_COND(event_execution_counter != 1);
		});

		test_event_2.append(test_event);
		test_event_2.broadcast(0, 0);
		CRASH_COND(event_execution_counter != 4);
	}
}
