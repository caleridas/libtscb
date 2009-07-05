/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <boost/bind.hpp>

#define _LIBTSCB_CALLBACK_UNITTESTS 1
#include <tscb/callback>
#include "tests.h"

//using namespace tscb;

int result=0;
int called=0;

class Receiver {
public:
	Receiver(void) : refcount(1) {}
	void cbrecv1(int arg) {result=arg;}
	void cbrecv2(int arg) {
		result=arg;
		link1->cancel();
		ASSERT(refcount==2);
		link1=0;
		ASSERT(refcount==2);
	}
	void cbrecv3(int arg) {
		called++; result=arg; link1->cancel(); link2->cancel();
	}
	
	inline void pin(void) {refcount++;}
	inline void release(void) {refcount--;}
	int refcount;
	
	tscb::link link1, link2;
};

static inline void intrusive_ptr_add_ref(Receiver *t) throw()
{
	t->pin();
}

static inline void intrusive_ptr_release(Receiver *t) throw()
{
	t->release();
}

static void fn(int arg)
{
	called+=arg;
}

void callback_tests(void)
{
	tscb::callback_chain<void (int)> chain;
	{
		/* verify that callbacks are invoked correctly at all, that
		callbacks are cancellable and that references to target objects
		are handled correctly */
		Receiver r;
		
		r.link1=chain.connect(boost::bind(&Receiver::cbrecv1, boost::intrusive_ptr<Receiver>(&r), _1));
		ASSERT(r.refcount==2);
		ASSERT(r.link1->refcount==2);
		
		chain(1);
		ASSERT(result==1);
		
		r.link1->cancel();
		ASSERT(r.refcount==1);
		r.link1=0;
		
		chain(2);
		ASSERT(result==1);
	}
	{
		/* verify that callbacks can cancel themselves and that the reference
		count to the target object is dropped after the callback has
		completed */
		Receiver r;
		r.link1=chain.connect(boost::bind(&Receiver::cbrecv2, boost::intrusive_ptr<Receiver>(&r), _1));
		
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
		r.link1=chain.connect(boost::bind(&Receiver::cbrecv3, boost::intrusive_ptr<Receiver>(&r), _1));
		r.link2=chain.connect(boost::bind(&Receiver::cbrecv3, boost::intrusive_ptr<Receiver>(&r), _1));
		
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
			tscb::callback_chain<void (int)> chain;
			r.link1=chain.connect(boost::bind(&Receiver::cbrecv1, boost::intrusive_ptr<Receiver>(&r), _1));
			ASSERT(r.link1->refcount==2);
			ASSERT(r.refcount==2);
		}
		ASSERT(r.link1->refcount==1);
		ASSERT(r.refcount==1);
		r.link1->cancel();
	}
	{
		called=result=0;
		tscb::link l=chain.connect(boost::bind(fn, _1));
		
		chain(1);
		ASSERT(called==1);
		ASSERT(result==0);
		
		l->cancel();
		chain(1);
		ASSERT(called==1);
	}
	/* check cancellation of first element in list */
	{
		called=0;
		tscb::link link1, link2;
		link1=chain.connect(boost::bind(fn, _1));
		link2=chain.connect(boost::bind(fn, _1));
		
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
		tscb::link link1, link2;
		link1=chain.connect(boost::bind(fn, _1));
		link2=chain.connect(boost::bind(fn, _1));
		
		chain(1);
		ASSERT(called==2);
		
		link2->cancel();
		called=0;
		chain(1);
		ASSERT(called==1);
		
		link1->cancel();
	}
}

int main(void)
{
	callback_tests();
	#if 0
	callback_reftests();
	#endif
	return 0;
}
