#include "test_processor.h"

#include "../core/ensure.h"
#include "../core/processor.h"
#include "NetworkSynchronizer/core/event_processor.h"

void test_internal_processor() {
	NS::Processor<int, int> test_event;
	NS_ASSERT_COND(test_event.size() == 0);

	int event_execution_counter = 0;
	int last_event_arg_a = 0;
	int last_event_arg_b = 0;

	// Test the `bind` & `breoadcast` APIs.
	NS::PHandler handler_index = test_event.bind([&](int a, int b) {
		event_execution_counter++;
		last_event_arg_a = a;
		last_event_arg_b = b;
	});

	NS_ASSERT_COND(handler_index != NS::NullPHandler);
	NS_ASSERT_COND(test_event.is_bind(handler_index));
	NS_ASSERT_COND(test_event.size() == 1);

	test_event.broadcast(1, 2);

	NS_ASSERT_COND_MSG(event_execution_counter == 1, "The event should have called the handler at this point.");
	NS_ASSERT_COND_MSG(last_event_arg_a == 1, "The event should have called the handler at this point.");
	NS_ASSERT_COND_MSG(last_event_arg_b == 2, "The event should have called the handler at this point.");

	// Test the `unbind` API.
	test_event.unbind(handler_index);
	NS_ASSERT_COND(!test_event.is_bind(handler_index));
	NS_ASSERT_COND(test_event.size() == 0);

	test_event.broadcast(3, 4);

	NS_ASSERT_COND_MSG(event_execution_counter == 1, "The event should NOT have called the handler at this point.");
	NS_ASSERT_COND_MSG(last_event_arg_a == 1, "The event should NOT have called the handler at this point.");
	NS_ASSERT_COND_MSG(last_event_arg_b == 2, "The event should NOT have called the handler at this point.");

	// Test the `clear` API.
	handler_index = test_event.bind([&](int a, int b) {
		event_execution_counter++;
		last_event_arg_a = a;
		last_event_arg_b = b;
	});
	NS_ASSERT_COND(test_event.size() == 1);
	NS_ASSERT_COND(test_event.is_bind(handler_index));

	test_event.broadcast(5, 6);

	NS_ASSERT_COND_MSG(event_execution_counter == 2, "The event should have called the handler at this point.");
	NS_ASSERT_COND_MSG(last_event_arg_a == 5, "The event should have called the handler at this point.");
	NS_ASSERT_COND_MSG(last_event_arg_b == 6, "The event should have called the handler at this point.");

	test_event.clear();
	NS_ASSERT_COND(test_event.size() == 0);
	NS_ASSERT_COND(!test_event.is_bind(handler_index));
	test_event.broadcast(7, 8);

	NS_ASSERT_COND_MSG(event_execution_counter == 2, "The event should NOT have called the handler at this point.");
	NS_ASSERT_COND_MSG(last_event_arg_a == 5, "The event should NOT have called the handler at this point.");
	NS_ASSERT_COND_MSG(last_event_arg_b == 6, "The event should NOT have called the handler at this point.");

	// Test the `append` API.
	{
		handler_index = test_event.bind([&](int a, int b) {
			event_execution_counter++;
			last_event_arg_a = a;
			last_event_arg_b = b;
			// Check execution order.
			NS_ASSERT_COND(event_execution_counter == 2);
		});
		NS_ASSERT_COND(test_event.size() == 1);

		NS::Processor<int, int> test_event_2;

		NS::PHandler handler_index_2 = test_event_2.bind([&](int a, int b) {
			event_execution_counter++;
			last_event_arg_a = a + 1;
			last_event_arg_b = b + 1;
			// Check execution order.
			NS_ASSERT_COND(event_execution_counter == 1);
		});
		NS_ASSERT_COND(test_event_2.size() == 1);
		NS_ASSERT_COND(test_event_2.is_bind(handler_index_2));

		std::vector<NS::PHandler> new_handlers;
		test_event_2.append(test_event, &new_handlers);

		// Make sure this function is still bind.
		NS_ASSERT_COND(test_event_2.is_bind(handler_index_2));

		// Make sure the append added all functions.
		NS_ASSERT_COND(int(new_handlers.size()) == test_event.size());
		// Make sure the `test_event_2` size equals to `test_event` + the function already registered in `test_event_2`.
		NS_ASSERT_COND(test_event_2.size() == (test_event.size() + 1));
		for (auto H : new_handlers) {
			NS_ASSERT_COND(test_event_2.is_bind(H));
		}

		event_execution_counter = 0;
		test_event_2.broadcast(1, 1);

		NS_ASSERT_COND_MSG(event_execution_counter == 2, "The event should have called two handlers at this point.");
		NS_ASSERT_COND_MSG(last_event_arg_a == 1, "The event should have called the two handlers at this point.");
		NS_ASSERT_COND_MSG(last_event_arg_b == 1, "The event should have called the two handlers at this point.");

		// Make sure the initial `Event` works just fine.
		// But first make sure the CRASH_COND inside the function is not triggered
		// by set `event_execution_counter` to 1.
		event_execution_counter = 1;
		test_event.broadcast(2, 3);

		NS_ASSERT_COND(event_execution_counter == 2);
		NS_ASSERT_COND(last_event_arg_a == 2);
		NS_ASSERT_COND(last_event_arg_b == 3);
	}

	// Test the execution order after using `unbind`.
	{
		test_event.clear();
		event_execution_counter = 0;

		NS::PHandler h = test_event.bind([&](int a, int b) {
			// Under proper considition this doesn't run.
			event_execution_counter++;
		});

		test_event.bind([&](int a, int b) {
			//  So this function shoul be the first one executing.
			event_execution_counter++;
			NS_ASSERT_COND(event_execution_counter == 1);
		});

		test_event.bind([&](int a, int b) {
			// Then this.
			event_execution_counter++;
			NS_ASSERT_COND(event_execution_counter == 2);
		});

		test_event.bind([&](int a, int b) {
			// Then this.
			event_execution_counter++;
			// Check execution order.
			NS_ASSERT_COND(event_execution_counter == 3);
		});

		test_event.unbind(h);
		test_event.broadcast(0, 0);
		NS_ASSERT_COND(event_execution_counter == 3);
	}

	// Test the execution order after using `append`.
	{
		test_event.clear();
		event_execution_counter = 0;

		NS::PHandler h = test_event.bind([&](int a, int b) {
			// Under proper considition this doesn't run.
			event_execution_counter++;
		});

		test_event.bind([&](int a, int b) {
			//  So this function this is the second executing function.
			event_execution_counter++;
			NS_ASSERT_COND(event_execution_counter == 2);
		});

		test_event.bind([&](int a, int b) {
			// Then this.
			event_execution_counter++;
			NS_ASSERT_COND(event_execution_counter == 3);
		});

		test_event.bind([&](int a, int b) {
			// Then this.
			event_execution_counter++;
			// Check execution order.
			NS_ASSERT_COND(event_execution_counter == 4);
		});

		test_event.unbind(h);

		NS::Processor<int, int> test_event_2;
		test_event_2.bind([&](int a, int b) {
			// Under proper considition this is the first functon to be executed
			event_execution_counter++;
			NS_ASSERT_COND(event_execution_counter == 1);
		});

		test_event_2.append(test_event);
		test_event_2.broadcast(0, 0);
		NS_ASSERT_COND(event_execution_counter == 4);
	}

	// Test the lambda added from the same class but different pointer doesn't
	// override each other.
	{
		test_event.clear();
		event_execution_counter = 0;

		struct TestLambda {
			int v = 0;

			NS::PHandler add_lambda(NS::Processor<int, int> &p_processor) {
				return p_processor.bind([this](int a, int b) {
					v = a + b;
				});
			}
		};

		TestLambda test_lambda_1;
		test_lambda_1.v = 2;
		TestLambda test_lambda_2;
		test_lambda_2.v = 3;

		NS::PHandler H_1 = test_lambda_1.add_lambda(test_event);
		NS::PHandler H_2 = test_lambda_2.add_lambda(test_event);

		NS_ASSERT_COND(test_event.size() == 2);
		NS_ASSERT_COND(test_event.is_bind(H_1));
		NS_ASSERT_COND(test_event.is_bind(H_2));
		NS_ASSERT_COND(H_1 != H_2);
	}
}

