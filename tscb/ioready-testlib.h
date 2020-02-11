/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_IOREADY_TESTLIB
#define TSCB_IOREADY_TESTLIB

#include <fcntl.h>
#include <unistd.h>

#include <thread>

#include <tscb/detail/eventflag.h>
#include <tscb/ioready.h>

#include <gtest/gtest.h>

namespace tscb {

class IoreadyTests : public ::testing::Test {
protected:
	class Target {
	public:
		Target() : called(false) {}

		void
		function(int event)
		{
			called = true;
		}

		bool called;
	};

	class Target2 {
	public:
		Target2(ioready_service *srv, int fd)
			: called(false), refcount(1)
		{
			link = srv->watch(
				std::bind(
					&Target2::input,
					detail::intrusive_ptr<Target2>(this),
					fd, std::placeholders::_1),
				fd, ioready_input);
			EXPECT_EQ(2, refcount);
		}

		void
		input(int fd, int event)
		{
			char c;
			EXPECT_EQ(1, ::read(fd, &c, 1));
			called = true;
			link.disconnect();
			EXPECT_EQ(2, refcount);
		}

		ioready_connection link;
		bool called;
		int refcount;

		friend inline void
		intrusive_ptr_add_ref(Target2 *t) noexcept { ++t->refcount; }
		friend inline void
		intrusive_ptr_release(Target2 *t) noexcept { --t->refcount; }
	};

	void
	run_simple(ioready_dispatcher * d);

	void
	run_threads(ioready_dispatcher * d);

	void
	run_sync_disconnect(ioready_dispatcher * d);

	void
	run_all(ioready_dispatcher * d);
};

void
IoreadyTests::run_simple(ioready_dispatcher *d)
{
	std::chrono::steady_clock::duration t = std::chrono::milliseconds(0);
	/* verify that an empty dispatcher in fact does nothing */
	{
		int count=d->dispatch(&t);
		EXPECT_EQ(0, count);
	}
	/* verify that basic dispatching and cancellation works */
	{
		int pipefd[2];

		int oserror=pipe(pipefd);
		EXPECT_EQ(0, oserror);

		int called = 0;

		tscb::ioready_connection link = d->watch(
			[&called, &pipefd](ioready_events event) {
				char c;
				EXPECT_EQ(1, ::read(pipefd[0], &c, 1));
				called = 1;
			},
			pipefd[0], ioready_input);

		int count=d->dispatch(&t);
		EXPECT_EQ(0, count);

		EXPECT_EQ(1, ::write(pipefd[1], &count, 1));
		count=d->dispatch(&t);
		EXPECT_EQ(1, count);
		EXPECT_EQ(1, called);

		called = 0;
		link.modify(ioready_none);
		EXPECT_EQ(1, ::write(pipefd[1], &count, 1));
		count=d->dispatch(&t);
		EXPECT_EQ(0, count);
		EXPECT_EQ(0, called);

		called = 0;
		link.modify(ioready_input);
		count=d->dispatch(&t);
		EXPECT_EQ(1, count);
		EXPECT_EQ(1, called);

		EXPECT_EQ(1, ::write(pipefd[1], &count, 1));
		called = 0;
		detail::intrusive_ptr<ioready_connection::link_type> cb(link.get());
		link.disconnect();
		count = d->dispatch(&t);
		EXPECT_EQ(0, count);
		EXPECT_EQ(0, called);

		::close(pipefd[0]);
		::close(pipefd[1]);
	}
	{
		int pipefd[2];

		int oserror=pipe(pipefd);
		EXPECT_EQ(0, oserror);

		Target target;

		tscb::connection link=d->watch(std::bind(&Target::function, &target, std::placeholders::_1),
			pipefd[0], ioready_input);

		int count = 0;
		EXPECT_EQ(1, ::write(pipefd[1], &count, 1));
		count=d->dispatch(&t);
		EXPECT_EQ(1, count);
		EXPECT_EQ(1, target.called);

		link.disconnect();
		count=d->dispatch(&t);
		EXPECT_EQ(0, count);

		::close(pipefd[0]);
		::close(pipefd[1]);
	}

	/* verify that a callback can cancel itself */
	{
		int pipefd[2];

		int oserror=pipe(pipefd);
		EXPECT_EQ(0, oserror);

		Target2 target(d, pipefd[0]);

		int count = 0;
		EXPECT_EQ(1, ::write(pipefd[1], &count, 1));
		count=d->dispatch(&t);
		EXPECT_EQ(1, count);
		EXPECT_EQ(1, target.called);
		EXPECT_EQ(1, target.refcount);

		EXPECT_EQ(1, ::write(pipefd[1], &count, 1));
		count=d->dispatch(&t);
		EXPECT_EQ(0, count);

		::close(pipefd[0]);
		::close(pipefd[1]);
	}
}

class pipe_swapper {
public:
	void handle_pipe1(ioready_events events)
	{
		char c;
		ssize_t count = ::read(pipe1[0], &c, 1);
		if (count == 0) {
			events |= ioready_hangup;
		}
		EXPECT_TRUE((events & ioready_hangup) != 0);
		conn.disconnect();
		::close(pipe1[0]);
		dup2(pipe2[0], pipe1[0]);
		conn = d->watch(std::bind(&pipe_swapper::handle_pipe2, this, std::placeholders::_1), pipe1[0], ioready_input);
	}

