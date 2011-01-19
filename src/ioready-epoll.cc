/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <boost/bind.hpp>

#include <sys/fcntl.h>
#include <sys/epoll.h>

#include <tscb/ioready-epoll>

namespace tscb {
	
	inline ioready_events ioready_dispatcher_epoll::translate_os_to_tscb(int ev) throw()
	{
		ioready_events e = ioready_none;
		if (ev & EPOLLIN) e |= ioready_input;
		if (ev & EPOLLOUT) e |= ioready_output;
		/* deliver hangup event to input and output handlers as well */
		if (ev & EPOLLHUP) e |= ioready_input|ioready_output|ioready_hangup|ioready_error;
		if (ev & EPOLLERR) e |= ioready_input|ioready_output|ioready_error;
		return e;
	}
	
	inline int ioready_dispatcher_epoll::translate_tscb_to_os(ioready_events ev) throw()
	{
		int e = 0;
		if (ev & ioready_input) e |= EPOLLIN;
		if (ev & ioready_output) e |= EPOLLOUT;
		return e;
	}
	
	ioready_dispatcher_epoll::ioready_dispatcher_epoll(void)
		/* throw(std::runtime_error) */
		: wakeup_flag(0)
	{
		epoll_fd = epoll_create(1024);
		if (epoll_fd<0) throw std::runtime_error("Unable to create epoll descriptor");
	}
	
	ioready_dispatcher_epoll::~ioready_dispatcher_epoll(void) throw()
	{
		/* we can assume
		
		- no thread is actively dispatching at the moment
		- no user can register new callbacks at the moment
		
		if those conditions are not met, we are in big trouble anyway, and
		there is no point doing anything about it
		*/
		
		while(lock.read_lock()) synchronize();
		fdtab.cancel_all();
		if (lock.read_unlock()) {
			/* the above cancel operations will cause synchronization
			to be performed at the next possible point in time; if
			there is no concurrent cancellation, this is now */
			synchronize();
		} else {
			/* this can only happen if some callback link was
			cancelled while this object is being destroyed; in
			that case we have to suspend the thread that is destroying
			the object until we are certain that synchronization has
			been performed */
			
			lock.write_lock_sync();
			synchronize();
			
			/* note that synchronize implicitly calls sync_finished,
			which is equivalent to write_unlock_sync for deferrable_rwlocks */
		}
		
		close(epoll_fd);
		
		if (wakeup_flag.load(memory_order_relaxed)) delete wakeup_flag.load(memory_order_relaxed);
	}
	
	void ioready_dispatcher_epoll::process_events(epoll_event events[], size_t nevents)
	{
		read_guard<ioready_dispatcher_epoll> guard(*this);
		
		for(size_t n=0; n<nevents; n++) {
			int fd = events[n].data.fd;
			ioready_events ev = translate_os_to_tscb(events[n].events);
			
			fdtab.notify(fd, ev);
		}
	}
	
	int ioready_dispatcher_epoll::dispatch(const boost::posix_time::time_duration *timeout, int max)
	{
		pipe_eventflag *evflag = wakeup_flag.load(memory_order_consume);
		
		int poll_timeout;
		/* need to round up timeout; alas this is the only good way to do it in boost */
		if (timeout) poll_timeout = (timeout->total_microseconds() + 999) / 1000;
		else poll_timeout = -1;
		
		if (max > 16) max = 16;
		epoll_event events[max];
		
		ssize_t nevents;
		
		if (__builtin_expect(evflag == 0, 1)) {
			nevents = epoll_wait(epoll_fd, events, max, poll_timeout);
			
			if (nevents > 0) process_events(events, nevents);
			else nevents = 0;
		} else {
			evflag->start_waiting();
			if (evflag->flagged.load(memory_order_relaxed) != 0) poll_timeout = 0;
			nevents = epoll_wait(epoll_fd, events, max, poll_timeout);
			evflag->stop_waiting();
			
			if (nevents > 0) process_events(events, nevents);
			else nevents = 0;
			
			evflag->clear();
		}
		return nevents;
	}
	
