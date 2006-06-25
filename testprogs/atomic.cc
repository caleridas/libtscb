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

void atomictests()
{
	atomic a(0);
	
	ASSERT((int)a==0);
	ASSERT(a.inc_if_not_zero()==false);
	ASSERT((int)a==0);
	a++;
	ASSERT((int)a==1);
	++a;
	ASSERT((int)a==2);
	ASSERT(a.inc_if_not_zero()==true);
	ASSERT((int)a==3);
	a--;
	ASSERT((int)a==2);
	ASSERT((--a)==true);
	ASSERT((int)a==1);
	ASSERT((--a)==false);
	ASSERT((int)a==0);
	
	int oldval=a.cmpxchg(0, 1);
	ASSERT((int)a==1);
	ASSERT(oldval==0);
	
	oldval=a.cmpxchg(1, 2);
	ASSERT((int)a==2);
	ASSERT(oldval==1);
	
	oldval=a.cmpxchg(3, 1);
	ASSERT((int)a==2);
	ASSERT(oldval==2);
}

int main(int argc, char **argv)
{
	atomictests();
}