void test_event_processor() {
	NS::EventProcessor<int, int> test_event;

	int the_a = 2, the_b = 2;
	test_event.broadcast(55, 223);
	NS_ASSERT_COND(test_event.bind_count() == 0);
	NS_ASSERT_COND(the_a == 2);
	NS_ASSERT_COND(the_b == 2);

	{
		std::unique_ptr<NS::EventProcessor<int, int>::Handler> event_handler = test_event.bind([&](int a, int b) {
			the_a = a;
			the_b = b;
		});
		NS_ASSERT_COND(test_event.bind_count() == 1);

		NS_ASSERT_COND(the_a == 2);
		NS_ASSERT_COND(the_b == 2);

		test_event.broadcast(55, 223);
		NS_ASSERT_COND(the_a == 55);
		NS_ASSERT_COND(the_b == 223);
	}

	test_event.broadcast(43, 32);
	NS_ASSERT_COND(test_event.bind_count() == 0);
	NS_ASSERT_COND(the_a == 55);
	NS_ASSERT_COND(the_b == 223);

	// Test that the clear function is able to correctly destroy all the registered functions.
	{
		std::unique_ptr<NS::EventProcessor<int, int>::Handler> event_handler = test_event.bind([&](int a, int b) {
			the_a = a;
			the_b = b;
		});
		NS_ASSERT_COND(test_event.bind_count() == 1);

		event_handler->clear();
		NS_ASSERT_COND(the_a == 55);
		NS_ASSERT_COND(the_b == 223);

		test_event.broadcast(43, 32);
		NS_ASSERT_COND(test_event.bind_count() == 0);
		NS_ASSERT_COND(the_a == 55);
		NS_ASSERT_COND(the_b == 223);
	}

	// Test that the clear function is able to correctly destroy all the registered functions.
	{
		std::unique_ptr<NS::EventProcessor<int, int>::Handler> event_handler = test_event.bind([&](int a, int b) {
			the_a = a;
			the_b = b;
		});
		NS_ASSERT_COND(test_event.bind_count() == 1);

		test_event.clear();
		NS_ASSERT_COND(event_handler && !event_handler->is_valid());

		test_event.broadcast(43, 32);
		NS_ASSERT_COND(test_event.bind_count() == 0);
		NS_ASSERT_COND(the_a == 55);
		NS_ASSERT_COND(the_b == 223);
	}

	// Test the event override.
	{
		std::unique_ptr<NS::EventProcessor<int, int>::Handler> event_handler = test_event.bind([&](int a, int b) {
			the_a = 0;
			the_b = 0;
		});
		event_handler = test_event.bind([&](int a, int b) {
			the_a = 1;
			the_b = 1;
		});
		event_handler = test_event.bind([&](int a, int b) {
			the_a = a;
			the_b = b;
		});

		NS_ASSERT_COND(test_event.bind_count() == 1);
		test_event.broadcast(43, 32);

		NS_ASSERT_COND(the_a == 43);
		NS_ASSERT_COND(the_b == 32);
	}

	// Test multiple functions
	{
		the_a = 0;
		the_b = 0;
		std::unique_ptr<NS::EventProcessor<int, int>::Handler> event_handler_0 = test_event.bind([&](int a, int b) {
			the_a += a;
			the_b += b;
		});
		std::unique_ptr<NS::EventProcessor<int, int>::Handler> event_handler_1 = test_event.bind([&](int a, int b) {
			the_a += a;
			the_b += b;
		});
		std::unique_ptr<NS::EventProcessor<int, int>::Handler> event_handler_2 = test_event.bind([&](int a, int b) {
			the_a += a;
			the_b += b;
		});

		NS_ASSERT_COND(test_event.bind_count() == 3);
		test_event.broadcast(2, 2);

		NS_ASSERT_COND(the_a == 6);
		NS_ASSERT_COND(the_b == 6);

		event_handler_1 = std::unique_ptr<NS::EventProcessor<int, int>::Handler>();

		NS_ASSERT_COND(test_event.bind_count() == 2);
		test_event.broadcast(2, 2);

		NS_ASSERT_COND(the_a == 10);
		NS_ASSERT_COND(the_b == 10);
	}
}

void NS_Test::test_processor() {
	test_internal_processor();
	test_event_processor();
}