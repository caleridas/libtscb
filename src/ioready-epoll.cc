/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <sys/fcntl.h>
#include <sys/epoll.h>

#include <tscb/ioready-epoll>

namespace tscb {
	
	inline int ioready_dispatcher_epoll::translate_os_to_tscb(int ev) throw()
	{
		int e=0;
		if (ev&EPOLLIN) e|=EVMASK_INPUT;
		if (ev&EPOLLOUT) e|=EVMASK_OUTPUT;
		/* deliver hangup event to input and output handlers as well */
		if (ev&EPOLLHUP) e|=EVMASK_HANGUP|EVMASK_INPUT|EVMASK_OUTPUT;
		return e;
	}
	
	inline int ioready_dispatcher_epoll::translate_tscb_to_os(int ev) throw()
	{
		int e=0;
		if (ev&EVMASK_INPUT) e|=EPOLLIN;
		if (ev&EVMASK_OUTPUT) e|=EPOLLOUT;
		if (ev&EVMASK_HANGUP) e|=EPOLLHUP;
		return e;
	}
	
	ioready_dispatcher_epoll::ioready_dispatcher_epoll(void)
		throw(std::runtime_error)
		: wakeup_flag(0)
	{
		epoll_fd=epoll_create(1024);
		if (epoll_fd<0) throw std::runtime_error("Unable to create epoll descriptor");
	}
	
	ioready_dispatcher_epoll::~ioready_dispatcher_epoll(void) throw()
	{
		/* FIXME: cancel pending callbacks */
		close(epoll_fd);
		
		if (wakeup_flag) delete wakeup_flag;
	}
	
	void ioready_dispatcher_epoll::process_events(epoll_event events[], size_t nevents)
		throw()
	{
		while(guard.read_lock()) synchronize();
		
		for(size_t n=0; n<nevents; n++) {
			int fd=events[n].data.fd;
			int ev=translate_os_to_tscb(events[n].events);
			
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
	
	int ioready_dispatcher_epoll::dispatch(const long long *timeout, int max)
		throw()
	{
		pipe_eventflag *evflag=wakeup_flag;
		
		int poll_timeout;
		if (timeout) poll_timeout=*timeout/1000;
		else poll_timeout=-1;
		
		if (max>16) max=16;
		epoll_event events[max];
		
		ssize_t nevents;
		
		if (__builtin_expect(evflag==0, 1)) {
			nevents=epoll_wait(epoll_fd, events, max, poll_timeout);
			
			if (nevents>0) process_events(events, nevents);
			else nevents=0;
		} else {
			data_dependence_memory_barrier();
			evflag->start_waiting();
			if (evflag->flagged!=0) poll_timeout=0;
			nevents=epoll_wait(epoll_fd, events, max, poll_timeout);
			evflag->stop_waiting();
			
			if (nevents>0) process_events(events, nevents);
			else nevents=0;
			
			evflag->clear();
		}
		return nevents;
	}
	
	eventflag *ioready_dispatcher_epoll::get_eventflag(void)
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
			watch<ioready_dispatcher_epoll,
				&ioready_dispatcher_epoll::drain_queue,
				&ioready_dispatcher_epoll::release_queue>
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
		
	void ioready_dispatcher_epoll::synchronize(void) throw()
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
	
	void ioready_dispatcher_epoll::register_ioready_callback(tscb::ref<ioready_callback_link> link)
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
			epoll_event event;
			event.events=translate_tscb_to_os(link->event_mask);
			event.data.fd=link->fd;
			epoll_ctl(epoll_fd, EPOLL_CTL_ADD, link->fd, &event);
		} else {
			ioready_callback_link *tmp=callback_tab.lookup_first_callback(link->fd);
			int newevmask=0;
			while(tmp) {
				newevmask|=tmp->event_mask;
				tmp=tmp->active_next;
			}
			newevmask=translate_tscb_to_os(newevmask);
			
			epoll_event event;
			event.events=newevmask;
			event.data.fd=link->fd;
			epoll_ctl(epoll_fd, EPOLL_CTL_MOD, link->fd, &event);
		}
		
		link->service=this;
		
		/* object ownership has been taken over by chain */
		link.unassign();
		
		if (sync) synchronize();
		else guard.write_unlock_async();
	}
	
	void ioready_dispatcher_epoll::unregister_ioready_callback(ioready_callback_link *link)
		throw()
	{
		bool sync=guard.write_lock_async();
		
		if (link->service) {
			int fd=link->fd;
			
			int oldevmask=link->event_mask;
			callback_tab.remove(link);
			
			if (callback_tab.chain_empty(fd)) {
				epoll_event event;
				event.events=translate_tscb_to_os(oldevmask);
				event.data.fd=link->fd;
				epoll_ctl(epoll_fd, EPOLL_CTL_DEL, link->fd, &event);
			} else {
				ioready_callback_link *tmp=callback_tab.lookup_first_callback(link->fd);
				int newevmask=0;
				while(tmp) {
					newevmask|=tmp->event_mask;
					tmp=tmp->active_next;
				}
				newevmask=translate_tscb_to_os(newevmask);
				
				epoll_event event;
				event.events=newevmask;
				event.data.fd=link->fd;
				epoll_ctl(epoll_fd, EPOLL_CTL_MOD, link->fd, &event);
			}
			link->service=0;
		}
		
		link->cancellation_mutex.unlock();
		
		if (sync) synchronize();
		else guard.write_unlock_async();
		
	}
	
	void ioready_dispatcher_epoll::modify_ioready_callback(ioready_callback_link *link, int event_mask)
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
		newevmask=translate_tscb_to_os(newevmask);
		
		epoll_event event;
		event.events=newevmask;
		event.data.fd=link->fd;
		epoll_ctl(epoll_fd, EPOLL_CTL_MOD, link->fd, &event);
		
		if (sync) synchronize();
		else guard.write_unlock_async();
	}
	
	void ioready_dispatcher_epoll::drain_queue(int fd, int event_mask) throw()
	{
	}
	
	void ioready_dispatcher_epoll::release_queue(void) throw()
	{
	}
	
}
