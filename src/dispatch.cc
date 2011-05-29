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
		timer_dispatcher(trigger),
		async_workqueue(io->get_eventtrigger())
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
	
	async_safe_connection
	posix_reactor::async_procedure(const boost::function<void(void)> & function)
	{
		return async_workqueue.async_procedure(function);
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
			mutex::guard guard(workqueue_lock);
			std::auto_ptr<workitem> item(workqueue.pop());
			guard.unlock();
			
			if (item.get())
				item->function();
			
			guard.lock();
			if (!workqueue.empty())
				trigger.set();
		}
		async_workqueue.dispatch();
		tscb::dispatch(&timer_dispatcher, io);
	}
	
	bool
	posix_reactor::dispatch_pending(void)
	{
		bool processed_events = false;
		
		if (__builtin_expect(!workqueue.empty(), 0)) {
			mutex::guard guard(workqueue_lock);
			std::auto_ptr<workitem> item(workqueue.pop());
			guard.unlock();
			
			if (item.get()) {
				item->function();
				processed_events = true;
			}
			
			guard.lock();
			if (!workqueue.empty())
				trigger.set();
		}
		
		if (async_workqueue.dispatch())
			processed_events = true;
		
		boost::posix_time::ptime first_timer_due;
		if (__builtin_expect(timer_dispatcher.next_timer(first_timer_due), false)) {
			boost::posix_time::ptime now = monotonic_time();
			
			if (first_timer_due <= now) {
				processed_events = true;
				
				timer_dispatcher.run_queue(now);
			}
		}
		
		if (io->dispatch_pending())
			processed_events = true;
		
		return processed_events;
	}
	
	void posix_reactor::dispatch_pending_all(void)
	{
		while (dispatch_pending()) {
			/* empty */
		}
	}
	
};
