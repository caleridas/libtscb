/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#ifndef TSCB_DISPATCH_H
#define TSCB_DISPATCH_H

#include <list>
#include <mutex>

#include <tscb/reactor.h>

/**
	\page dispatcher_descr Compound event dispatching

	The various event dispatching mechanisms
	(\ref tscb::generic_timerqueue_dispatcher "timerqueue_dispatcher",
	\ref tscb::ioready_dispatcher "ioready_dispatcher") can
	be used stand-alone if the application so desires.

	However many applications require both timer and IO readiness
	callbacks, and the classes support this use. Specifically
	timer queues and io readiness can cooperate by using
	an \ref tscb::eventtrigger "eventtrigger" that is associated
	with an \ref tscb::ioready_dispatcher "ioready_dispatcher"
	(see for example \ref tscb::ioready_dispatcher::get_eventtrigger
	"ioready_dispatcher::get_eventtrigger"):

	\code
		tscb::ioready_dispatcher *io;
		...
		tscb::eventtrigger & trigger = io->get_eventtrigger();
		tscb::timerqueue_dispatcher *tq;

		tq=new tscb::timerqueue_dispatcher(trigger);
	\endcode


	This will enable the timer queue dispatcher to interrupt the
	IO readiness dispatcher in case new timers have been added.

	To handle both timer and io readiness events, the dispatching
	thread should call the individual dispatchers in the following
	way:

	\code
		tscb::ioready_dispatcher * io;
		tscb::timerqueue_dispatcher * timers;

		io = tscb::create_ioready_dispatcher();
		timers = new tscb::timerqueue_dispatcher(io->get_eventtrigger());

		// run the dispatcher loop
		while(true) {
			long long next_timer;
			long long now = tscb::current_time();
			bool timer_pending;
			do {
				next_timer = now;
				timer_pending = timers->run_queue(next_timer);
				if (!timer_pending) break;
				now = tscb::current_time();
			} while(now >= next_timer);

			if (timer_pending) {
				long long timeout = next_timer-now;
				io->dispatch(&timeout);
			} else
				io->dispatch(0);
		}
	\endcode

	The above code is implemented in the \ref tscb::dispatch "dispatch"
	global function, so a shorter equivalent of the above would
	be:

	\code
		tscb::ioready_dispatcher *io;
		tscb::timerqueue_dispatcher *timers;

		io = tscb::create_ioready_dispatcher();
		timers = new tscb::timerqueue_dispatcher(io->get_eventtrigger());

		// run the dispatcher loop
		while(true) {dispatch(timers, io);}

	\endcode

*/

namespace tscb {

/**
	\brief Dispatch timer and/or io readiness events

	\param tq
		Events pending on this timer queue may be dispatched
	\param io
		Descriptors from this set will be watched for IO readiness
		and events dispatched to registered receivers

	Try to dispatch at least one timer event; wait until the next
	timer event is due, and try to dispatch at least one io
	readiness event during this time. Return after one io readiness
	event has been handled or the timeout has been reached.

	This function should be called in an endless loop from a
	dispatching thread; a call to this function will suspend the
	calling thread if there is no work to do.

	Sometimes it is necessarily to wake up the suspended thread
	prematurely; this can be accomplished in several different
	ways

	- register an ioready callback for a loopback connection
	(e.g. pipe); the function will return immediately if
	any ioready callbacks have been processed

	- set an \ref eventtrigger that the io readiness event
	dispatcher is associated with
*/
void dispatch(
	tscb::timer_dispatcher *tq,
	tscb::ioready_dispatcher *io);

/**
	\brief Queue of work items to be performed

	This class provides the interface for submitting
	functions calls to be performed at some later point in time.
*/

/**
	\brief Posix reactor service provider

	This class implements the \ref posix_reactor_service interface
	and is capable of running stand-alone to provide the requested
	notifications.
*/
class posix_reactor : public posix_reactor_service {
public:
	posix_reactor() throw(std::bad_alloc, std::runtime_error);
	virtual ~posix_reactor() noexcept;

