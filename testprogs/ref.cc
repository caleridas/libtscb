/*
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.
 * Refer to the file "COPYING" for details.
 */

#include <iostream>

#include "tests.h"
#include <tscb/ref>

using namespace tscb;

class X {
public:
	static int count;
	
	inline X(void) : refcnt(0) {count++;}
	inline ~X(void) {ASSERT(refcnt==0); count--;}
	inline void pin(void) {refcnt++;}
	inline void release(void) {refcnt--; if (!refcnt) delete this;}

	atomic refcnt;
};

class Y : public X {
};

int X::count=0;

ref_transfer<X> generator1(void)
{
	X *x=new X;
	ASSERT(x->refcnt==0);
	x->pin();
	ASSERT(x->refcnt==1);
	return x;
}

ref_transfer<X> generator2(void)
{
	ref<X> x=new X;
	ASSERT(x->refcnt==1);
	return x;
}

void reftests(void)
{
	ASSERT(X::count==0);
	
	{
		ASSERT(X::count==0);
		
		ref<X> x(generator1());
		ASSERT(X::count==1);
		ASSERT(x->refcnt==1);
		{
			ref<X> y=x;
			ASSERT(x->refcnt==2);
			ASSERT(y->refcnt==2);
		}
		ASSERT(x->refcnt==1);
		
		x=generator1();
		ASSERT(X::count==1);
		ASSERT(x->refcnt==1);
		
		x=generator2();
		ASSERT(X::count==1);
		ASSERT(x->refcnt==1);
	}
	
	ASSERT(X::count==0);
	
	{
		generator1();
		ASSERT(X::count==0);
	}
	
	{
		X *x=generator1().unassign();
		ASSERT(X::count==1);
		ASSERT(x->refcnt==1);
		x->release();
		ASSERT(X::count==0);
	}
	
	{
		X *x=generator1();
		ASSERT(X::count==1);
		ASSERT(x->refcnt==1);
		x->release();
		ASSERT(X::count==0);
	}
}

void take(ref_transfer<X> x)
{
	ref<X> obj=x;
	ASSERT(obj->refcnt==1);
}

ref_transfer<Y> instantiate_y(void)
{
	ref<Y> y=new Y;
	return y;
}

void casttests(void)
{
	{
		ref<Y> y=new Y;
		take(y);
		ASSERT(X::count==0);
		
		take(instantiate_y());
		ASSERT(X::count==0);
	}
	
	{
		ref<X> x=instantiate_y();
		ASSERT(X::count==1);
		ASSERT(x->refcnt==1);
	}
	
	ASSERT(X::count==0);
	
}

int main()
{
	reftests();
	casttests();
}
