/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#define _LIBTSCB_CALLBACK_UNITTESTS 1
#include <tscb/callback>
#include <tscb/ref>
#include "tests.h"

using namespace tscb;

int result=0;
int called=0;

class Receiver {
public:
	Receiver(void) : refcount(1) {}
	void cbrecv1(int arg) {result=arg;}
	void cbrecv2(int arg) {result=arg; link->cancel(); ASSERT(refcount==2); link=0; ASSERT(refcount==2);}
	void cbrecv3(int arg) {
		called++; result=arg; link->cancel(); link2->cancel();
	}
	
	inline void pin(void) {refcount++;}
	inline void release(void) {refcount--;}
	int refcount;
	
	ref<callback_link> link, link2;
};

void fn(void *context, int arg)
{
	ASSERT(context==0);
	called+=arg;
}

void release(void *context)
{
	ASSERT(context==0);
	result=1;
}

void callback_tests(void)
{
	callback_chain<int> chain;
	{
		/* verify that callbacks are invoked correctly at all, that
		callbacks are cancellable and that references to target objects
		are handled correctly */
		Receiver r;
		r.pin();
		r.link=chain.connect<Receiver, &Receiver::cbrecv1, &Receiver::release>(&r);
		ASSERT(r.refcount==2);
		
		chain(1);
		ASSERT(result==1);
		
		r.link->cancel();
		ASSERT(r.refcount==1);
		r.link=0;
		
		chain(2);
		ASSERT(result==1);
	}
	{
		/* verify that callbacks can cancel themselves and that the reference
		count to the target object is dropped after the callback has
		completed */
		Receiver r;
		r.pin();
		r.link=chain.connect<Receiver, &Receiver::cbrecv2, &Receiver::release>(&r);
		
		chain(3);
		ASSERT(result==3);
		chain(4);
		ASSERT(result==3);
		
		ASSERT(r.refcount==1);
	}
	{
		/* veriy that callbacks can cancel each other (out of two
		callbacks that mutually cancel each other, exactly one must be
		executed) and that reference counting still works as expected */
		
		Receiver r;
		r.pin();
		r.link=chain.connect<Receiver, &Receiver::cbrecv3, &Receiver::release>(&r);
		r.pin();
		r.link2=chain.connect<Receiver, &Receiver::cbrecv3, &Receiver::release>(&r);
		
		chain(5);
		
		ASSERT(result==5);
		ASSERT(called==1);
		ASSERT(r.refcount==1);
	}
	{
		/* verify that, upon destroying the a callback chain, all corresponding
		callback links are removed from the chain and all references
		to target objects are dropped as well */
		Receiver r;
		{
			callback_chain<int> chain;
			r.pin();
			r.link=chain.connect<Receiver, &Receiver::cbrecv1, &Receiver::release>(&r);
			ASSERT(r.link->refcount==2);
			ASSERT(r.refcount==2);
		}
		ASSERT(r.link->refcount==1);
		ASSERT(r.refcount==1);
		r.link->cancel();
	}
	{
		called=result=0;
		ref<callback_link> link=chain.connect<void *, &fn, &release>(0);
		
		chain(1);
		ASSERT(called==1);
		ASSERT(result==0);
		
		link->cancel();
		ASSERT(result==1);
		chain(1);
		ASSERT(called==1);
	}
	/* check cancellation of first element in list */
	{
		called=0;
		ref<callback_link> link1, link2;
		link1=chain.connect<void *, &fn, &release>(0);
		link2=chain.connect<void *, &fn, &release>(0);
		
		chain(1);
		ASSERT(called==2);
		
		link1->cancel();
		called=0;
		chain(1);
		ASSERT(called==1);
		
		link2->cancel();
	}
	/* check cancellation of second element in list */
	{
		called=0;
		ref<callback_link> link1, link2;
		link1=chain.connect<void *, &fn, &release>(0);
		link2=chain.connect<void *, &fn, &release>(0);
		
		chain(1);
		ASSERT(called==2);
		
		link2->cancel();
		called=0;
		chain(1);
		ASSERT(called==1);
		
		link1->cancel();
	}
}

void callback_reftests(void)
{
	callback_chain<int> chain;
	Receiver r;
	
	ASSERT(r.refcount==1);
	tscb::callback link=chain.ref_connect<Receiver, &Receiver::cbrecv1>(&r);
	ASSERT(r.refcount==2);
	link->cancel();
	ASSERT(r.refcount==1);
}

int main(void)
{
	callback_tests();
	callback_reftests();
	return 0;
}
