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
		tscb::ioready_dispatcher *io)
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
		
		boost::posix_time::ptime now = monotonic_time();
		boost::posix_time::ptime t = now;
		bool pending;
		do {
			t = now;
			pending = tq->run_queue(t);
			if (!pending) break;
			now = monotonic_time();
		} while(now >= t);
		
		if (pending) {
			boost::posix_time::time_duration timeout = t-now;
			io->dispatch(&timeout);
		} else io->dispatch(0);
	}
	
	posix_reactor::posix_reactor(void)
		throw(std::bad_alloc, std::runtime_error)
		: io(create_ioready_dispatcher()),
		trigger(io->get_eventtrigger()),
		timer_dispatcher(trigger)
	{
	}
	
	posix_reactor::~posix_reactor(void) throw()
	{
		delete io;
	}
	
	void
	posix_reactor::post(const boost::function<void(void)> & function) /*throw(std::bad_alloc)*/
	{
		workitem * item = new workitem(function);
		try {
			mutex::guard guard(workqueue_lock);
			workqueue.push_back(item);
		}
		catch(...) {
			delete item;
			throw;
		}
		trigger.set();
	}
		
	void posix_reactor::register_timer(timer_callback * cb) throw()
	{
		timer_dispatcher.register_timer(cb);
	}
	
	void posix_reactor::unregister_timer(timer_callback * cb) throw()
	{
		timer_dispatcher.unregister_timer(cb);
	}
	
	void
	posix_reactor::register_ioready_callback(ioready_callback * cb) /*throw(std::bad_alloc)*/
	{
		io->register_ioready_callback(cb);
	}
	
	void
	posix_reactor::unregister_ioready_callback(ioready_callback * cb) throw()
	{
		io->unregister_ioready_callback(cb);
	}
	
	void
	posix_reactor::modify_ioready_callback(ioready_callback * cb, ioready_events event_mask) /*throw(std::bad_alloc)*/
	{
		io->modify_ioready_callback(cb, event_mask);
	}
	
	eventtrigger &
	posix_reactor::get_eventtrigger(void) /*throw(std::bad_alloc)*/
	{
		return trigger;
	}
	
	void
	posix_reactor::dispatch(void)
	{
		if (__builtin_expect(!workqueue.empty(), 0)) {
			workitem * item;
			workqueue_lock.lock();
			while( (item = workqueue.pop()) != 0) {
				workqueue_lock.unlock();
				try {
					item->function();
				}
				catch (...) {
					delete item;
					throw;
				}
				delete item;
				workqueue_lock.lock();
			}
			workqueue_lock.unlock();
		}
		tscb::dispatch(&timer_dispatcher, io);
	}
	
};
