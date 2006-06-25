/*
 * (c) 2004 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.
 * Refer to the file "COPYING" for details.
 */

#include <tscb/config>
#include <tscb/compiler>

namespace tscb {
	
	file_callback_link::~file_callback_link(void) throw()
	{
	}
	
	void file_callback_link::cancel(void) throw()
	{
		event_dispatcher *d=_dispatcher;
		if (d) d->unregister_file(this);
	}
	
	timer_callback_link::~timer_callback_link(void) throw()
	{
	}
	
	void timer_callback_link::cancel(void) throw()
	{
		event_dispatcher *d=_dispatcher;
		if (d) d->unregister_timer(this);
	}
	
	event_dispatcher::~event_dispatcher(void) throw()
	{
	}
	
	dispatcher_posix::dispatcher_posix(void)
	{
		/* create control pipe; the reading end of the pipe is included
		in the set of file_event descriptors that are polled; a single character
		is written into the pipe as an easy way to get the thread out of
		the poll() system call */
		if (pipe(controlpipe)) {
			/* this should never happen */
			__panic("unable to create control pipe: %s\n", strerror(errno));
		}
		/* make the file_event descriptors non-blocking; */
		int flags=fcntl(controlpipe[0], F_GETFL);
		flags |= O_NONBLOCK;
		fcntl(controlpipe[0], F_SETFL, flags);
	
		flags=fcntl(controlpipe[1], F_GETFL);
		flags |= O_NONBLOCK;
		fcntl(controlpipe[1], F_SETFL, flags);
		
		/* mark the file descriptors close-on-exec; they are purely for
		internal use only, for waking up a sleeping thread upon addition
		of a new file descriptor or timer; there really isn't any point
		in receiving wakeups from child processes */
		fcntl(controlpipe[0], F_SETFD, FD_CLOEXEC);
		fcntl(controlpipe[1], F_SETFD, FD_CLOEXEC);
		__set_file(controlpipe[0], 0);
		
		cancelled=false;
		need_wakeup=false;
	}
	
	dispatcher_posix::~dispatcher_posix(void) throw()
	{
		close(controlpipe[0]);
		close(controlpipe[1]);
		for(size_t n=0; n<notifiers.size(); n++)
			if (notifiers[n]) notifiers[n]->release_runnable();
	}
	
	void dispatcher_posix::register_timer(timer_callback_link *t) throw()
	{
		t->pin();
		t->pin_runnable();
		
		dispatcher_mutex.lock();
		timer_queue.insert(t);
		t->queued=true;
		__set_dispatcher(t);
		dispatcher_mutex.unlock();
		wakeup();
	}
	
	void dispatcher_posix::unregister_timer(timer_callback_link *t) throw()
	{
		dispatcher_mutex.lock();
		if (__builtin_expect(t->get_dispatcher()==this, true)) {
			if (t->queued) timer_queue.remove(t);
			t->queued=false;
			__unset_dispatcher(t);
			dispatcher_mutex.unlock();
			
			t->release_runnable();
			wakeup();
		} else dispatcher_mutex.unlock();
	}
	
	void dispatcher_posix::wakeup(void)
	{
		if (need_wakeup) {
			char c;
			/* it is important to keep the ordering of the following two
			statements, otherwise the need_wakeup flag might be
			cleared erroneously (i.e. the thread has been activated
			and set the need_wakeup flag in the meantime); the small
			race between testing and clearing this flag can only
			result in superfluous wakeups, but never in lost wakeups */
			need_wakeup=false;
			write(controlpipe[1], &c, 1);
		}
	}
	
