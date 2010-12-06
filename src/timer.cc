/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <sys/time.h>
#include <time.h>

#include <boost/date_time/posix_time/conversion.hpp>

#include <tscb/timer>

namespace tscb {
	
	boost::posix_time::ptime monotonic_time(void) throw()
	{
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return boost::posix_time::from_time_t(ts.tv_sec) + boost::posix_time::microsec(ts.tv_nsec/1000);
	}
	
}
