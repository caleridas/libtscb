/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <boost/bind.hpp>

#include <sys/fcntl.h>
#include <string.h>

#include <tscb/ioready-poll>

namespace tscb {
	
	inline ioready_events ioready_dispatcher_poll::translate_os_to_tscb(int ev) throw()
	{
		ioready_events e = ioready_none;
		if (ev & POLLIN) e |= ioready_input;
		if (ev & POLLOUT) e |= ioready_output;
		/* deliver hangup event to input and output handlers as well */
		if (ev & POLLHUP) e |= (ioready_input|ioready_output|ioready_hangup|ioready_error);
		if (ev & POLLERR) e |= (ioready_input|ioready_output|ioready_error);
		return e;
	}
	
	inline int ioready_dispatcher_poll::translate_tscb_to_os(ioready_events ev) throw()
	{
		int e=0;
		if (ev & ioready_input) e |= POLLIN;
		if (ev & ioready_output) e |= POLLOUT;
		return e;
	}
	
	ioready_dispatcher_poll::polltab::polltab(size_t _size)
		throw(std::bad_alloc)
		: size(_size)
	{
		pfd = new pollfd[size];
		old = 0;
		peer = 0;
	}
	
	ioready_dispatcher_poll::polltab::~polltab(void)
		throw()
	{
		delete []pfd;
	}
	
	/* dispatcher_poll */
	
	ioready_dispatcher_poll::ioready_dispatcher_poll(void)
		/*throw(std::bad_alloc, std::runtime_error)*/
		: master_ptab(new polltab(0))
	{
		try {
			pipe_callback = watch(
				boost::bind(&ioready_dispatcher_poll::drain_queue, this),
				wakeup_flag.readfd, ioready_input);
		}
		catch (std::bad_alloc) {
			delete master_ptab.load(memory_order_relaxed);
			throw;
		}
	}
	
	ioready_dispatcher_poll::~ioready_dispatcher_poll(void) throw()
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
		