	/**
		\brief Run the dispatcher
	*/
	void dispatch();

	/**
		\brief Dispatch pending events, but do not wait

		\return
			Whether any event was processed

		Processes a number of events (not necessarily all) that are pending
		currently. Returns "true" if any event was processed (in which
		case it usually makes sense to call the function again to check
		for further events), or "false" if no event can be processed at
		the moment.
	*/
	bool dispatch_pending();

	/**
		\brief Dispatch all pending events, but do not wait

		\return
			Whether any event was processed

		Processes all pending events, but do not wait for new events
		to arrive.
	*/
	void dispatch_pending_all();

	/* workqueue_service */
	void
	post(std::function<void()> function) /*throw(std::bad_alloc)*/ override;

	/* timer_service */

	timer_connection
	timer(
		std::function<void(std::chrono::steady_clock::time_point)> function,
		std::chrono::steady_clock::time_point when)
		/* throw(std::bad_alloc)*/ override;

	timer_connection
	one_shot_timer(
		std::function<void(std::chrono::steady_clock::time_point)> function,
		std::chrono::steady_clock::time_point when)
		/* throw(std::bad_alloc)*/ override;

	timer_connection
	suspended_timer(
		std::function<void(std::chrono::steady_clock::time_point)> function)
		/* throw(std::bad_alloc)*/ override;

	timer_connection
	one_shot_suspended_timer(
		std::function<void(std::chrono::steady_clock::time_point)> function)
		/* throw(std::bad_alloc)*/ override;

	/* ioready_service */

	ioready_connection
	watch(
		std::function<void(tscb::ioready_events)> function,
		int fd, tscb::ioready_events event_mask) /* throw(std::bad_alloc) */
			override;

	/* async_safe_work_service */
	async_safe_connection
	async_procedure(std::function<void()> function) override;

	eventtrigger &
	get_eventtrigger() /*throw(std::bad_alloc)*/ override;

protected:
	std::unique_ptr<ioready_dispatcher> io_;
	eventtrigger & trigger_;
	timer_dispatcher timer_;

	class workitem {
	public:
		workitem(std::function<void()> function)
			: function_(std::move(function)) {}

		std::function<void()> function_;
		workitem * prev_;
		workitem * next_;
	};

	class workitem_list {
	public:
		std::atomic<workitem *> first_;
		workitem * last_;

		inline workitem_list() : first_(nullptr), last_(nullptr) {}
		inline ~workitem_list()
		{
			for (workitem * item = pop(); item; item = pop()) {
				delete item;
			}
		}

		inline void push_back(workitem * item) noexcept
		{
			item->prev_ = last_;
			item->next_ = nullptr;
			if (last_) {
				last_->next_ = item;
			} else {
				first_.store(item, std::memory_order_relaxed);
			}
			last_ = item;
		}

		inline workitem * pop() noexcept
		{
			workitem * item = first_.load(std::memory_order_relaxed);
			if (!item) {
				return nullptr;
			}
			if (item->next_) {
				item->next_->prev_ = nullptr;
			} else {
				last_ = nullptr;
			}
			first_.store(item->next_, std::memory_order_relaxed);
			return item;
		}

		inline bool empty() noexcept
		{
			return first_.load(std::memory_order_relaxed) == nullptr;
		}

		inline void swap(workitem_list & other) noexcept
		{
			workitem * tmp = first_.load(std::memory_order_relaxed);
			first_.store(other.first_.load(std::memory_order_relaxed), std::memory_order_relaxed);
			other.first_.store(tmp, std::memory_order_relaxed);

			tmp = last_;
			last_ = other.last_;
			other.last_ = tmp;
		}
	};

	workitem_list workqueue_;
	std::mutex workqueue_lock_;

	async_safe_work_dispatcher async_workqueue_;
};

}

#endif
