/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/ioready-kqueue>

namespace tscb {
	
	ioready_dispatcher_kqueue::ioready_dispatcher_kqueue(void)
		throw(std::runtime_error)
		: wakeup_flag(0)
	{
		kqueue_fd=kqueue();
		if (kqueue_fd<0) throw std::runtime_error("Unable to create kqueue descriptor");
	}
	
	ioready_dispatcher_kqueue::~ioready_dispatcher_kqueue(void) throw()
	{
		/* FIXME: cancel pending callbacks */
		close(kqueue_fd);
		
		if (wakeup_flag) delete wakeup_flag;
	}
	
	void ioready_dispatcher_kqueue::process_events(struct kevent events[], size_t nevents)
		throw()
	{
		while(guard.read_lock()) synchronize();
		
		for(size_t n=0; n<nevents; n++) {
			int fd=events[n].ident;
			int ev=0;
			if (events[n].filter==EVFILT_READ) ev=EVMASK_INPUT;
			else if (events[n].filter==EVFILT_WRITE) ev=EVMASK_OUTPUT;
			
			ioready_callback_link *link=
				callback_tab.lookup_first_callback(fd);
			while(link) {
				data_dependence_memory_barrier();
				if (ev&link->event_mask) {
					(*link)(ev&link->event_mask);
				}
				link=link->active_next;
			}
		}
		
		if (guard.read_unlock()) synchronize();
	}
	
	int ioready_dispatcher_kqueue::dispatch(const long long *timeout, int max)
		throw()
	{
		pipe_eventflag *evflag=wakeup_flag;
		
		struct timespec tv, *t;
		if (timeout) {
			tv.tv_sec=*timeout/1000000;
			tv.tv_nsec=(*timeout%1000000)*1000;
			t=&tv;
		} else t=0;
		
		if (max>16) max=16;
		struct kevent events[max];
		
		ssize_t nevents;
		
		if (__builtin_expect(evflag==0, 1)) {
			nevents=kevent(kqueue_fd, NULL, 0, events, max, t);
			
			if (nevents>0) process_events(events, nevents);
			else nevents=0;
		} else {
			data_dependence_memory_barrier();
			evflag->start_waiting();
			if (evflag->flagged!=0) {
				tv.tv_sec=0;
				tv.tv_nsec=0;
				t=&tv;
			}
			nevents=kevent(kqueue_fd, NULL, 0, events, max, t);
			evflag->stop_waiting();
			
			if (nevents>0) process_events(events, nevents);
			else nevents=0;
			
			evflag->clear();
		}
		return nevents;
	}
	
	eventflag *ioready_dispatcher_kqueue::get_eventflag(void)
		throw(std::runtime_error, std::bad_alloc)
	{
		if (wakeup_flag) {
			data_dependence_memory_barrier();
			return wakeup_flag;
		}
		singleton_mutex.lock();
		
		if (wakeup_flag) {
			data_dependence_memory_barrier();
			singleton_mutex.unlock();
			return wakeup_flag;
		}
		
		pipe_eventflag *tmp=0;
		try {
			tmp=new pipe_eventflag();
			watch<ioready_dispatcher_kqueue,
				&ioready_dispatcher_kqueue::drain_queue,
				&ioready_dispatcher_kqueue::release_queue>
				(tmp->readfd, EVMASK_INPUT, this);
		}
		catch (std::bad_alloc) {
			delete tmp;
			singleton_mutex.unlock();
			throw;
		}
		catch (std::runtime_error) {
			delete tmp;
			singleton_mutex.unlock();
			throw;
		}
		
		memory_barrier();
		wakeup_flag=tmp;
		singleton_mutex.unlock();
		
		return wakeup_flag;
		
	}
		
	void ioready_dispatcher_kqueue::synchronize(void) throw()
	{
		ioready_callback_link *stale=callback_tab.synchronize();
		guard.sync_finished();
		
		while(stale) {
			ioready_callback_link *next=stale->inactive_next;
			stale->release();
			stale=next;
		}
	}
	
	void ioready_dispatcher_kqueue::register_ioready_callback(tscb::ref<ioready_callback_link> link)
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
			struct kevent modlist[4];
			
			struct timespec timeout;
			timeout.tv_sec=0;
			timeout.tv_nsec=0;
			
			modlist[0].ident=link->fd;
			modlist[0].flags=EV_ADD;
			modlist[0].filter=EVFILT_READ;
			modlist[1].ident=link->fd;
			modlist[1].flags=EV_ADD;
			modlist[1].filter=EVFILT_WRITE;
			modlist[2].ident=link->fd;
			modlist[2].flags=link->event_mask&EVMASK_INPUT?EV_ENABLE:EV_DISABLE;
			modlist[2].filter=EVFILT_READ;
			modlist[3].ident=link->fd;
			modlist[3].flags=link->event_mask&EVMASK_OUTPUT?EV_ENABLE:EV_DISABLE;
			modlist[3].filter=EVFILT_WRITE;
			
