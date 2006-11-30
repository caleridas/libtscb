/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#define private public
#define protected public

#include <tscb/timer>
#include <tscb/eventflag>
#include "tests.h"

using namespace tscb;

class my_eventflag: public eventflag {
public:
	my_eventflag(void) : flagged(false) {}
	virtual ~my_eventflag(void) throw() {}
	
	virtual void set(void) throw() {flagged=true;}
	virtual void wait(void) throw() {while(!flagged);}
	virtual void clear(void) throw() {flagged=false;}
	
	volatile bool flagged;
};

my_eventflag flag;
int called=0;
int released=0;

ref<callback_link> timer_link;

bool my_fn(void *, long long &time)
{
	time++;
	called++;
	return true;
}

bool my_fn2(void *, long long &time)
{
	time++;
	called++;
	ASSERT(released==0);
	timer_link->cancel();
	ASSERT(released==0);
	return true;
}

void my_release(void *)
{
	released++;
}

class X {
public:
	bool fn(long long &t) {return false;}
	void pin(void) {}
	void release(void) {}
};

void timer_tests(void)
{
	timerqueue_dispatcher tq(&flag);
	
	{
		long long zero(0);
		bool pending=tq.run_queue(zero);
		ASSERT(pending==false);
	}
	
	{
		long long zero(0);
		bool pending=tq.run_queue(zero);
		ASSERT(pending==false);
	}
	
	{
		called=0;
		long long time(0);
		timer_link=tq.timer<void *, &my_fn, &my_release>(time, (void *)0);
		ASSERT(timer_link->refcount==2);
		
		ASSERT(flag.flagged==true);
		flag.clear();
		
		bool pending=tq.run_queue(time);
		ASSERT(pending==true);
		ASSERT(called==1);
		ASSERT(time==1);
		ASSERT(flag.flagged==false);
		timer_link->cancel();
		ASSERT(flag.flagged==true);
		flag.clear();
		pending=tq.run_queue(time);
		ASSERT(pending==false);
		ASSERT(called==1);
		ASSERT(flag.flagged==false);
		
		ASSERT(timer_link->refcount==1);
	}
	
	{
		long long time(0);
		timer_link=tq.timer<void *, &my_fn2, &my_release>(time, (void *)0);
		
		called=0; released=0;
		tq.run_queue(time);
		ASSERT(called==1);
		ASSERT(released==1);
		ASSERT(timer_link->refcount==1);
	}
	
	{
		X x;
		long long time(0);
		timer_link=tq.timer<X, &X::fn, &X::release>(time, &x);
		timer_link->cancel();
		ASSERT(timer_link->refcount==1);
	}
	{
		X x;
		long long time(0);
		timer_link=tq.ref_timer<X, &X::fn>(time, &x);
		timer_link->cancel();
		ASSERT(timer_link->refcount==1);
	}
}

int main()
{
	timer_tests();
	return 0;
}
