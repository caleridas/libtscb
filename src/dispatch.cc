/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#include <tscb/dispatch>

namespace tscb {
	
	void dispatch(tscb::timerqueue_dispatcher *tq,
		tscb::ioready_dispatcher *io) throw()
	{
		/* if there are no timers pending, avoid call to gettimeofday
		it is debatable whether this should be considered fast-path
		or not -- however a mispredicted branch is lost in the noise
		compared to the call to gettimeofday
		*/
		if (__builtin_expect(!tq->timers_pending(), true)) {
			io->dispatch(0);
			return;
		}
		
		long long now=current_time(), t;
		long long t=now;
		bool pending;
		do {
			t=now;
			pending=tq->run_queue(t);
			if (!pending) break;
			now=current_time();
		} while(now>=t);
		
		if (pending) {
			long long timeout=t-now;
			io->dispatch(&timeout);
		} else io->dispatch(0);
	}
	
};
