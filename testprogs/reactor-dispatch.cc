/* -*- C++ -*-
 * (c) 2011 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <boost/bind.hpp>

#define private public
#define protected public

#include <tscb/dispatch>

static bool dummy_timer(int * what, boost::posix_time::ptime & now)
{
	(*what) ++;
	return false;
}

static void dummy_reader(int * what, int fd, tscb::ioready_events events)
{
	char tmp;
	read(fd, &tmp, 1);
	(*what) ++;
}

static void dummy_work(int * what)
{
	(*what) ++;
}

void test_basic_operation(void)
{
	tscb::posix_reactor reactor;
	
	{
		int timer_called = 0;
		tscb::connection c = reactor.timer(boost::bind(dummy_timer, &timer_called, _1), tscb::monotonic_time());
		reactor.dispatch();
		
		assert(timer_called);
		
		c.disconnect();
	}
	
	{
		int fds[2];
		int reader_called = 0;
		pipe(fds);
		tscb::connection c = reactor.watch(boost::bind(dummy_reader, &reader_called, fds[0], _1), fds[0], tscb::ioready_input);
		reactor.get_eventtrigger().set();
		reactor.dispatch();
		assert(!reader_called);
		
		write(fds[1], "x", 1);
		reactor.get_eventtrigger().set();
		reactor.dispatch();
		assert(reader_called);
		
		c.disconnect();
		
		close(fds[0]);
		close(fds[1]);
	}
	
	{
		int worker_called = 0;
		reactor.post(boost::bind(dummy_work, &worker_called));
		reactor.dispatch();
		
		assert(worker_called);
	}
}

static void perpetual_work(tscb::posix_reactor_service & reactor, int * what)
{
	(*what) ++;
	reactor.post(boost::bind(perpetual_work, boost::ref(reactor), what));
}

void test_workqueue_monopolization(void)
{
	tscb::posix_reactor reactor;
	
	int count = 0;
	perpetual_work(reactor, &count);
	
	while(count < 10)
		reactor.dispatch();
}

int main()
{
	test_basic_operation();
	test_workqueue_monopolization();
}