	void handle_pipe2(ioready_events events)
	{
		char c;
		ssize_t count = ::read(pipe1[0], &c, 1);
		if (count == 0) {
			events |= ioready_hangup;
		}
		EXPECT_EQ(1, count);
		EXPECT_TRUE(!(events & ioready_hangup));
		EXPECT_TRUE(events & ioready_input);
		conn.disconnect();
		finished = true;
	}

	int pipe1[2], pipe2[2];
	connection conn;
	ioready_dispatcher * d;
	bool finished;
};

void
IoreadyTests::run_sync_disconnect(ioready_dispatcher * d)
{
	pipe_swapper sw;

	EXPECT_EQ(0, pipe(sw.pipe1));
	fcntl(sw.pipe1[0], F_SETFL, O_NONBLOCK);
	EXPECT_EQ(0, pipe(sw.pipe2));
	fcntl(sw.pipe2[0], F_SETFL, O_NONBLOCK);
	sw.d = d;
	sw.conn = d->watch(std::bind(&pipe_swapper::handle_pipe1, &sw, std::placeholders::_1), sw.pipe1[0], ioready_input);
	sw.finished = false;

	char c = 0;
	EXPECT_EQ(1, ::write(sw.pipe2[1], &c, 1));
	::close(sw.pipe1[1]);

	while (!sw.finished) {
		std::chrono::steady_clock::duration t = std::chrono::milliseconds(0);
		d->dispatch(&t);
	}

	::close(sw.pipe1[0]);
	::close(sw.pipe2[0]);
	::close(sw.pipe2[1]);
}

void
IoreadyTests::run_threads(ioready_dispatcher * d)
{
	std::atomic<bool> stop_dispatcher(false);

	std::thread t([d, &stop_dispatcher]()
		{
			while (!stop_dispatcher.load(std::memory_order_relaxed)) {
				d->dispatch(0);
			}
		});


	int pipefd[2];
	EXPECT_EQ(0, ::pipe(pipefd));

	detail::atomic_eventflag ev;

	tscb::ioready_connection link = d->watch(
		[&ev, &pipefd](ioready_events event)
		{
			char c;
			EXPECT_EQ(1, ::read(pipefd[0], &c, 1));
			ev.set();
		},
		pipefd[0], ioready_input);

	char c = 0;
	EXPECT_EQ(1, ::write(pipefd[1], &c, 1));

	ev.wait();

	stop_dispatcher.store(true, std::memory_order_relaxed);
	d->wake_up();

	t.join();

	link.disconnect();

	::close(pipefd[0]);
	::close(pipefd[1]);
}

void
IoreadyTests::run_all(ioready_dispatcher * d)
{
	run_simple(d);
	run_threads(d);
	run_sync_disconnect(d);
}

}

#endif
