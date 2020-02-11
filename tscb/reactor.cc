/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#include <memory>

#include <tscb/reactor.h>

/**
	\page reactor_descr Reactor interface

	The \ref tscb::reactor_service interface combines the
	\ref tscb::timer_service, \ref tscb::ioready_service and
	\ref tscb::workqueue_service interfaces. It is suitable
	for being used as the basis for event-driven applications
	that perform actions in reaction to external events.

	In most event-driven applications, clients should use \ref
	tscb::reactor_service to request event callback services while
	a \ref tscb::reactor instance in the main program takes over the
	role of dispatching all events.

	\section reactor_usage_example Reactor usage example

	\code
		// Read non-blocking from stdin, echo input back to stdout. Exit
		// cleanly when stdin is at end-of-stream (ctrl-D) or we did not
		// get new input for 3 seconds.

		#include <fcntl.h>
		#include <unistd.h>
		#include <chrono>
		#include <tscb/reactor.h>

		constexpr auto IDLE_TIMEOUT = std::chrono::seconds(3);

		int main()
		{
			using steady_clock = std::chrono::steady_clock;
			tscb::reactor reactor;
			bool request_exit = false;

			// Read non-blocking from stdin.
			::fcntl(0, F_SETFL, O_NONBLOCK);

			// Set up to exit program on idle timeout.
			tscb::timer_connection idle_timeout =
				reactor.timer([&](steady_clock::time_point) {
					request_exit = true;
					reactor.wake_up();
				}, steady_clock::now() + idle_timeout);

			// Handle stdin, echo back input to stdout.
			reactor.watch([&](tscb::ioready_events) {
				std::array<char, 1024> buffer;
				for (;;) {
					ssize_t count = ::read(0, &buffer[0], buffer.size());
					if (count > 0) {
						::write(1, &buffer[0], count);
						// Reset idle timeout.
						idle_timeout.set(steady_clock::now() + IDLE_TIMEOUT);
					} else if (count == -1 && errno == EAGAIN) {
						break;
					} else {
						// Handle end-of-stream.
						request_exit = true;
						reactor.wake_up();
						break;
					}
				}
			}, 0, tscb::ioready_input);

			// Run until exit requested (timeout or end of stream).
			while (!request_exit) {
				reactor.dispatch();
			}

			return 0;
		}
	\endcode
*/

namespace tscb {

reactor::reactor()
	: io_(create_ioready_dispatcher())
	, timer_([io{io_.get()}](){io->wake_up();})
	, workqueue_([io{io_.get()}](){io->wake_up();})
{
}

reactor::~reactor() noexcept
{
}

std::pair<connection, std::function<void()>>
reactor::register_deferred_procedure(std::function<void()> function)
{
	return workqueue_.register_deferred_procedure(std::move(function));
}

std::pair<connection, std::function<void()>>
reactor::register_async_deferred_procedure(std::function<void()> function)
{
	return workqueue_.register_async_deferred_procedure(std::move(function));
}

void
reactor::queue_procedure(std::function<void()> function)
{
	workqueue_.queue_procedure(function);
}

timer_connection
reactor::timer(
	std::function<void(std::chrono::steady_clock::time_point)> function,
	std::chrono::steady_clock::time_point when)
{
	return timer_.timer(std::move(function), std::move(when));
}

timer_connection
reactor::one_shot_timer(
	std::function<void(std::chrono::steady_clock::time_point)> function,
	std::chrono::steady_clock::time_point when)
{
	return timer_.one_shot_timer(std::move(function), std::move(when));
}

timer_connection
reactor::suspended_timer(
	std::function<void(std::chrono::steady_clock::time_point)> function)
{
	return timer_.suspended_timer(std::move(function));
}

timer_connection
reactor::one_shot_suspended_timer(
	std::function<void(std::chrono::steady_clock::time_point)> function)
{
	return timer_.one_shot_suspended_timer(std::move(function));
}

ioready_connection
reactor::watch(
	std::function<void(tscb::ioready_events)> function,
	int fd, tscb::ioready_events event_mask)
{
	return io_->watch(std::move(function), fd, event_mask);
}

/**
	\brief Wake reactor up prematurely.

	Interrupets \ref dispatch to either return prematurely without
	blocking if there is presently a call to \ref dispatch ongoing, or
	causes next call to \ref dispatch to return without blocking.

	This function is async-signal safe and also thread-safe: It can be
	called from other threads or signal handlers.
*/
void
reactor::wake_up() noexcept
{
	io_->wake_up();
}

/**
	\brief Dispatch events.

	Runs one iteration of the event handling loop. This will:

	- handle queued procedures pending at call time
	- handle all timers that are due according to steady_clock time
	- handle I/O events

	After handling all non-I/O events, the call will block iff
	- there are no queued procedures pending and
	- there are no timers due at current clock time and
	- there are no pending I/O events

	The call will return instantly if any I/O event is handled, and
	also if any other event is pending. Otherwise it will block until
	the earliest timer event, an I/O event occurs, or any other event
	needs handling, in particular it will also return if:
	- any queued procedure or timer with earlier due time is triggered
	- any actionable I/O event occurs during the blocking time
	- the \ref wake_up function is called concurrently

	This function never throws any exception by itself, but if any of
	the user-supplied handler callback functions throws an exception,
	this is passed through. In the event of such an exception, the
	function is safe against resource leaks and loss of event
	notifications. Operation can resume if/after exception is handled.
*/
void
reactor::dispatch()
{
	workqueue_.dispatch();

	bool timers_pending;
	std::chrono::steady_clock::time_point next_timer, now;
	std::tie(timers_pending, next_timer) = timer_.next_timer();
	if (timers_pending) {
		now = std::chrono::steady_clock::now();
		while (timers_pending && next_timer <= now) {
			timer_.run(now);
			now = std::chrono::steady_clock::now();
			std::tie(timers_pending, next_timer) = timer_.next_timer();
		}
	}

	if (timers_pending) {
		std::chrono::steady_clock::duration timeout = next_timer - now;
		io_->dispatch(&timeout);
	} else {
		io_->dispatch(nullptr);
	}
}

/**
	\brief Dispatch pending events, but do not wait

	\return
		Whether any event was processed

	Processes a number of events (not necessarily all) that are
	pending currently. Returns "true" if any event was processed
	(in which case it usually makes sense to call the function
	again to check for further events), or "false" if no event can
	be processed at the moment.

	In contrast to \ref dispatch this call never blocks/waits. For
	exception-safety, the guarantees described in \ref dispatch apply
	as well. Also see \ref dispatch_pending_all.
*/
bool
reactor::dispatch_pending()
{
	bool processed_events = false;

	processed_events = workqueue_.dispatch();

	bool timers_pending;
	std::chrono::steady_clock::time_point first_timer_due;
	std::tie(timers_pending, first_timer_due) = timer_.next_timer();
	if (timers_pending) {
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

		if (first_timer_due <= now) {
			processed_events = true;

			timer_.run(now);
		}
	}

	if (io_->dispatch_pending()) {
		processed_events = true;
	}

	return processed_events;
}

/**
	\brief Dispatch all pending events, but do not wait

	Processes all pending events, but does not wait for new events
	to arrive. This is purely a convenience function that loops calling
	\ref dispatch_pending, see comments there.
*/
void reactor::dispatch_pending_all()
{
	while (dispatch_pending()) {
		/* empty */
	}
}

};
