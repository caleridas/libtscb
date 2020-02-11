/* -*- C++ -*-
 * (c) 2011 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <unistd.h>

#include <tscb/reactor.h>

#include <gtest/gtest.h>

namespace tscb {

TEST(ReactorTests, basic_operation)
{
	reactor reactor;

	{
		int timer_called = 0;
		connection c = reactor.timer(
			[&timer_called](std::chrono::steady_clock::time_point) { ++timer_called; },
			std::chrono::steady_clock::now());
		reactor.dispatch();

		EXPECT_TRUE(timer_called);

		c.disconnect();
	}

	{
		int fds[2];
		int reader_called = 0;
		EXPECT_TRUE(::pipe(fds) == 0);
		connection c = reactor.watch(
			[&reader_called, &fds] (ioready_events events)
			{
				++reader_called;
				char tmp;
				EXPECT_EQ(1, ::read(fds[0], &tmp, 1));
			}, fds[0], ioready_input);
		reactor.wake_up();
		reactor.dispatch();
		EXPECT_TRUE(!reader_called);

		EXPECT_TRUE(::write(fds[1], "x", 1) == 1);
		reactor.wake_up();
		reactor.dispatch();
		EXPECT_TRUE(reader_called);

		c.disconnect();

		::close(fds[0]);
		::close(fds[1]);
	}

	{
		int worker_called = 0;
		reactor.queue_procedure([&worker_called](){++worker_called;});
		reactor.dispatch();

		EXPECT_TRUE(worker_called);
	}
}

static void
perpetual_work(reactor_service & reactor, int * what)
{
	(*what) ++;
	reactor.queue_procedure(std::bind(perpetual_work, std::ref(reactor), what));
}

TEST(ReactorTests, workqueue_monopolization)
{
	reactor reactor;

	int count = 0;
	perpetual_work(reactor, &count);

	while (count < 10) {
		reactor.dispatch();
	}
}

TEST(ReactorTests, pending)
{
	reactor reactor;

	EXPECT_FALSE(reactor.dispatch_pending());

	/* timers pending */
	{
		int timer_called = 0;
		std::chrono::steady_clock::time_point due = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
		connection c = reactor.timer(
			[&timer_called](std::chrono::steady_clock::time_point) { ++timer_called; },
			due);

		/* registering a new event source may as a side effect cause
		a spurious wakeup, so clear this first */
		while (reactor.dispatch_pending()) { /* nothing */ }

		EXPECT_EQ(0, timer_called);

		while (std::chrono::steady_clock::now() < due) {
			::usleep(1000);
		}

		EXPECT_TRUE(reactor.dispatch_pending());

		EXPECT_EQ(1, timer_called);

		c.disconnect();
		/* removal may cause spurious wakeup as well */
		reactor.dispatch_pending_all();
	}

	/* io events pending */
	{
		int fds[2];
		int reader_called = 0;
		EXPECT_TRUE(::pipe(fds) == 0);
		connection c = reactor.watch(
			[&reader_called, &fds] (ioready_events events)
			{
				++reader_called;
				char tmp;
				EXPECT_EQ(1, ::read(fds[0], &tmp, 1));
			}, fds[0], ioready_input);

		/* registering a new event source may as a side effect cause
		a spurious wakeup, so clear this first */
		reactor.dispatch_pending_all();

		EXPECT_EQ(0, reader_called);
		EXPECT_EQ(1, ::write(fds[1], "x", 1));

		EXPECT_TRUE(reactor.dispatch_pending());
		EXPECT_EQ(1, reader_called);

		c.disconnect();

		::close(fds[0]);
		::close(fds[1]);
		/* removal may cause spurious wakeup as well */
		while (reactor.dispatch_pending()) { /* nothing */ }
	}

	/* pending work items */
	{
		int worker_called = 0;
		reactor.queue_procedure([&worker_called](){ ++worker_called; });

		EXPECT_TRUE(reactor.dispatch_pending());

		EXPECT_EQ(1, worker_called);

		EXPECT_FALSE(reactor.dispatch_pending());
	}
}

}