	bool dispatcher_posix::run_timers(deltatime &next_timer, deltatime &elapsed)
	{
		timer_callback_link *t=timer_queue.peek_min();
		
		if (!t) {
			/* no need to get current time if there are no timers pending
			anyway */
			elapsed=0;
			return false;
		}
		
		abstime now=current_time();
		abstime start=now;
		
		while (t && (t->expires()<=now)) {
			if (cancelled) return 0;
			t=timer_queue.extract_min();
			t->queued=false;
			
			/* acquire temporary reference to protect from
			deallocation of the timer_callback_link while running it */
			t->pin_runnable();
			dispatcher_mutex.unlock();
			
			abstime expires=now;
			bool rearm=t->notify(this, expires);
			
			dispatcher_mutex.lock();
			/* three cases have to be considered here:
			1. the timer has been cancelled (e->event_dispatcher==NULL);
			in that case the reference held by the timer queue has
			already been dropped, and after dropping the temporary
			reference acquired just above the timer will be
			deallocated; however this "release" operation *must
			not* be done under the lock
			2. the timer has not been cancelled, but it should
			not be scheduled again (retval==false); in that case the
			timer queue is still holding a reference, which must be
			dropped as well for the timer to be deallocated
			3. the timer has not been cancelled and should fire
			again at some later point in time; in that case it
			is sufficient to re-insert it into the pending timer
			queue */
			if (t->get_dispatcher()) {
				if (rearm) {
					*t=expires;
					timer_queue.insert(t);
					t->queued=true;
				} else t->release_runnable();
			}
			
			dispatcher_mutex.unlock();
			/* It is important that the lock is not being held during
			this last release operation; we are potentially holding
			the last reference to the timer, dropping this
			last reference may cause the object to be deleted,
			which in turn can result in a cascade of other
			actions that modify the timer queue */
			t->release_runnable();
			dispatcher_mutex.lock();
			
			t=timer_queue.peek_min();
			/* only need to update our notion of "current" time if
			we are not sure if the next pending timer is already due
			to run; saves one system call in the (common) case
			of multiple timers expiring simultaneously */
			if (t) if (t->expires()>now)
				now=current_time();
			
		}
		
		elapsed=now-start;
		if (t) {
			next_timer=t->expires()-now;
			return true;
		} else return false;
	}
	
	typedef event_dispatcher *(*create_dispatcher_func_t)(void);
	static event_dispatcher *create_dispatcher_unknown(void);
	static create_dispatcher_func_t create_dispatcher_func=create_dispatcher_unknown;
	
	static create_dispatcher_func_t dispatcher_impl[]={
	#ifdef HAVE_KQUEUE
		create_dispatcher_kqueue,
	#endif
	#ifdef HAVE_EPOLL
		create_dispatcher_epoll,
	#endif
	#ifdef HAVE_POLL
		create_dispatcher_poll,
	#endif
	#ifdef HAVE_SELECT
		create_dispatcher_select,
	#endif
		0
	};
	
	static event_dispatcher *create_dispatcher_unknown(void)
	{
		char *impl=getenv("EVENT_DISPATCHER_IMPLEMENTATION");
		if (impl) {
	#ifdef HAVE_KQUEUE
			if (strcmp(impl, "kqueue")==0) {
				create_dispatcher_func=create_dispatcher_kqueue;
			} else
	#endif
	#ifdef HAVE_EPOLL
			if (strcmp(impl, "epoll")==0) {
				create_dispatcher_func=create_dispatcher_epoll;
			} else
	#endif
	#ifdef HAVE_POLL
			if (strcmp(impl, "poll")==0) {
				create_dispatcher_func=create_dispatcher_poll;
			} else
	#endif
	#ifdef HAVE_SELECT
			if (strcmp(impl, "select")==0) {
				create_dispatcher_func=create_dispatcher_select;
			} else
	#endif
			{
				fprintf(stderr, "Unknown or unsupported event event_dispatcher implementation: %s\n", impl);
				abort();
			}
			fprintf(stderr, "Overriding event event_dispatcher implementation: %s\n", impl);
			return (*create_dispatcher_func)();
		}
		for(int n=0;; n++) {
			create_dispatcher_func_t f=dispatcher_impl[n];
			try {
				event_dispatcher *tmp=(*f)();
				/* note: this relies on pointer assignment being
				atomic -- I don't know if there may be platforms
				where this is not the case */
				create_dispatcher_func=f;
				return tmp;
			}
			catch (std::exception e) {
				/* only occurs if instantiation of event_dispatcher
				failed, simply try next one */
			}
		}
		/* never reached - NULL pointer dereference will happen before */
	}
	
	event_dispatcher *create_dispatcher(void)
	{
		return (*create_dispatcher_func)();
	}
	
}; /* namespace event */
