/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#include <unistd.h>
#include <fcntl.h>

#include <tscb/config.h>

#ifdef HAVE_POLL
#include <sys/poll.h>
#endif

#include <tscb/detail/eventflag.h>

#include <stdexcept>

namespace tscb {
namespace detail {

/**
	\class atomic_eventflag
	\brief Async-safe event flag using busy-waiting.
	\headerfile tscb/detail/eventflag.h <tscb/detail/eventflag.h>

	Implementation of an event flag using an atomic variable and
	busy-waiting.

	Prefer to use implementations that do not need busy waiting such
	as \ref pipe_eventflag.

	\fn atomic_eventflag::atomic_eventflag
	\brief Create event flag in cleared state.

	Creates an atomic_eventflag instance, initialized to "cleared" state.


	\fn atomic_eventflag::set
	\brief Trigger the event

	Triggers the event associated with this eventtrigger. This function
	is async-safe meaning that it can be called from any context
	(including signal handler).


	\fn atomic_eventflag::clear
	\brief Clear the flag

	Clear the flag, threads calling \ref wait will block until the flag
	is set again via \ref set. This operation is wait-free and
	async-signal safe.


	\fn atomic_eventflag::wait
	\brief Wait until the flag is set

	Block the current thread until the flag is set. The thread will
	continue without blocking if the flag is set already.


	\fn atomic_eventflag::set
	\brief Set the flag.

	Set the flag, will implicitly wake up all threads waiting for the
	flag via \ref wait. This operation is wait-free and async-signal
	safe.


	\fn atomic_eventflag::clear
	\brief Clear the flag

	Clear the flag, threads calling \ref wait will block until the flag
	is set again via \ref set. This operation is wait-free and
	async-signal safe.


	\fn atomic_eventflag::wait
	\brief Wait until the flag is set

	Block the current thread until the flag is set. The thread will
	continue without blocking if the flag is set already. This function
	will spin busy-waiting on the atomic variable.
*/

/**
	\class pipe_eventflag
	\brief Event flag implementation using a control pipe
	\headerfile tscb/detail/eventflag.h <tscb/detail/eventflag.h>

	This class implements an event flag that can cooperate with I/O
	dispatching and that can suspend threads without the need for
	busy-waiting. It is implemented using a control pipe (cf.
	atomic_eventflag).


	\fn pipe_eventflag::set
	\brief Set the flag.

	Set the flag, will implicitly wake up all threads waiting for the
	flag via \ref wait. This operation is wait-free and async-signal
	safe.


	\fn pipe_eventflag::clear
	\brief Clear the flag

	Clear the flag, threads calling \ref wait will block until the flag
	is set again via \ref set. This operation is wait-free and
	async-signal safe.


	\fn pipe_eventflag::wait
	\brief Wait until the flag is set

	Block the current thread until the flag is set. The thread will
	continue without blocking if the flag is set already. This function
	will spin busy-waiting on the pipe variable.
*/

/**
	\brief Create pipe_eventflag

	Create a new pipe_eventflag initialized to "cleared" state. The
	constructor may fail with std::runtime_error if file descriptors
	are exhausted.
*/
pipe_eventflag::pipe_eventflag()
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


void pipe_eventflag::set_slow() noexcept
{
	/* at least one thread has been marked "waiting"; we have to
	post a wakeup; the last thread that was waiting will clear
	the control pipe */

	int expected = 1;
	if (!flagged_.compare_exchange_strong(expected, 2, std::memory_order_relaxed)) {
		return;
	}

	char c = 0;
	do {
	} while (write(writefd_, &c, 1) != 1);
}

/**
	\fn pipe_eventflag::flagged()
	\brief Check whether flag is set.
	\returns True if flag is set.
*/

/**
	\fn pipe_eventflag::waiting()
	\brief Number of threads waiting for this flag.
	\returns The number of threads presently waiting on this flag.
*/

/**
	\fn pipe_eventflag::readfd()
	\brief File descriptor to check for readability.
	\returns File descriptor.

	Returns a file descriptor that can be polled for reading. If the
	present flag marked its state via \ref start_waiting, and the
	flag is unset after \ref start_waiting (see \ref flagged), then
	the file descriptor will become readable if the flag is set
	intermittently.
*/

/**
	\fn pipe_eventflag::start_waiting
	\brief Start slow wait path for calling thread.
*/

void pipe_eventflag::wait_slow() noexcept
{
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

/**
	\fn pipe_eventflag::stop_waiting
	\brief End slow wait path for calling thread.
*/

void pipe_eventflag::clear_slow() noexcept
{
	/* a wakeup has been sent the last time the flag was raised;
	clear the control pipe */
	char c;
	do {
	} while (read(readfd_, &c, 1) != 1);
}

}
}
