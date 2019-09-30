/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#include <unistd.h>
#include <fcntl.h>

#include <tscb/config.h>

#ifdef HAVE_POLL
#include <sys/poll.h>
#endif

#include <tscb/eventflag.h>

namespace tscb {

eventtrigger::~eventtrigger() noexcept
{
}

eventflag::~eventflag() noexcept
{
}


pipe_eventflag::pipe_eventflag() throw(std::runtime_error)
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

pipe_eventflag::~pipe_eventflag() noexcept
{
	::close(readfd_);
	::close(writefd_);
}

void pipe_eventflag::set() noexcept
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

	if (waiting_.load(std::memory_order_relaxed) == 0) {
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
	do {
	} while (write(writefd_, &c, 1) != 1);
}

void pipe_eventflag::start_waiting() noexcept
{
	/* slow path */
	waiting_.fetch_add(1, std::memory_order_relaxed);
}

void pipe_eventflag::wait() noexcept
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
		do {
		} while (read(readfd_, &c, 1) != 1);
		do {
		} while (write(writefd_, &c, 1) != 1);
		#endif
	}

	stop_waiting();

}

void pipe_eventflag::stop_waiting() noexcept
{
	waiting_.fetch_sub(1, std::memory_order_relaxed);
}

void pipe_eventflag::clear() noexcept
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
	if (oldval == 1) {
		return;
	}

	/* a wakeup has been sent the last time the flag was raised;
	clear the control pipe */
	char c;
	do {
	} while (read(readfd_, &c, 1) != 1);
}

platform_eventflag::platform_eventflag() noexcept
	: flagged_(false)
{
}

platform_eventflag::~platform_eventflag() noexcept
{
}

void platform_eventflag::set() noexcept
{
	std::lock_guard<std::mutex> guard(mutex_);
	flagged_ = true;
	cond_.notify_all();
}

void platform_eventflag::wait() noexcept
{
	std::unique_lock<std::mutex> guard(mutex_);
	while (!flagged_) {
		cond_.wait(guard);
	}
}

void platform_eventflag::clear() noexcept
{
	std::lock_guard<std::mutex> guard(mutex_);
	flagged_ = false;
}

}
