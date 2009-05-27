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
		throw(std::bad_alloc, std::runtime_error)
		: maxfd(0)
	{
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_ZERO(&exceptfds);
		try {
			watch(boost::bind(&ioready_dispatcher_select::drain_queue, this),
				wakeup_flag.readfd, EVMASK_INPUT);
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
		
		while(guard.read_lock()) synchronize();
		callback_tab.cancel_all();
		if (guard.read_unlock()) {
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
			
			guard.write_lock_sync();
			synchronize();
			
			/* note that synchronize implicitly calls sync_finished,
			which is equivalent to write_unlock_sync for deferrable_rwlocks */
		}
	}
	
	eventflag *ioready_dispatcher_select::get_eventflag(void) throw()
	{
		return &wakeup_flag;
	}
	
	/* this assumes that fd_set *really* is a bitfield -- which does
	not necessarily have to be true */
	static void copy_fdset(fd_set *dst, const fd_set *src, int maxfd)
	{
		int copybytes=((maxfd+63)/64)*8;
		memcpy(dst, src, copybytes);
	}
	
	int ioready_dispatcher_select::dispatch(const boost::posix_time::time_duration *timeout, int max)
		throw()
	{
		while(guard.read_lock()) synchronize();
		
		fd_set l_readfds, l_writefds, l_exceptfds;
		
		wakeup_flag.start_waiting();
		
		copy_fdset(&l_readfds, &readfds, maxfd);
		copy_fdset(&l_writefds, &writefds, maxfd);
		copy_fdset(&l_exceptfds, &exceptfds, maxfd);
		
		int count, handled=0;
		struct timeval tv, *select_timeout;
		if (timeout) {
			tv.tv_sec=timeout->total_seconds();
			tv.tv_usec=timeout->total_microseconds()%1000000;
			select_timeout=&tv;
		} else select_timeout=0;
		
		if (wakeup_flag.flagged!=0) {
			tv.tv_sec=0;
			tv.tv_usec=0;
			select_timeout=&tv;
		}
		
		count=select(maxfd, &l_readfds, &l_writefds, &l_exceptfds, select_timeout);
		
		wakeup_flag.stop_waiting();
		
		if (count<0) count=0;
		if (count>max) count=max;
		int n=0;
		while(count) {
			int r=FD_ISSET(n, &l_readfds);
			int w=FD_ISSET(n, &l_writefds);
			int e=FD_ISSET(n, &l_exceptfds);
			if (r | w | e) {
				int ev=0;
				if (r) ev=EVMASK_INPUT;
				if (w) ev|=EVMASK_OUTPUT;
				/* deliver exception events to everyone */
				if (e) ev|=EVMASK_OUTPUT|EVMASK_INPUT|EVMASK_HANGUP;
				
				ioready_callback_link *link=
					callback_tab.lookup_first_callback(n);
				while(link) {
					data_dependence_memory_barrier();
					if (ev&link->event_mask) {
						link->target(ev&link->event_mask);
					}
					link=link->active_next;
				}
				count--;
				handled++;
			}
			n++;
		}
		
		if (guard.read_unlock()) synchronize();
		
		wakeup_flag.clear();
		
		return handled;
	}
	
	void ioready_dispatcher_select::register_ioready_callback(ioready_callback_link *link)
		throw(std::bad_alloc)
	{
		if (link->fd>=(int)FD_SETSIZE) {
			delete link;
			throw std::bad_alloc();
		}
		
		bool sync=guard.write_lock_async();
		
		try {
			callback_tab.insert(link);
		}
		catch (std::bad_alloc) {
			if (sync) synchronize();
			else guard.write_unlock_async();
			delete link;
			throw;
		}
		
		update_fdsets(link->fd);
		
		if (link->fd>=maxfd) maxfd=link->fd+1;
		
		link->service=this;
		
		if (sync) synchronize();
		else guard.write_unlock_async();
		
		wakeup_flag.set();
	}
	
	void ioready_dispatcher_select::unregister_ioready_callback(ioready_callback_link *link)
		throw()
	{
		bool sync=guard.write_lock_async();
		
		if (link->service) {
			int fd=link->fd;
			
			callback_tab.remove(link);
			
			update_fdsets(fd);
			
			if (callback_tab.chain_empty(fd)) {
				if (fd==maxfd+1) {
					do {
						maxfd--;
						if (!maxfd) break;
						if (FD_ISSET(maxfd-1, &readfds)) break;
						if (FD_ISSET(maxfd-1, &writefds)) break;
						if (FD_ISSET(maxfd-1, &exceptfds)) break;
					} while(true);
				}
			}
			
			link->service=0;
		}
		
		link->cancellation_mutex.unlock();
		
		if (sync) synchronize();
		else guard.write_unlock_async();
		
		wakeup_flag.set();
	}
	
	void ioready_dispatcher_select::modify_ioready_callback(ioready_callback_link *link, int event_mask)
		throw()
	{
		bool sync=guard.write_lock_async();
		
		link->event_mask=event_mask;
		
		update_fdsets(link->fd);
		
		if (sync) synchronize();
		else guard.write_unlock_async();
		
		wakeup_flag.set();
	}
	
	void ioready_dispatcher_select::update_fdsets(int fd) throw()
	{
		ioready_callback_link *tmp=callback_tab.lookup_first_callback(fd);
		int evmask=0;
		while(tmp) {
			evmask|=tmp->event_mask;
			tmp=tmp->active_next;
		}
		if (evmask&EVMASK_INPUT) FD_SET(fd, &readfds);
		else FD_CLR(fd, &readfds);
		if (evmask&EVMASK_OUTPUT) FD_SET(fd, &writefds);
		else FD_CLR(fd, &writefds);
		if (evmask&EVMASK_HANGUP) FD_SET(fd, &exceptfds);
		else FD_CLR(fd, &exceptfds);
	}
	
	void ioready_dispatcher_select::synchronize(void) throw()
	{
		ioready_callback_link *stale=callback_tab.synchronize();
		
		guard.sync_finished();
		
		while(stale) {
			ioready_callback_link *next=stale->inactive_next;
			stale->cancelled();
			stale->release();
			stale=next;
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
