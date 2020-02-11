/* -*- C++ -*-
 * (c) 2010 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#include <tscb/workqueue.h>

/**
	\page workqueue_descr Workqueue interface

	The \ref tscb::workqueue_service interface provides a
	mechanism for queueing up work to be performed later
	("deferred procedure calls").

*/

namespace tscb {

/**
	\class workqueue_service
	\brief Deferred procedure registration service.
	\headerfile tscb/workqueue.h <tscb/workqueue.h>

	Provides the interface to register deferred procedures for later
	execution. This class provides the interface for submitting
	functions calls to be performed at some later point in time.


	\fn workqueue_service::register_deferred_procedure
	\brief Register a triggerable work procedure.
	\param function Procedure to be called.
	\returns Connection handle and trigger function
	\throws std:bad_alloc if resources exhausted to register procedure

	Registers the given procedure call to be executed when triggered.
	When the given trigger function is called, the previously registered
	procedure is to be executed eventually.

	The trigger function is thread-safe: It can be called in any
	thread, registered procedure will be called in the thread (or one
	of the threads) handling this workqueue. The trigger function is
	<I>NOT</I> async-signal safe, see \ref
	register_async_deferred_procedure.

	The trigger procedure itself is \p noexcept.


	\fn workqueue_service::register_async_deferred_procedure
	\brief Register a triggerable work procedure.
	\param function Procedure to be called.
	\returns Connection handle and trigger function
	\throws std:bad_alloc if resources exhausted to register procedure

	Registers the given procedure call to be executed when triggered.
	When the given trigger function is called, the previously registered
	procedure is to be executed eventually.

	The trigger function is thread-safe and async-signal safe: It can be
	called in any thread or even from signal handler context. It is
	recommended to use this mechanism only when async-signal safety
	is required, otherwise use \ref register_deferred_procedure .

	The trigger procedure itself is \p noexcept.


	\fn workqueue_service::queue_procedure
	\brief Queue a single procedure call
	\param function Procedure to be called.
	\throws std:bad_alloc if resources exhausted to register into queue

	Queues the given procedure call for later execution. All queued
	procedures will be run one after another -- generally, in the order
	they were enqueued although in case of multi-threaded dispatching
	another call may be initiated before the previous finishes.
	Dispatcher ensures that work scheduled in this way never starves
	other event sources.

	This function is thread-safe, can be called concurrently to
	execution of any queued work items. It is <I>not</I> async-signal
	safe, see \ref register_async_deferred_procedure.

	This mechanism should be used for "ad-hoc" queued work. See also
	\ref register_deferred_procedure which is to be preferred for
	repeated work. Also note that the trigger procedure returned by
	\ref register_deferred_procedure is \p noexcept while this
	registration function may throw.
*/

workqueue_service::~workqueue_service() noexcept
{
}

/* exclude private nested class from doxygen */
/** \cond false */

class workqueue::link_type final : public connection::link_type {
public:
	using pointer = detail::intrusive_ptr<link_type>;

	~link_type() noexcept override
	{
	}

	inline
	link_type(
		std::function<void()> function,
		workqueue * service) noexcept
		: async_trigger_next_(nullptr)
		, service_(service)
		, state_(state_type::INACTIVE)
		, function_(std::move(function))
	{
	}

	void
	disconnect() noexcept override
	{
		{
			std::lock_guard<std::mutex> guard1(registration_mutex_);
			workqueue* service = service_.load(std::memory_order_relaxed);

			if (!service) {
				return;
			}

			{
				std::lock_guard<std::mutex> guard2(service->lock_);

				state_type state = state_.load(std::memory_order_acquire);
				do {
					if (state == state_type::ASYNC_TRIGGER_CRITICAL) {
						state = state_.load(std::memory_order_acquire);
						continue;
					}
					if (state == state_type::DISCONNECTING) {
						return;
					}
				} while (!state_.compare_exchange_weak(state, state_type::DISCONNECTING, std::memory_order_relaxed));

				pointer ptr;
				if (state == state_type::ACTIVE) {
					list_erase(service->active_, this);
				} else {
					list_erase(service->inactive_, this);
				}
			}
		}

		function_ = std::function<void()>();
		intrusive_ptr_release(this);
	}

	bool
	is_connected() const noexcept override
	{
		state_type state = state_.load(std::memory_order_relaxed);
		return state != state_type::DISCONNECTING;
	}

	void
	normal_trigger() noexcept
	{
		std::lock_guard<std::mutex> guard(registration_mutex_);
		workqueue* service = service_.load(std::memory_order_relaxed);
		if (service) {
			std::lock_guard<std::mutex> guard(service->lock_);

			state_type state = state_.load(std::memory_order_relaxed);
			if (state != state_type::INACTIVE) {
				return;
			}

			list_erase(service->inactive_, this);
			list_insert(service->active_, nullptr, this);

			state_.store(state_type::ACTIVE, std::memory_order_relaxed);

			service->trigger();
		}
	}

