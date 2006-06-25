/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <sys/fcntl.h>

#include <tscb/ioready-poll>

namespace tscb {
	
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
			
			pipe_callback=watch<ioready_dispatcher_poll,
				&ioready_dispatcher_poll::drain_queue,
				&ioready_dispatcher_poll::release_queue>
				(wakeup_flag.readfd, EVMASK_INPUT, this);
		}
		catch (std::bad_alloc) {
			delete master_ptab;
			throw;
		}
	}
	
	ioready_dispatcher_poll::~ioready_dispatcher_poll(void) throw()
	{
		/* FIXME: cancel pending callbacks */
		/* note: will also kill the pipe drain callback */
		
		/* FIXME: all other ptabs */
		delete master_ptab;
	}
	
	eventflag *ioready_dispatcher_poll::get_eventflag(void) throw()
	{
		return &wakeup_flag;
	}
	
	int ioready_dispatcher_poll::dispatch(const long long *timeout, int max)
		throw()
	{
		while(guard.read_lock()) synchronize();
		
		wakeup_flag.start_waiting();
		
		polltab *ptab=master_ptab;
		
		int count, handled=0;
		
		int poll_timeout;
		
		if (timeout) poll_timeout=*timeout/1000;
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
				
				ioready_callback_link *link=
					callback_tab.lookup_first_callback(fd);
				while(link) {
					data_dependence_memory_barrier();
					if (ev&link->event_mask) {
						(*link)(ev&link->event_mask);
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
		ioready_callback_link *stale=callback_tab.synchronize();
		/* FIXME: discard old polltabs */
		
		guard.sync_finished();
		
		while(stale) {
			ioready_callback_link *next=stale->inactive_next;
			stale->release();
			stale=next;
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
	
	void ioready_dispatcher_poll::create_polltab_entry(ioready_callback_link *link)
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
		ioready_callback_link *tmp=callback_tab.lookup_first_callback(fd);
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
	
	void ioready_dispatcher_poll::register_ioready_callback(tscb::ref<ioready_callback_link> link)
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
		
		/* object ownership has been taken over by chain */
		link.unassign();
		
		if (sync) synchronize();
		else guard.write_unlock_async();
		
		wakeup_flag.set();
	}
	
	void ioready_dispatcher_poll::unregister_ioready_callback(ioready_callback_link *link)
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
	
	void ioready_dispatcher_poll::modify_ioready_callback(ioready_callback_link *link, int event_mask)
		throw()
	{
		bool sync=guard.write_lock_async();
		
		link->event_mask=event_mask;
		
		update_polltab_entry(link->fd);
		
		if (sync) synchronize();
		else guard.write_unlock_async();
		
		wakeup_flag.set();
	}
	
	void ioready_dispatcher_poll::drain_queue(int fd, int event_mask) throw()
	{
	}
	
	void ioready_dispatcher_poll::release_queue(void) throw()
	{
	}
	
}
