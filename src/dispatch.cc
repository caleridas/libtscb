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
		
		boost::posix_time::ptime now=boost::posix_time::microsec_clock::universal_time();
		boost::posix_time::ptime t=now;
		bool pending;
		do {
			t=now;
			pending=tq->run_queue(t);
			if (!pending) break;
			now=boost::posix_time::microsec_clock::universal_time();
		} while(now>=t);
		
		if (pending) {
			boost::posix_time::time_duration timeout=t-now;
			io->dispatch(&timeout);
		} else io->dispatch(0);
	}
	
	workqueue_service::~workqueue_service(void) throw()
	{
	}
	
	posix_event_service::~posix_event_service(void) throw()
	{
	}
	
	posix_event_dispatcher::posix_event_dispatcher(void)
		throw(std::bad_alloc, std::runtime_error)
		: io(create_ioready_dispatcher()),
		flag(io->get_eventflag()),
		timer(flag)
	{
	}
	
	posix_event_dispatcher::~posix_event_dispatcher(void) throw()
	{
		delete io;
	}
	
	void
	posix_event_dispatcher::post(const boost::function<void(void)> &function) throw(std::bad_alloc)
	{
		workqueue_lock.lock();
		workqueue.push_back(function);
		workqueue_lock.unlock();
		flag->set();
	}
		
	void posix_event_dispatcher::register_timer(timer_callback *ptr) throw()
	{
		timer.register_timer(ptr);
	}
	
	void posix_event_dispatcher::unregister_timer(timer_callback *t) throw()
	{
		timer.unregister_timer(t);
	}
	
	void
	posix_event_dispatcher::register_ioready_callback(ioready_callback *l) throw(std::bad_alloc)
	{
		io->register_ioready_callback(l);
	}
	
	void
	posix_event_dispatcher::unregister_ioready_callback(ioready_callback *e) throw()
	{
		io->unregister_ioready_callback(e);
	}
	
	void
	posix_event_dispatcher::modify_ioready_callback(ioready_callback *e, ioready_events event_mask) throw()
	{
		io->modify_ioready_callback(e, event_mask);
	}
	
	eventflag *
	posix_event_dispatcher::get_eventflag(void) throw(std::bad_alloc)
	{
		return flag;
	}
	void
	posix_event_dispatcher::dispatch(void)
	{
		if (__builtin_expect(!workqueue.empty(), 0)) {
			workitems items;
			workqueue_lock.lock();
			items.swap(workqueue);
			workqueue_lock.unlock();
			
			for(workitems::const_iterator i=items.begin(); i!=items.end(); ++i)
				(*i)();
		}
		tscb::dispatch(&timer, io);
	}
	
};