	void
	async_safe_trigger() noexcept
	{
		state_type state = state_.load(std::memory_order_relaxed);

		/*
		 * Try atomic INACTIVE -> ASYNC_TRIGGER_CRITICAL transition, see
		 * state diagram.
		 * Failing this, we cannot trigger (either it is triggered
		 * already, or disconnected).
		 */
		do {
			if (state != state_type::INACTIVE) {
				return;
			}
		} while (!state_.compare_exchange_weak(state, state_type::ASYNC_TRIGGER_CRITICAL, std::memory_order_acquire));

		workqueue* service = service_.load(std::memory_order_relaxed);
		if (!service) {
			state_.store(state_type::INACTIVE, std::memory_order_relaxed);
			return;
		}

		intrusive_ptr_add_ref(this);

		link_type* tmp = service->async_triggered_.load(std::memory_order_relaxed);
		do {
			async_trigger_next_ = tmp;
		} while (!service->async_triggered_.compare_exchange_weak(tmp, this, std::memory_order_release));

		service->trigger();

		/*
		 * Conclude critical section.
		 */
		state_.store(state_type::ASYNC_TRIGGERED, std::memory_order_release);
	}

private:
	/*
	 * Possible state transitions:
	 *
	 * - INACTIVE -> ACTIVE:
	 *   This occurs in "normal" (not async-safe) triggering and
	 *   includes moving the procedure from inactive -> active list
	 *   LOCKS HELD: service_->lock_ held
	 *
	 * - INACTIVE -> DISCONNECTING, ACTIVE -> DISCONNECTING:
	 *   Occurs on "disconnect", removes procedure from inactive /
	 *   active set
	 *   LOCKS HELD: service_->lock_ held
	 *
	 * - INACTIVE -> ASYNC_TRIGGER_CRITICAL:
	 *   Occurs as the first step of async-safe triggering, when signal
	 *   handler code enters the critical region of queuing a notifier
	 *   for execution. This is the only way for "normal" code to know
	 *   that a signal handler might be in the critical region.
	 *   The ASYNC_TRIGGER_CRITICAL state acts as a spinlock that
	 *   in a signal handler in another thread occurs.
	 *   prevents destructor of workqueue to finish while triggering
	 *   LOCKS HELD: None
	 *
	 * - ASYNC_TRIGGER_CRITICAL -> ASYNC_TRIGGERED:
	 *   Occurs as the last step of async-safe triggering, when signal
	 *   handler code leaves the critical region. On observing this
	 *   state, normal code knows that signal handler has left
	 *   critical region.
	 *   Procedure is in the async_triggered_ set, but is still on
	 *   the inactive list.
	 *   LOCKS HELD: None
	 *
	 * - ASYNC_TRIGGERED -> DISCONNECTING:
	 *   Occurs on "disconnect", removes procedure from inactive set.
	 *   LOCKS HELD: service_->lock_ held
	 *
	 * - ASYNC_TRIGGERED -> ACTIVE
	 *   Removes procedure from async_triggered_set_, and transfers it
	 *   from inactive -> active list
	 *   LOCKS HELD: service_->lock_ held
	 */
	enum class state_type {
		INACTIVE,
		ASYNC_TRIGGER_CRITICAL,
		ASYNC_TRIGGERED,
		ACTIVE,
		DISCONNECTING
	};

	struct list_anchor {
		link_type* prev;
		link_type* next;
	};

	state_type sync_with_async_safe_trigger()
	{
		state_type state = state_.load(std::memory_order_relaxed);
		while (state == state_type::ASYNC_TRIGGER_CRITICAL) {
			state = state_.load(std::memory_order_relaxed);
		}
		std::atomic_thread_fence(std::memory_order_acquire);
		return state;
	}

	list_anchor anchor_;
	link_type* async_trigger_next_;
	std::atomic<workqueue*> service_;
	std::atomic<state_type> state_{state_type::INACTIVE};
	std::function<void()> function_;

	std::mutex registration_mutex_;

	friend class workqueue;
};

class workqueue::retrigger_guard final {
public:
	explicit retrigger_guard(workqueue& wq) noexcept
		: wq_(wq), active_(true)
	 {}
	~retrigger_guard()
	{
		if (active_) {
			wq_.trigger();
		}
	}

