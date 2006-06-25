/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

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

void release(int *closure)
{
}

class Target {
public:
	Target(void) : called(false) {}
	void function(int fd, int event)
	{
		called=true;
	}
	void release(void)
	{
	}
	bool called;
};

void test_dispatcher(ioready_dispatcher *d)
{
	long long t(0);
	{
		int count=d->dispatch(&t);
		ASSERT(count==0);
	}
	{
		int pipefd[2];
		
		int oserror=pipe(pipefd);
		ASSERT(oserror==0);
		
		int called=0;
		
		ref<ioready_callback_link> link=d->watch<int *, function, release>(
			pipefd[0], EVMASK_INPUT, &called);
		
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
		link->cancel();
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
		
		ref<ioready_callback_link> link=d->watch<Target, &Target::function, &Target::release>(
			pipefd[0], EVMASK_INPUT, &target);
		
		int count;
		write(pipefd[1], &count, 1);
		count=d->dispatch(&t);
		ASSERT(count==1);
		ASSERT(target.called==1);
		
		link->cancel();
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
		//sleep(1);
		
		int pipefd[2];
		int oserror=pipe(pipefd);
		ASSERT(oserror==0);
		
		int called=0;
		
		ref<ioready_callback_link> link=d->watch<int *, function, release>(
			pipefd[0], EVMASK_INPUT, &called);
		
		write(pipefd[1], &called, 1);
		
		usleep(10*1000);
		//sleep(1);
		
		ASSERT(called==1);
		
		cancel_dispatching=1;
		
		evflag->set();
		
		//write(pipefd[1], &called, 1);
		
		pthread_join(thread, 0);
		link->cancel();
		
		close(pipefd[0]);
		close(pipefd[1]);
	}
}