			kevent(kqueue_fd, modlist, 4, NULL, 0, &timeout);
		} else {
			ioready_callback_link *tmp=callback_tab.lookup_first_callback(link->fd);
			int newevmask=0;
			while(tmp) {
				newevmask|=tmp->event_mask;
				tmp=tmp->active_next;
			}
			
			struct kevent modlist[2];
			
			struct timespec timeout;
			timeout.tv_sec=0;
			timeout.tv_nsec=0;
			
			modlist[0].ident=link->fd;
			modlist[0].flags=newevmask&EVMASK_INPUT?EV_ENABLE:EV_DISABLE;
			modlist[0].filter=EVFILT_READ;
			modlist[1].ident=link->fd;
			modlist[1].flags=newevmask&EVMASK_OUTPUT?EV_ENABLE:EV_DISABLE;
			modlist[1].filter=EVFILT_WRITE;
			
			kevent(kqueue_fd, modlist, 2, NULL, 0, &timeout);
		}
		
		link->service=this;
		
		/* object ownership has been taken over by chain */
		link.unassign();
		
		if (sync) synchronize();
		else guard.write_unlock_async();
	}
	
	void ioready_dispatcher_kqueue::unregister_ioready_callback(ioready_callback_link *link)
		throw()
	{
		bool sync=guard.write_lock_async();
		
		if (link->service) {
			int fd=link->fd;
			
			callback_tab.remove(link);
			
			if (callback_tab.chain_empty(fd)) {
				struct kevent modlist[2];
				
				struct timespec timeout;
				timeout.tv_sec=0;
				timeout.tv_nsec=0;
				
				modlist[0].ident=link->fd;
				modlist[0].flags=EV_DELETE;
				modlist[0].filter=EVFILT_READ;
				modlist[1].ident=link->fd;
				modlist[1].flags=EV_DELETE;
				modlist[1].filter=EVFILT_WRITE;
				
				kevent(kqueue_fd, modlist, 2, NULL, 0, &timeout);
			} else {
				ioready_callback_link *tmp=callback_tab.lookup_first_callback(link->fd);
				int newevmask=0;
				while(tmp) {
					newevmask|=tmp->event_mask;
					tmp=tmp->active_next;
				}
				
				struct kevent modlist[2];
			
				struct timespec timeout;
				timeout.tv_sec=0;
				timeout.tv_nsec=0;
				
				modlist[0].ident=link->fd;
				modlist[0].flags=newevmask&EVMASK_INPUT?EV_ENABLE:EV_DISABLE;
				modlist[0].filter=EVFILT_READ;
				modlist[1].ident=link->fd;
				modlist[1].flags=newevmask&EVMASK_OUTPUT?EV_ENABLE:EV_DISABLE;
				modlist[1].filter=EVFILT_WRITE;
				
				kevent(kqueue_fd, modlist, 2, NULL, 0, &timeout);
			}
			link->service=0;
		}
		
		link->cancellation_mutex.unlock();
		
		if (sync) synchronize();
		else guard.write_unlock_async();
		
	}
	
	void ioready_dispatcher_kqueue::modify_ioready_callback(ioready_callback_link *link, int event_mask)
		throw()
	{
		bool sync=guard.write_lock_async();
		
		link->event_mask=event_mask;
		
		ioready_callback_link *tmp=callback_tab.lookup_first_callback(link->fd);
		int newevmask=0;
		while(tmp) {
			newevmask|=tmp->event_mask;
			tmp=tmp->active_next;
		}
		
		struct kevent modlist[2];
	
		struct timespec timeout;
		timeout.tv_sec=0;
		timeout.tv_nsec=0;
		
		modlist[0].ident=link->fd;
		modlist[0].flags=newevmask&EVMASK_INPUT?EV_ENABLE:EV_DISABLE;
		modlist[0].filter=EVFILT_READ;
		modlist[1].ident=link->fd;
		modlist[1].flags=newevmask&EVMASK_OUTPUT?EV_ENABLE:EV_DISABLE;
		modlist[1].filter=EVFILT_WRITE;
		
		kevent(kqueue_fd, modlist, 2, NULL, 0, &timeout);
		
		if (sync) synchronize();
		else guard.write_unlock_async();
	}
	
	void ioready_dispatcher_kqueue::drain_queue(int fd, int event_mask) throw()
	{
	}
	
	void ioready_dispatcher_kqueue::release_queue(void) throw()
	{
	}
	
}