	void
	deactivate() noexcept
	{
		active_ = false;
	}
private:
	workqueue& wq_;
	bool active_;
};

/** \endcond */


workqueue::~workqueue()
{
	for (;;) {
		link_type::pointer p = get_registered();
		if (!p) {
			break;
		}
		p->disconnect();
	}
}

/**
	\class workqueue
	\brief Deferred procedure handler service.
	\headerfile tscb/workqueue.h <tscb/workqueue.h>

	Provides the mechanism to dispatch registered deferred procedures.
*/

/**
	\brief Construct workqueue dispatcher
	\param trigger
		Trigger function called when new work scheduled.
		<B>Must be async-signal safe</B>.

	Instantiates work queue dispatcher. It dispatches registered
	and triggered work functions. The work functions can be triggered
	in async-safe or non-async-safe fashion. See \ref
	ioready_dispatcher::wake_up for possible async-safe function that
	can be passed as argument to \p trigger.
*/
workqueue::workqueue(std::function<void()> trigger)
	: async_triggered_(nullptr)
	, active_{nullptr, nullptr}
	, inactive_{nullptr, nullptr}
	, pending_(false)
	, trigger_(std::move(trigger))
{
}

std::pair<connection, std::function<void()>>
workqueue::register_deferred_procedure(std::function<void()> function)
{
	link_type::pointer p(new link_type(std::move(function), this));
	std::function<void()> trigger_fn = std::bind(&link_type::normal_trigger, link_type::pointer(p));

	std::pair<connection, std::function<void()>> result(connection(p), std::move(trigger_fn));

	{
		std::lock_guard guard(lock_);
		list_insert(inactive_, nullptr, p.detach());
	}

	return result;
}

std::pair<connection, std::function<void()>>
workqueue::register_async_deferred_procedure(std::function<void()> function)
{
	link_type::pointer p(new link_type(std::move(function), this));
	std::function<void()> trigger_fn = std::bind(&link_type::async_safe_trigger, link_type::pointer(p));

	std::pair<connection, std::function<void()>> result(connection(p), std::move(trigger_fn));

	{
		std::lock_guard guard(lock_);
		list_insert(inactive_, nullptr, p.detach());
	}

	return result;
}

void
workqueue::queue_procedure(std::function<void()> function)
{
	{
		std::lock_guard guard(lock_);
		work_.push_back(std::move(function));
	}

	trigger();
}

workqueue::link_type::pointer
workqueue::get_registered() noexcept
{
	std::lock_guard guard(lock_);
	if (active_.first) {
		return link_type::pointer(active_.first);
	} else if (inactive_.first) {
		return link_type::pointer(inactive_.first);
	} else {
		return {};
	}
}

void
workqueue::list_erase(list_type& list, link_type* element) noexcept
{
	link_type* prev = element->anchor_.prev;
	link_type* next = element->anchor_.next;
	if (prev) {
		prev->anchor_.next = next;
	} else {
		list.first = next;
	}
	if (next) {
		next->anchor_.prev = prev;
	} else {
		list.last = prev;
	}
}

void
workqueue::list_insert(list_type& list, link_type* pos, link_type* element) noexcept
{
	link_type* next = pos;
	link_type* prev = pos ? pos->anchor_.prev : list.first;

	element->anchor_.next = next;
	element->anchor_.prev = prev;
	if (next) {
		next->anchor_.prev = element;
	} else {
		list.last = element;
	}
	if (prev) {
		prev->anchor_.next = element;
	} else {
		list.first = element;
	}
}


/* called with lock_ held */
void
workqueue::transfer_async_triggered() noexcept
{
	link_type* head = async_triggered_.exchange(nullptr, std::memory_order_acquire);
	if (!head) {
		return;
	}

	link_type* insert_before = nullptr;
	while (head) {
		link_type* next = head->async_trigger_next_;

		link_type::state_type state = head->sync_with_async_safe_trigger();

		if (state == link_type::state_type::ASYNC_TRIGGERED) {
			list_erase(inactive_, head);
			list_insert(active_, insert_before, head);
			head->state_.store(link_type::state_type::ACTIVE, std::memory_order_relaxed);
			insert_before = head;
		} else if (state == link_type::state_type::DISCONNECTING) {
		}

		intrusive_ptr_release(head);
		head = next;
	}
}

void
workqueue::trigger() noexcept
{
	bool was_pending = pending_.exchange(true, std::memory_order_relaxed);
	if (!was_pending) {
		trigger_();
	}
}

/**
	\brief Handle deferred procedures
	\returns Number of procedures called

	Dispatches triggered procedures (see \ref
	register_deferred_procedure and \ref
	register_async_deferred_procedure) as well queued work (see \ref
	queue_procedure). Returns the number of procedures run.

	This will always handle <I>all</I> triggered procedures but at most
	<I>one</I> queued procedure to avoid starvation. If more queued
	procedures remain after this call, then this call will also
	retrigger for later execution (see parameter to \ref
	workqueue::workqueue).

	This function never throws by itself, but any exception raised
	by any triggered or queued procedure will be passed through.
*/
std::size_t
workqueue::dispatch()
{
	if (!pending_.load(std::memory_order_relaxed)) {
		return 0;
	}
	pending_.store(false, std::memory_order_relaxed);

	retrigger_guard trigger_guard(*this);

	std::size_t count = 0;
	std::unique_lock<std::mutex> guard(lock_);
	transfer_async_triggered();

	while (active_.first) {
		link_type* element = active_.first;
		list_erase(active_, element);
		list_insert(inactive_, nullptr, element);
		element->state_.store(link_type::state_type::INACTIVE);

		guard.unlock();

		element->function_();

		guard.lock();
		++count;
	}

	if (!work_.empty()) {
		std::function<void()> fn = std::move(work_.front());
		work_.pop_front();

		bool more_work = !work_.empty();
		guard.unlock();

		fn();
		++count;

		if (more_work) {
			trigger();
		}
	}

	trigger_guard.deactivate();

	return count;
}

/**
	\fn workqueue::pending
	\brief Check whether any procedure is pending
	\returns Any procedure pending

	Checks whether any procedure is presently pending (such that
	\ref dispatch needs to be called).
*/

}
