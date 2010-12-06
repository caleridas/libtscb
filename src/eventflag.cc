/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#include <tscb/eventflag>

namespace tscb {
	
	eventflag::~eventflag(void) throw()
	{
	}
	
	
	pipe_eventflag::pipe_eventflag(void) throw(std::runtime_error)
		: flagged(0), waiting(0)
	{
		int filedes[2];
		int error=pipe(filedes);
		
		if (error) throw std::runtime_error("Unable to create control pipe");
		
		readfd=filedes[0];
		writefd=filedes[1];
	}
	
	pipe_eventflag::~pipe_eventflag(void) throw()
	{
		close(readfd);
		close(writefd);
	}
	
	void pipe_eventflag::set(void) throw()
	{
		/* fast path (to avoid atomic op) if flag is already set */
		if (flagged.load(memory_order_relaxed)!=0) return;
		
		/* atomic exchange to ensure only one setter can "see" the
		0->1 transition; otherwise we could have spurious wakeups */
		int expected=0;
		if (!flagged.compare_exchange_strong(expected, 1, memory_order_release)) return;
		
		/* we are now certain that we have switched the flag from 0 to 1;
		if no one has been waiting before we switched the flag,
		there is no one to wakeup */
		
		if (__builtin_expect(waiting.load(memory_order_relaxed)==0, true)) return;
		
		/* at least one thread has been marked "waiting"; we have to
		post a wakeup; the last thread that was waiting will clear
		the control pipe */
		
		expected=1;
		if (!flagged.compare_exchange_strong(expected, 2, memory_order_relaxed))
			return;
		
		char c=0;
		write(writefd, &c, 1);
	}
	
	void pipe_eventflag::start_waiting(void) throw()
	{
		/* slow path */
		waiting.fetch_add(1, memory_order_relaxed);
	}
	
	void pipe_eventflag::wait(void) throw()
	{
		/* fast path to avoid atomic op if flag is already set */
		if (flagged.load(memory_order_acquire)!=0) return;
		
		/* slow path */
		start_waiting();
		
		if (flagged.load(memory_order_acquire)==0) {
			/* poll file descriptor */
		}
		
		stop_waiting();
		
	}
	
	void pipe_eventflag::stop_waiting(void) throw()
	{
		waiting.fetch_sub(1, memory_order_relaxed);
	}
	
	void pipe_eventflag::clear(void) throw()
	{
		int oldval;
		{
			oldval=flagged.load(memory_order_relaxed);
			/* fast path (to avoid atomic op) if flag is already cleared */
			if (oldval==0) return;
			/* after clearing a flag, the application will test a
			condition in a data structure; make sure test of the
			condition and clearing of the flag are not reordered by
			changing the flag with "acquire" semantics */
		} while(!flagged.compare_exchange_strong(oldval, 0, memory_order_acquire));
		if (__builtin_expect(oldval==1, true)) return;
		
		/* a wakeup has been sent the last time the flag was raised;
		clear the control pipe */
		char c;
		read(readfd, &c, 1);
	}
	
	platform_eventflag::platform_eventflag(void) throw()
		: mutex((pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER),
		cond((pthread_cond_t)PTHREAD_COND_INITIALIZER),
		flagged(false)
	{
	}
	
	platform_eventflag::~platform_eventflag(void) throw()
	{
	}
	
	void platform_eventflag::set(void) throw()
	{
		if (flagged) return;
		pthread_mutex_lock(&mutex);
		flagged=true;
		pthread_mutex_unlock(&mutex);
		pthread_cond_broadcast(&cond);
	}
	
	void platform_eventflag::wait(void) throw()
	{
		if (flagged) return;
		pthread_mutex_lock(&mutex);
		while (!flagged) pthread_cond_wait(&cond, &mutex);
		pthread_mutex_unlock(&mutex);
	}
	
	void platform_eventflag::clear(void) throw()
	{
		flagged=false;
	}
	
	#if 0
	
	signal_eventflag::signal_eventflag(pthread_t _thread, int _signo) throw()
		: thread(_thread), signo(_signo)
	{
	}
	
	signal_eventflag::~signal_eventflag(void) throw()
	{
	}
	
	void signal_eventflag::set(void) throw()
	{
		if (!flagged) {
			/* ordering is important here; we can allow spurious
			wakeups (through spurious signal to the thread), but not missed
			wakeups */
			flagged=true;
			
			/* PREMISE: system calls are implicit memory fences */
			
			pthread_kill(thread, signo);
		}
	}
	
	void signal_eventflag::wait(void) throw()
	{
		sigset_t set;
		
		sigemptyset(&set);
		sigaddset(&set, signo);
		
		int s;
		sigwait(&set, &s);
	}
	
	void signal_eventflag::clear(void) throw()
	{
		if (flagged) { sigtimedwait
		sigset_t set;
		
		sigemptyset(&set);
		sigaddset(&set, signo);
		
		int s;
		if (sigpending
	}
	
	#endif
	
}
