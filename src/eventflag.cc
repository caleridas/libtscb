/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#include <unistd.h>
#include <fcntl.h>

#include <tscb/config>

#ifdef HAVE_POLL
#include <sys/poll.h>
#endif

#include <tscb/eventflag>

namespace tscb {
	
	eventtrigger::~eventtrigger(void) noexcept
	{
	}
	
	eventflag::~eventflag(void) noexcept
	{
	}
	
	
	pipe_eventflag::pipe_eventflag(void) throw(std::runtime_error)
		: flagged_(0), waiting_(0)
	{
		int filedes[2];
		int error = -1;
		
#ifdef HAVE_PIPE2
		error = ::pipe2(filedes, O_CLOEXEC);
#endif
		if (error) {
			error = ::pipe(filedes);
			if (error == 0) {
				::fcntl(filedes[0], F_SETFL, O_CLOEXEC);
				::fcntl(filedes[1], F_SETFL, O_CLOEXEC);
			}
		}
		
		if (error) {
			throw std::runtime_error("Unable to create control pipe");
		}
		
		readfd_ = filedes[0];
		writefd_ = filedes[1];
	}
	
	pipe_eventflag::~pipe_eventflag(void) noexcept
	{
		::close(readfd_);
		::close(writefd_);
	}
	
	void pipe_eventflag::set(void) noexcept
	{
		/* fast path (to avoid atomic op) if flag is already set */
		if (flagged_.load(std::memory_order_relaxed) != 0) {
			return;
		}
		
		/* atomic exchange to ensure only one setter can "see" the
		0->1 transition; otherwise we could have spurious wakeups */
		int expected = 0;
		if (!flagged_.compare_exchange_strong(expected, 1, std::memory_order_release)) {
			return;
		}
		
		/* we are now certain that we have switched the flag from 0 to 1;
		if no one has been waiting before we switched the flag,
		there is no one to wakeup */
		
		if (__builtin_expect(waiting_.load(std::memory_order_relaxed) == 0, true)) {
			return;
		}
		
		/* at least one thread has been marked "waiting"; we have to
		post a wakeup; the last thread that was waiting will clear
		the control pipe */
		
		expected = 1;
		if (!flagged_.compare_exchange_strong(expected, 2, std::memory_order_relaxed)) {
			return;
		}
		
		char c = 0;
		write(writefd_, &c, 1);
	}
	
	void pipe_eventflag::start_waiting(void) noexcept
	{
		/* slow path */
		waiting_.fetch_add(1, std::memory_order_relaxed);
	}
	
	void pipe_eventflag::wait(void) noexcept
	{
		/* fast path to avoid atomic op if flag is already set */
		if (flagged_.load(std::memory_order_acquire) != 0) {
			return;
		}
		
		/* slow path */
		start_waiting();
		
		if (flagged_.load(std::memory_order_acquire) == 0) {
			#ifdef HAVE_POLL
			struct pollfd pfd;
			pfd.fd = readfd_;
			pfd.events = POLLIN;
			for(;;) {
				poll(&pfd, 1, -1);
				if (pfd.revents & POLLIN) break;
			}
			#else
			/* old OS X do not have poll -- pretty dumb, but
			have to comply, so just read and re-inject token */
			char c;
			read(readfd_, &c, 1);
			write(writefd_, &c, 1);
			#endif
		}
		
		stop_waiting();
		
	}
	
	void pipe_eventflag::stop_waiting(void) noexcept
	{
		waiting_.fetch_sub(1, std::memory_order_relaxed);
	}
	
	void pipe_eventflag::clear(void) noexcept
	{
		int oldval;
		{
			oldval = flagged_.load(std::memory_order_relaxed);
			/* fast path (to avoid atomic op) if flag is already cleared */
			if (oldval==0) {
				return;
			}
			/* after clearing a flag, the application will test a
			condition in a data structure; make sure test of the
			condition and clearing of the flag are not reordered by
			changing the flag with "acquire" semantics */
		} while (!flagged_.compare_exchange_strong(oldval, 0, std::memory_order_acquire));
		if (__builtin_expect(oldval == 1, true)) {
			return;
		}
		
		/* a wakeup has been sent the last time the flag was raised;
		clear the control pipe */
		char c;
		read(readfd_, &c, 1);
	}
	
	platform_eventflag::platform_eventflag(void) noexcept
		: mutex_((pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER),
		cond_((pthread_cond_t)PTHREAD_COND_INITIALIZER),
		flagged_(false)
	{
	}
	
	platform_eventflag::~platform_eventflag(void) noexcept
	{
	}
	
	void platform_eventflag::set(void) noexcept
	{
		pthread_mutex_lock(&mutex_);
		flagged_ = true;
		pthread_cond_broadcast(&cond_);
		pthread_mutex_unlock(&mutex_);
	}
	
	void platform_eventflag::wait(void) noexcept
	{
		pthread_mutex_lock(&mutex_);
		while (!flagged_) {
			pthread_cond_wait(&cond_, &mutex_);
		}
		pthread_mutex_unlock(&mutex_);
	}
	
	void platform_eventflag::clear(void) noexcept
	{
		pthread_mutex_lock(&mutex_);
		flagged_ = false;
		pthread_mutex_unlock(&mutex_);
	}
	
	#if 0
	
	signal_eventflag::signal_eventflag(pthread_t _thread, int _signo) noexcept
		: thread(_thread), signo(_signo)
	{
	}
	
	signal_eventflag::~signal_eventflag(void) noexcept
	{
	}
	
	void signal_eventflag::set(void) noexcept
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
	
	void signal_eventflag::wait(void) noexcept
	{
		sigset_t set;
		
		sigemptyset(&set);
		sigaddset(&set, signo);
		
		int s;
		sigwait(&set, &s);
	}
	
	void signal_eventflag::clear(void) noexcept
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
