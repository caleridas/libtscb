/*
 * (c) 2004 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/thread>

namespace tscb {
	
	void *__thread_wrapper(void *arg) throw()
	{
		thread *t=(thread *)arg;
		return t->thread_func();
	}
	
	thread::~thread(void) throw()
	{
	}
	
}
