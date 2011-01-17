/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <boost/bind.hpp>

#include <string.h>

#include <tscb/ioready-select>

namespace tscb {
	
	ioready_dispatcher_select::ioready_dispatcher_select(void)
		/* throw(std::bad_alloc, std::runtime_error) */
		: maxfd(0)
	{
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_ZERO(&exceptfds);
		
		pthread_mutex_init(&fdset_mtx, NULL);
		
		try {
			watch(boost::bind(&ioready_dispatcher_select::drain_queue, this),
				wakeup_flag.readfd, ioready_input);
		}
		catch (std::bad_alloc) {
			throw;
		}
		
	}
	
	ioready_dispatcher_select::~ioready_dispatcher_select(void) throw()
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
	}
	
	eventflag & ioready_dispatcher_select::get_eventflag(void) throw()
	{
		return wakeup_flag;
	}
	
	/* this assumes that fd_set *really* is a bitfield -- which does
	not necessarily have to be true */
	static void copy_fdset(fd_set *dst, const fd_set *src, int maxfd)
	{
		int copybytes = ((maxfd + 63) / 64) * 8;
		memcpy(dst, src, copybytes);
	}
	
	int ioready_dispatcher_select::dispatch(const boost::posix_time::time_duration *timeout, int max)
	{
		read_guard<ioready_dispatcher_select> guard(*this);
		
		fd_set l_readfds, l_writefds, l_exceptfds;
		int l_maxfd;
		
		pthread_mutex_lock(&fdset_mtx);
		l_maxfd = maxfd;
		copy_fdset(&l_readfds, &readfds, maxfd);
		copy_fdset(&l_writefds, &writefds, maxfd);
		copy_fdset(&l_exceptfds, &exceptfds, maxfd);
		pthread_mutex_unlock(&fdset_mtx);
		
		int count, handled=0;
		struct timeval tv, *select_timeout;
		if (timeout) {
			tv.tv_sec = timeout->total_microseconds()/1000000;
			tv.tv_usec = timeout->total_microseconds()%1000000;
			select_timeout = &tv;
		} else select_timeout = 0;
		
		wakeup_flag.start_waiting();
		
		if (wakeup_flag.flagged.load(memory_order_relaxed) != 0) {
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			select_timeout = &tv;
		}
		
		count = select(l_maxfd, &l_readfds, &l_writefds, &l_exceptfds, select_timeout);
		
		wakeup_flag.stop_waiting();
		
		if (count<0) count = 0;
		if (count>max) count = max;
		int n = 0;
		while(count) {
			int r = FD_ISSET(n, &l_readfds);
			int w = FD_ISSET(n, &l_writefds);
			int e = FD_ISSET(n, &l_exceptfds);
			if (r | w | e) {
				ioready_events ev = ioready_none;
				if (r) ev = ioready_input;
				if (w) ev |= ioready_output;
				/* deliver exception events to everyone */
				if (e) ev |= ioready_error|ioready_input|ioready_output;
				
				fdtab.notify(n, ev);
				count--;
				handled++;
			}
			n++;
		}
		
		wakeup_flag.clear();
		
		return handled;
	}
	
	void ioready_dispatcher_select::register_ioready_callback(ioready_callback *link)
		/*throw(std::bad_alloc)*/
	{
		if (link->fd >= (int)FD_SETSIZE) {
			delete link;
			throw std::bad_alloc();
		}
		
		{
			async_write_guard<ioready_dispatcher_select> guard(*this);
			
			ioready_events old_events, new_events;
			try {
				fdtab.insert(link, old_events, new_events);
			}
			catch (std::bad_alloc) {
				delete link;
				throw;
			}
			update_fdsets(link->fd, new_events);
			
			link->service.store(this, memory_order_relaxed);
		}
		
		wakeup_flag.set();
	}
	
	void ioready_dispatcher_select::unregister_ioready_callback(ioready_callback *link)
		throw()
	{
		{
			async_write_guard<ioready_dispatcher_select> guard(*this);
			
			if (link->service.load(memory_order_relaxed)) {
				ioready_events old_events, new_events;
				fdtab.remove(link, old_events, new_events);
				update_fdsets(link->fd, new_events);
				
				link->service.store(0, memory_order_relaxed);
			}
			
			link->cancellation_mutex.unlock();
		}
		
		wakeup_flag.set();
	}
	
	void ioready_dispatcher_select::modify_ioready_callback(ioready_callback *link, ioready_events event_mask)
		/*throw(std::bad_alloc)*/
	{
		{
			async_write_guard<ioready_dispatcher_select> guard(*this);
			
			link->event_mask = event_mask;
			ioready_events new_events = fdtab.compute_mask(link->fd);
			update_fdsets(link->fd, new_events);
		}
		
		wakeup_flag.set();
	}
	
	void ioready_dispatcher_select::update_fdsets(int fd, ioready_events mask) throw()
	{
		pthread_mutex_lock(&fdset_mtx);
		if (mask & ioready_input) FD_SET(fd, &readfds);
		else FD_CLR(fd, &readfds);
		if (mask & ioready_output) FD_SET(fd, &writefds);
		else FD_CLR(fd, &writefds);
		if (mask) FD_SET(fd, &exceptfds);
		else FD_CLR(fd, &exceptfds);
		
		if (mask) {
			if (fd >= maxfd) maxfd = fd + 1;
		} else if (fd == maxfd - 1) {
			for(;;) {
				maxfd--;
				if (!maxfd) break;
				if (FD_ISSET(maxfd-1, &readfds)) break;
				if (FD_ISSET(maxfd-1, &writefds)) break;
				if (FD_ISSET(maxfd-1, &exceptfds)) break;
			}
		}
		pthread_mutex_unlock(&fdset_mtx);
	}
	
	void ioready_dispatcher_select::synchronize(void) throw()
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
	
	void ioready_dispatcher_select::drain_queue(void) throw()
	{
	}
	
	ioready_dispatcher *
	create_ioready_dispatcher_select(void) throw(std::bad_alloc, std::runtime_error)
	{
		return new ioready_dispatcher_select();
	}
	
}