	eventtrigger & ioready_dispatcher_epoll::get_eventtrigger(void)
		/* throw(std::runtime_error, std::bad_alloc) */
	{
		pipe_eventflag * flag = wakeup_flag.load(memory_order_consume);
		if (flag) return *flag;
		
		singleton_mutex.lock();
		flag = wakeup_flag.load(memory_order_consume);
		if (flag) {
			singleton_mutex.unlock();
			return *flag;
		}
		
		try {
			flag = new pipe_eventflag();
			watch(boost::bind(&ioready_dispatcher_epoll::drain_queue, this), flag->readfd, ioready_input);
		}
		catch (std::bad_alloc) {
			delete flag;
			singleton_mutex.unlock();
			throw;
		}
		catch (std::runtime_error) {
			delete flag;
			singleton_mutex.unlock();
			throw;
		}
		
		wakeup_flag.store(flag, memory_order_release);
		singleton_mutex.unlock();
		
		return *flag;
	}
		
	void ioready_dispatcher_epoll::synchronize(void) throw()
	{
		ioready_callback * stale = fdtab.synchronize();
		lock.sync_finished();
		
		while(stale) {
			ioready_callback * next = stale->inactive_next;
			stale->cancelled();
			stale->release();
			stale = next;
		}
	}
	
	void ioready_dispatcher_epoll::register_ioready_callback(ioready_callback *link)
		/*throw(std::bad_alloc)*/
	{
		async_write_guard<ioready_dispatcher_epoll> guard(*this);
		
		ioready_events old_mask, new_mask;
		
		try {
			fdtab.insert(link, old_mask, new_mask);
		}
		catch (std::bad_alloc) {
			delete link;
			throw;
		}
		
		if (new_mask != ioready_none) {
			epoll_event event;
			event.events = translate_tscb_to_os(new_mask);
			event.data.u64 = 0;
			event.data.fd = link->fd;
			
			if (old_mask)
				epoll_ctl(epoll_fd, EPOLL_CTL_MOD, link->fd, &event);
			else
				epoll_ctl(epoll_fd, EPOLL_CTL_ADD, link->fd, &event);
		}
		
		link->service.store(this, memory_order_relaxed);
	}
	
	void ioready_dispatcher_epoll::unregister_ioready_callback(ioready_callback *link)
		throw()
	{
		async_write_guard<ioready_dispatcher_epoll> guard(*this);
		
		if (link->service.load(memory_order_relaxed)) {
			int fd = link->fd;
			ioready_events old_mask, new_mask;
			fdtab.remove(link, old_mask, new_mask);
			
			if (old_mask) {
				epoll_event event;
				event.data.u64 = 0;
				event.data.fd = fd;
				int op;
				if (new_mask) {
					event.events = translate_tscb_to_os(new_mask);
					op = EPOLL_CTL_ADD;
				} else {
					event.events = translate_tscb_to_os(old_mask);
					op = EPOLL_CTL_DEL;
				}
				epoll_ctl(epoll_fd, op, fd, &event);
			}
			
			link->service.store(0, memory_order_relaxed);
		}
		
		link->cancellation_mutex.unlock();
	}
	
	void ioready_dispatcher_epoll::modify_ioready_callback(ioready_callback *link, ioready_events event_mask)
		/*throw(std::bad_alloc)*/
	{
		async_write_guard<ioready_dispatcher_epoll> guard(*this);
		
		ioready_events old_mask = fdtab.compute_mask(link->fd);
		link->event_mask = event_mask;
		ioready_events new_mask = fdtab.compute_mask(link->fd);
		
		if (old_mask != new_mask) {
			epoll_event event;
			event.data.u64 = 0;
			event.data.fd = link->fd;
			int op;
			
			if (old_mask) {
				if (new_mask) {
					event.events = translate_tscb_to_os(new_mask);
					op = EPOLL_CTL_MOD;
				} else {
					event.events = translate_tscb_to_os(old_mask);
					op = EPOLL_CTL_DEL;
				}
			} else {
				event.events = translate_tscb_to_os(new_mask);
				op = EPOLL_CTL_ADD;
			}
			epoll_ctl(epoll_fd, op, link->fd, &event);
		}
	}
	
	void ioready_dispatcher_epoll::drain_queue(void) throw()
	{
	}
	
	ioready_dispatcher *
	create_ioready_dispatcher_epoll(void) throw(std::bad_alloc, std::runtime_error)
	{
		return new ioready_dispatcher_epoll();
	}
	
}
