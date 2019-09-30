/* -*- C++ -*-
 * (c) 2011 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/signal.h>
#include <tscb/ioready.h>
#include <tscb/timer.h>

#include <gtest/gtest.h>

namespace tscb {

TEST(ConnectionsTests, ioready_casts)
{
	ioready_service * ioready = ioready_dispatcher::create();

	/* no casts, assignment/construction to/from same type */
	{
		ioready_connection construct(ioready->watch([](ioready_events){}, 0, ioready_input));
		construct.disconnect();

		ioready_connection assign;
		assign = ioready->watch([](ioready_events){}, 0, ioready_input);
		assign.disconnect();
	}

	/* downcast to "connection" */
	{
		ioready_connection construct(ioready->watch([](ioready_events){}, 0, ioready_input));
		construct.disconnect();

		connection assign;
		assign = ioready->watch([](ioready_events){}, 0, ioready_input);
		assign.disconnect();
	}

	/* scoped_connection */
	{
		ioready_connection construct(ioready->watch([](ioready_events){}, 0, ioready_input));

		scoped_connection assign;
		assign = ioready->watch([](ioready_events){}, 0, ioready_input);
	}

	delete ioready;
}

TEST(ConnectionsTests, timer_casts)
{
	platform_eventflag ev;
	timer_dispatcher timer(ev);

	/* no casts, assignment/construction to/from same type */
	{
		timer_connection construct(timer.timer(
			[](std::chrono::steady_clock::time_point){},
			std::chrono::steady_clock::now()));
		construct.disconnect();

		timer_connection assign;
		assign = timer.timer(
			[](std::chrono::steady_clock::time_point){},
			std::chrono::steady_clock::now());
		assign.disconnect();
	}

	/* downcast to "connection" */
	{
		connection construct(timer.timer(
			[](std::chrono::steady_clock::time_point){},
			std::chrono::steady_clock::now()));
		construct.disconnect();

		connection assign;
		assign = timer.timer(
			[](std::chrono::steady_clock::time_point){},
			std::chrono::steady_clock::now());
		assign.disconnect();
	}

	/* scoped_connection */
	{
		scoped_connection construct(timer.timer(
			[](std::chrono::steady_clock::time_point){},
			std::chrono::steady_clock::now()));

		scoped_connection assign;
		assign = timer.timer(
			[](std::chrono::steady_clock::time_point){},
			std::chrono::steady_clock::now());
	}
}

}
