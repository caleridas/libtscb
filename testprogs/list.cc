/* -*- C++ -*-
 * (c) 2004 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include "tests.h"
#include <tscb/list>

using namespace tscb;

class X {
public:
	X *prev, *next;
};

typedef list<X, &X::prev, &X::next> XL;

void listtests(void)
{
	X a, b, c, d;
	XL list;
	XL::iterator i, j;
	
	i=list.begin();
	ASSERT(i==list.end());

	list.push_back(&a);
	list.push_back(&b);
	i=list.begin();
	ASSERT((X *)i==&a);
	i++;
	ASSERT((X *)i==&b);
	i++;
	ASSERT(i==list.end());
	i--;
	ASSERT((X *)i==&b);
	
	j=list.begin();
	ASSERT(i!=j);
	list.push_front(&c);
	list.push_back(&d);
	ASSERT((X *)j==&a);
	ASSERT((X *)i==&b);
	--j;
	++i;
	ASSERT((X *)j==&c);
	ASSERT((X *)i==&d);
	list.remove(&a);
	ASSERT((X *)j==&c);
	j++;
	ASSERT((X *)j==&b);
}

void listtests2(void)
{
	X a, b, c;
	XL list1, list2;
	XL::iterator i;
	
	list1.join_front(list2);
	ASSERT(list1.begin()==list1.end());
	
	list2.push_back(&a);
	list1.join_back(list2);
	
	i=list1.begin();
	ASSERT((X*)i==&a);
	i++;
	ASSERT(i==list1.end());
	
	ASSERT(list2.begin()==list2.end());
	
	list2.push_back(&b);
	list1.join_back(list2);
	i=list1.begin();
	ASSERT((X*)i==&a);
	i++;
	ASSERT((X*)i==&b);
	i++;
	ASSERT(i==list1.end());
	
	list2.push_back(&c);
	list1.join_front(list2);
	i=list1.begin();
	ASSERT((X*)i==&c);
	i++;
	ASSERT((X*)i==&a);
	i++;
	ASSERT((X*)i==&b);
	i++;
	ASSERT(i==list1.end());
}

int main(int argc, char **argv)
{
	listtests();
	listtests2();
	return 0;
}
