/*
 * (c) 2004 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.
 * Refer to the file "COPYING" for details.
 */

#include <iostream>

#include "tests.h"
#include <tscb/atomic>

using namespace tscb;

static inline bool inc_if_not_zero(atomics::atomic_int &a)
{
	int expected;
	do {
		expected=a.load(atomics::memory_order_relaxed);
		if (expected==0) return false;
	} while (!a.compare_exchange_strong(expected, expected+1, atomics::memory_order_acquire));
	return true;
}


/* note: the following code obviously does not even try to test
the atomicity of the operations in question -- this is not
really feasible as a unit test, but testing that the operations
behave correctly when executed sequentially has already caught
a sizable number of bugs */
void atomictests()
{
	tscb::atomics::atomic_int a(0);
	
	ASSERT((int)a==0);
	ASSERT(inc_if_not_zero(a)==false);
	ASSERT((int)a==0);
	a++;
	ASSERT((int)a==1);
	++a;
	ASSERT((int)a==2);
	ASSERT(inc_if_not_zero(a)==true);
	ASSERT((int)a==3);
	a--;
	ASSERT((int)a==2);
	ASSERT((--a)==true);
	ASSERT((int)a==1);
	ASSERT((--a)==false);
	ASSERT((int)a==0);
	
	int oldval=0;
	bool success=a.compare_exchange_strong(oldval, 1);
	ASSERT(success);
	ASSERT((int)a==1);
	ASSERT(oldval==0);
	
	oldval=1;
	success=a.compare_exchange_strong(oldval, 2);
	ASSERT(success);
	ASSERT((int)a==2);
	ASSERT(oldval==1);
	
	oldval=3;
	success=a.compare_exchange_strong(oldval, 1);
	ASSERT(!success);
	ASSERT((int)a==2);
	ASSERT(oldval==2);
}

int main(int argc, char **argv)
{
	atomictests();
}
