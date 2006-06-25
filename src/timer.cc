/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <sys/time.h>
#include <time.h>

#include <tscb/timer>

namespace tscb {
	
	long long current_time(void) throw()
	{
		struct timeval tv;
		
		gettimeofday(&tv, 0);
		return tv.tv_usec+tv.tv_sec*1000000LL;
	}
	
}
