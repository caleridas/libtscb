/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <boost/bind.hpp>

#define private public
#define protected public

#include "tests.h"

#include <tscb/ioready>
#include <pthread.h>

using namespace tscb;

void function(int *closure, int fd, int event)
{
	char c;
	read(fd, &c, 1);
	*closure=1;
}

class Target {
public:
	Target(void) : called(false) {}
	void function(int event)
	{
		called=true;
	}
	bool called;
};

class Target2;

static inline void intrusive_ptr_add_ref(Target2 *t) throw();
static inline void intrusive_ptr_release(Target2 *t) throw();

class Target2 {
public:
	Target2(ioready_service *srv, int fd)
		: called(false), refcount(1)
	{
		link=srv->watch(boost::bind(&Target2::input, boost::intrusive_ptr<Target2>(this), fd, _1), fd, EVMASK_INPUT);
		ASSERT(refcount==2);
	}
	
	void input(int fd, int event)
	{
		char c;
		read(fd, &c, 1);
		called=true;
		link->disconnect();
		ASSERT(refcount==2);
	}
	
	void pin(void)
	{
		refcount++;
	}
	
	void release(void)
	{
		refcount--;
	}
	
	ioready_connection link;
	bool called;
	int refcount;
};

static inline void intrusive_ptr_add_ref(Target2 *t) throw()
{
	t->pin();
}

static inline void intrusive_ptr_release(Target2 *t) throw()
{
	t->release();
}

void test_dispatcher(ioready_dispatcher *d)
{
	boost::posix_time::time_duration t(0);
	/* verify that an empty dispatcher in fact does nothing */
	{
		int count=d->dispatch(&t);
		ASSERT(count==0);
	}
	/* verify that basic dispatching and cancellation works */
	{
		int pipefd[2];
		
		int oserror=pipe(pipefd);
		ASSERT(oserror==0);
		
		int called=0;
		
		tscb::ioready_connection link=d->watch(boost::bind(function, &called, pipefd[0], _1),
			pipefd[0], EVMASK_INPUT);
		
		int count=d->dispatch(&t);
		ASSERT(count==0);
		
		write(pipefd[1], &count, 1);
		count=d->dispatch(&t);
		ASSERT(count==1);
		ASSERT(called==1);
		
		called=0;
		link->modify(0);
		write(pipefd[1], &count, 1);
		count=d->dispatch(&t);
		ASSERT(count==0);
		ASSERT(called==0);
		
		called=0;
		link->modify(EVMASK_INPUT);
		count=d->dispatch(&t);
		ASSERT(count==1);
		ASSERT(called==1);
		
		write(pipefd[1], &count, 1);
		called=0;
		link->disconnect();
		count=d->dispatch(&t);
		ASSERT(count==0);
		ASSERT(called==0);
		
		ASSERT(link->refcount==1);
		
		close(pipefd[0]);
		close(pipefd[1]);
	}
	{
		int pipefd[2];
		
		int oserror=pipe(pipefd);
		ASSERT(oserror==0);
		
		Target target;
		
		tscb::ioready_connection link=d->watch(boost::bind(&Target::function, &target, _1),
			pipefd[0], EVMASK_INPUT);
		
		int count;
		write(pipefd[1], &count, 1);
		count=d->dispatch(&t);
		ASSERT(count==1);
		ASSERT(target.called==1);
		
		link->disconnect();
		count=d->dispatch(&t);
		ASSERT(count==0);
		
		close(pipefd[0]);
		close(pipefd[1]);
	}
		
	/* verify that a callback can cancel itself */
	{
		int pipefd[2];
		
		int oserror=pipe(pipefd);
		ASSERT(oserror==0);
		
		Target2 target(d, pipefd[0]);
		
		int count;
		write(pipefd[1], &count, 1);
		count=d->dispatch(&t);
		ASSERT(count==1);
		ASSERT(target.called==1);
		ASSERT(target.refcount==1);
		
		write(pipefd[1], &count, 1);
		count=d->dispatch(&t);
		ASSERT(count==0);
		
		close(pipefd[0]);
		close(pipefd[1]);
	}
}

static int cancel_dispatching;

static void *run_dispatcher(void *arg)
{
	ioready_dispatcher *d=(ioready_dispatcher *)arg;
	
	while(!cancel_dispatching) {
		d->dispatch(0);
	}
	
	return 0;
}

void test_dispatcher_threading(ioready_dispatcher *d)
{
	{
		cancel_dispatching=0;
		
		eventflag *evflag=d->get_eventflag();
		
		pthread_t thread;
		
		pthread_create(&thread, 0, &run_dispatcher, d);
		
		usleep(10*1000);
		
		int pipefd[2];
		int oserror=pipe(pipefd);
		ASSERT(oserror==0);
		
		int called=0;
		
		tscb::ioready_connection link=d->watch(boost::bind(function, &called, pipefd[0], _1),
			pipefd[0], EVMASK_INPUT);
		
		write(pipefd[1], &called, 1);
		
		usleep(10*1000);
		
		ASSERT(called==1);
		
		cancel_dispatching=1;
		
		evflag->set();
		
		pthread_join(thread, 0);
		link->disconnect();
		
		close(pipefd[0]);
		close(pipefd[1]);
	}
}
