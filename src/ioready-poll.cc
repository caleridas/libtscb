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
	
	inline int ioready_dispatcher_poll::translate_os_to_tscb(int ev) throw()
	{
		int e=0;
		if (ev&POLLIN) e|=EVMASK_INPUT;
		if (ev&POLLOUT) e|=EVMASK_OUTPUT;
		/* deliver hangup event to input and output handlers as well */
		if (ev&POLLHUP) e|=EVMASK_HANGUP|EVMASK_INPUT|EVMASK_OUTPUT;
		return e;
	}
	
	inline int ioready_dispatcher_poll::translate_tscb_to_os(int ev) throw()
	{
		int e=0;
		if (ev&EVMASK_INPUT) e|=POLLIN;
		if (ev&EVMASK_OUTPUT) e|=POLLOUT;
		if (ev&EVMASK_HANGUP) e|=POLLHUP;
		return e;
	}
	
	ioready_dispatcher_poll::polltab::polltab(size_t _size)
		throw(std::bad_alloc)
		: size(_size)
	{
		pfd=new pollfd[size];
		old=0;
		peer=0;
	}
	
	ioready_dispatcher_poll::polltab::~polltab(void)
		throw()
	{
		delete []pfd;
	}
	
	/* dispatcher_poll */
	
	ioready_dispatcher_poll::ioready_dispatcher_poll(void)
		throw(std::bad_alloc, std::runtime_error)
		: master_ptab(0)
	{
		try {
			master_ptab=new polltab(0);
			
			pipe_callback=watch(
				boost::bind(&ioready_dispatcher_poll::drain_queue, this),
				wakeup_flag.readfd, EVMASK_INPUT);
		}
		catch (std::bad_alloc) {
			delete master_ptab;
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
		
		/* FIXME: all other ptabs */
		delete master_ptab;
	}
	
	eventflag *ioready_dispatcher_poll::get_eventflag(void) throw()
	{
		return &wakeup_flag;
	}
	
	int ioready_dispatcher_poll::dispatch(const boost::posix_time::time_duration *timeout, int max)
		throw()
	{
		while(guard.read_lock()) synchronize();
		
		wakeup_flag.start_waiting();
		
		polltab *ptab=master_ptab;
		
		int count, handled=0;
		
		int poll_timeout;
		
		if (timeout) poll_timeout=timeout->total_milliseconds();
		else poll_timeout=-1;
		
		if (wakeup_flag.flagged!=0) poll_timeout=0;
		
		count=poll(ptab->pfd, ptab->size, poll_timeout);
		
		wakeup_flag.stop_waiting();
		
		if (count<0) count=0;
		if (count>max) count=max;
		int n=0;
		while(count) {
			if (ptab->pfd[n].revents) {
				int fd=ptab->pfd[n].fd;
				int ev=translate_os_to_tscb(ptab->pfd[n].revents);
				
				ioready_callback *link=
					callback_tab.lookup_first_callback(fd);
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
	
	void ioready_dispatcher_poll::synchronize(void) throw()
	{
		ioready_callback *stale=callback_tab.synchronize();
		
		polltab *discard_ptab=master_ptab->old;
		master_ptab->old=0;
		
		guard.sync_finished();
		
		while(stale) {
			ioready_callback *next=stale->inactive_next;
			stale->cancelled();
			stale->release();
			stale=next;
		}
		
		while(discard_ptab) {
			polltab *next=discard_ptab->old;
			delete discard_ptab;
			discard_ptab=next;
		}
	}
	
	ioready_dispatcher_poll::polltab *
	ioready_dispatcher_poll::clone_polltab_for_extension(void)
		throw(std::bad_alloc)
	{
		polltab *ptab=new polltab(master_ptab->size+1);
		memcpy(ptab->pfd, master_ptab->pfd, sizeof(struct pollfd)*master_ptab->size);
		
		ptab->old=master_ptab;
		ptab->generation=master_ptab->generation+1;
		
		return ptab;
	}
	
	void ioready_dispatcher_poll::create_polltab_entry(ioready_callback *link)
		throw(std::bad_alloc)
	{
		polltab *p=clone_polltab_for_extension();
		
		p->pfd[p->size-1].fd=link->fd;
		p->pfd[p->size-1].events=translate_tscb_to_os(link->event_mask);
		
		callback_tab.set_closure(link->fd, (void *)(p->size-1));
		
		memory_barrier();
		
		master_ptab=p;
	}
	
	void ioready_dispatcher_poll::update_polltab_entry(int fd) throw()
	{
		int index=(long)callback_tab.get_closure(fd);
		
		int newevmask=0;
		ioready_callback *tmp=callback_tab.lookup_first_callback(fd);
		while(tmp) {
			newevmask|=translate_tscb_to_os(tmp->event_mask);
			tmp=tmp->active_next;
		}
		
		master_ptab->pfd[index].events=newevmask;
	}
	
	void ioready_dispatcher_poll::remove_polltab_entry(int fd)
		throw()
	{
		int index=(long)callback_tab.get_closure(fd);
		
		callback_tab.set_closure(fd, (void *)-1);
		
		/* move entry from end of list to fill gap */
		master_ptab->pfd[index]=master_ptab->pfd[master_ptab->size-1];
		master_ptab->size--;
	}
	
	void ioready_dispatcher_poll::register_ioready_callback(ioready_callback *link)
		throw(std::bad_alloc)
	{
		bool sync=guard.write_lock_async();
		
		bool empty_chain=callback_tab.chain_empty(link->fd);
		
		try {
			callback_tab.insert(link);
		}
		catch (std::bad_alloc) {
			if (sync) synchronize();
			else guard.write_unlock_async();
			delete link;
			throw;
		}
		
		if (empty_chain) {
			try {
				create_polltab_entry(link);
			}
			catch(std::bad_alloc) {
				callback_tab.remove(link);
				if (sync) synchronize();
				else guard.write_unlock_async();
				throw;
			}
		} else update_polltab_entry(link->fd);
		
		link->service=this;
		
		if (sync) synchronize();
		else guard.write_unlock_async();
		
		wakeup_flag.set();
	}
	
	void ioready_dispatcher_poll::unregister_ioready_callback(ioready_callback *link)
		throw()
	{
		bool sync=guard.write_lock_async();
		
		if (link->service) {
			int fd=link->fd;
			
			callback_tab.remove(link);
			
			if (callback_tab.chain_empty(fd))
				remove_polltab_entry(fd);
			else
				update_polltab_entry(fd);
			link->service=0;
		}
		
		link->cancellation_mutex.unlock();
		
		if (sync) synchronize();
		else guard.write_unlock_async();
		
		wakeup_flag.set();
	}
	
	void ioready_dispatcher_poll::modify_ioready_callback(ioready_callback *link, int event_mask)
		throw()
	{
		bool sync=guard.write_lock_async();
		
		link->event_mask=event_mask;
		
		update_polltab_entry(link->fd);
		
		if (sync) synchronize();
		else guard.write_unlock_async();
		
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