		/* FIXME: all other ptabs */
		delete master_ptab.load(memory_order_relaxed);
	}
	
	eventtrigger & ioready_dispatcher_poll::get_eventtrigger(void) throw()
	{
		return wakeup_flag;
	}
	
	size_t ioready_dispatcher_poll::dispatch(const boost::posix_time::time_duration *timeout, size_t max)
	{
		read_guard<ioready_dispatcher_poll> guard(*this);
		
		uint32_t cookie = fdtab.get_cookie();
		
		polltab * ptab = master_ptab.load(memory_order_consume);
		
		int count, handled = 0;
		
		int poll_timeout;
		
		/* need to round up timeout; alas this is the only good way to do it in boost */
		if (timeout) poll_timeout = (timeout->total_microseconds() + 999) / 1000;
		else poll_timeout = -1;
		
		wakeup_flag.start_waiting();
		
		if (wakeup_flag.flagged.load(memory_order_relaxed) != 0)
			poll_timeout = 0;
		
		count = poll(ptab->pfd, ptab->size, poll_timeout);
		
		wakeup_flag.stop_waiting();
		
		if (count < 0) count = 0;
		if ((size_t)count > max) count = max;
		int n=0;
		while(count) {
			if (ptab->pfd[n].revents) {
				int fd = ptab->pfd[n].fd;
				ioready_events ev = translate_os_to_tscb(ptab->pfd[n].revents);
				fdtab.notify(fd, ev, cookie);
				
				count--;
				handled++;
			}
			n++;
		}
		
		wakeup_flag.clear();
		
		if (lock.read_unlock()) synchronize();
		
		return handled;
	}
	
	size_t ioready_dispatcher_poll::dispatch_pending(size_t max)
	{
		read_guard<ioready_dispatcher_poll> guard(*this);
		
		polltab * ptab = master_ptab.load(memory_order_consume);
		
		ssize_t count;
		size_t handled = 0;
		
		uint32_t cookie = fdtab.get_cookie();
		
		count = poll(ptab->pfd, ptab->size, 0);
		
		if (count < 0) count = 0;
		if ((size_t)count > max) count = max;
		
		size_t n = 0;
		while(count) {
			if (ptab->pfd[n].revents) {
				int fd = ptab->pfd[n].fd;
				ioready_events ev = translate_os_to_tscb(ptab->pfd[n].revents);
				fdtab.notify(fd, ev, cookie);
				
				count--;
				handled++;
			}
			n++;
		}
		
		wakeup_flag.clear();
		
		if (lock.read_unlock()) synchronize();
		
		return handled;
	}
	
	void ioready_dispatcher_poll::synchronize(void) throw()
	{
		ioready_callback *stale = fdtab.synchronize();
		
		polltab * ptab = master_ptab.load(memory_order_relaxed);
		polltab * discard_ptab = ptab->old;
		ptab->old = 0;
		
		lock.sync_finished();
		
		while(stale) {
			ioready_callback * next = stale->inactive_next;
			stale->cancelled();
			stale->release();
			stale = next;
		}
		
		while(discard_ptab) {
			polltab * next = discard_ptab->old;
			delete discard_ptab;
			discard_ptab = next;
		}
	}
	
	void ioready_dispatcher_poll::update_polltab_entry(int fd, ioready_events mask) /*throw(std::bad_alloc)*/
	{
		polltab * old_ptab = master_ptab.load(memory_order_relaxed);
		int index = -1;
		
		if ( ((size_t)fd) < polltab_index.size() ) index = polltab_index[fd];
		
		if (index == -1) {
			if (!mask) return;
			
			/* no entry so far, just create new one */
			while (polltab_index.size() <= (size_t) fd)
				polltab_index.push_back(-1);
			
			polltab * p = new polltab(old_ptab->size + 1);
			for (size_t n = 0; n < old_ptab->size; n++) {
				p->pfd[n].fd = old_ptab->pfd[n].fd;
				p->pfd[n].events = old_ptab->pfd[n].events;
			}
			
			p->pfd[p->size-1].fd = fd;
			p->pfd[p->size-1].events = translate_tscb_to_os(mask);
			
			polltab_index[fd] = p->size - 1;
			p->old = old_ptab;
			
			master_ptab.store(p, memory_order_release);
			
			return;
		}
		
		if (mask) {
			old_ptab->pfd[index].events = translate_tscb_to_os(mask);
			
			return;
		}
		
		polltab * p = new polltab(old_ptab->size - 1);
		for(size_t n = 0; n<p->size; n++) {
			p->pfd[n].fd = old_ptab->pfd[n].fd;
			p->pfd[n].events = old_ptab->pfd[n].events;
		}
		
		if (p->size > (size_t) index) {
			/* unless deleting last element, move last element into vacant position */
			p->pfd[index].fd = old_ptab->pfd[old_ptab->size -1].fd;
			p->pfd[index].events = old_ptab->pfd[old_ptab->size -1].events;
		}
		
		polltab_index[fd] = -1;
		p->old = old_ptab;
		master_ptab.store(p, memory_order_release);
	}
	
	void ioready_dispatcher_poll::register_ioready_callback(ioready_callback *link)
		/*throw(std::bad_alloc)*/
	{
		{
			async_write_guard<ioready_dispatcher_poll> guard(*this);
			
			try {
				ioready_events old_mask, new_mask;
				fdtab.insert(link, old_mask, new_mask);
				if (old_mask != new_mask)
					update_polltab_entry(link->fd, new_mask);
			}
			catch (std::bad_alloc) {
				delete link;
				throw;
			}
			
			link->service.store(this, memory_order_relaxed);
		}
		
		wakeup_flag.set();
	}
	
	void ioready_dispatcher_poll::unregister_ioready_callback(ioready_callback *link)
		throw()
	{
		{
			async_write_guard<ioready_dispatcher_poll> guard(*this);
			
			if (link->service.load(memory_order_relaxed)) {
				ioready_events old_mask, new_mask;
				fdtab.remove(link, old_mask, new_mask);
				if (old_mask != new_mask) update_polltab_entry(link->fd, new_mask);
				
				link->service.store(0, memory_order_relaxed);
			}
			
			link->cancellation_mutex.unlock();
		}
		
		wakeup_flag.set();
	}
	
	void ioready_dispatcher_poll::modify_ioready_callback(ioready_callback *link, ioready_events event_mask)
	{
		{
			async_write_guard<ioready_dispatcher_poll> guard(*this);
			
			ioready_events old_mask = link->event_mask;
			link->event_mask = event_mask;
			ioready_events new_events = fdtab.compute_mask(link->fd);
			try {
				update_polltab_entry(link->fd, new_events);
			}
			catch(std::bad_alloc) {
				link->event_mask = old_mask;
				throw;
			}
		}
		
		wakeup_flag.set();
	}
	
	void ioready_dispatcher_poll::drain_queue(void) throw()
	{
	}
	
	ioready_dispatcher *
	create_ioready_dispatcher_poll(void) throw(std::bad_alloc, std::runtime_error)
	{
		return new ioready_dispatcher_poll();
	}
	
}
