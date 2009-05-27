/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <boost/bind.hpp>

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
					link->target(ev&link->event_mask);
				}
				link=link->active_next;
			}
		}
		
		if (guard.read_unlock()) synchronize();
	}
	
	int ioready_dispatcher_kqueue::dispatch(const boost::posix_time::time_duration *timeout, int max)
		throw()
	{
		pipe_eventflag *evflag=wakeup_flag;
		
		struct timespec tv, *t;
		if (timeout) {
			tv.tv_sec=timeout->total_seconds();
			tv.tv_nsec=timeout->total_nanoseconds()%1000000000;
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
			watch(boost::bind(&ioready_dispatcher_kqueue::drain_queue, this),
				tmp->readfd, EVMASK_INPUT);
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
			stale->cancelled();
			stale->release();
			stale=next;
		}
	}
	
	void ioready_dispatcher_kqueue::update_evmask(int fd) throw()
	{
		int oldevmask=(long)callback_tab.get_closure(fd);
		ioready_callback_link *tmp=callback_tab.lookup_first_callback(fd);
		int newevmask=0;
		while(tmp) {
			newevmask|=tmp->event_mask;
			tmp=tmp->active_next;
		}
		struct kevent modlist[2];
		int nmods=0;
		if ((oldevmask^newevmask)&EVMASK_OUTPUT) {
			EV_SET(&modlist[nmods], fd, EVFILT_WRITE, (newevmask&EVMASK_OUTPUT)?EV_ADD:EV_DELETE, 0, 0, (void *)EVFILT_WRITE);
			nmods++;
		}
		if ((oldevmask^newevmask)&EVMASK_INPUT) {
			EV_SET(&modlist[nmods], fd, EVFILT_READ, (newevmask&EVMASK_INPUT)?EV_ADD:EV_DELETE, 0, 0, (void *)EVFILT_READ);
			nmods++;
		}
		struct timespec timeout;
		timeout.tv_sec=0;
		timeout.tv_nsec=0;
		if (nmods>0)
			kevent(kqueue_fd, modlist, nmods, NULL, 0, &timeout);
		callback_tab.set_closure(fd, (void *)newevmask);
	}
	
	void ioready_dispatcher_kqueue::register_ioready_callback(ioready_callback_link *link)
		throw(std::bad_alloc)
	{
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

		update_evmask(link->fd);
		
		link->service=this;
		
		if (sync) synchronize();
		else guard.write_unlock_async();
	}
	
	void ioready_dispatcher_kqueue::unregister_ioready_callback(ioready_callback_link *link)
		throw()
	{
		bool sync=guard.write_lock_async();
		
		if (link->service) {
			callback_tab.remove(link);
			
			update_evmask(link->fd);
			
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
		
		update_evmask(link->fd);
		
		if (sync) synchronize();
		else guard.write_unlock_async();
	}
	
	void ioready_dispatcher_kqueue::drain_queue(void) throw()
	{
	}
	
	ioready_dispatcher *
	create_ioready_dispatcher_kqueue(void) throw(std::bad_alloc, std::runtime_error)
	{
		return new ioready_dispatcher_kqueue();
	}
	
}
