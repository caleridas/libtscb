/* -*- C++ -*-
 * (c) 2008 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#define private public
#define protected public

#include "tests.h"

#define FIBHEAP_DEBUG 1

#include <tscb/fibheap>

using namespace tscb;

class Node {
public:
	Node *next, *prev, *parent, *child;
	int degree;
	int value;
	
	inline bool operator>(const Node &x) const {return value>x.value;}
	inline bool operator<(const Node &x) const {return value<x.value;}
	inline bool operator<=(const Node &x) const {return value<=x.value;}
	
	inline Node(int _value) : value(_value) {}
};

void assert_fibheap_structure(fibheap<Node> &heap)
{
	heap.assert_structure();
}

void fibheap_tests(void)
{
	fibheap<Node> heap;
	
	Node a(1), b(2), c(3), d(4);
	
	heap.insert(&a);
	heap.insert(&d);
	heap.insert(&b);
	heap.insert(&c);
	assert_fibheap_structure(heap);
	
	ASSERT(heap.extract_min()==&a);
	assert_fibheap_structure(heap);
	
	heap.remove(&d);
	assert_fibheap_structure(heap);
	
	ASSERT(heap.extract_min()==&b);
	ASSERT(heap.extract_min()==&c);
}

int main()
{
	fibheap_tests();
	return 0;
}
