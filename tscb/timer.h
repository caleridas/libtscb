/* -*- C++ -*-
 * (c) 2019 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#ifndef TSCB_TIMER_H
#define TSCB_TIMER_H

/**
	\page timer_descr Timer callbacks

	The template class \ref tscb::generic_timer_service "generic_timer_service"
	defines the interface which receivers of timer callbacks can use to register
	themselves. The single template parameter determines the data type used to
	represent time values.

	A specialization of this template, \ref tscb::timer_service "timer_service",
	uses std::chrono::steady_clock::time_point to represent time values, and is thus suitable
	to cooperate with boost::posix_time::microsec_clock and
	\ref tscb::ioready_dispatcher "ioready_dispatcher" to dispatch wall clock
	timer events.

	\section timer_registration "Registration for events"

	Interested receivers can register functions to be called at specified points
	in time at the \ref tscb::generic_timer_service "generic_timer_service" interface. Receivers
	can use the \ref tscb::generic_timer_service::timer "generic_timer_service::timer" functions
	for this purpose; they can be used in the following fashion:

	\code
		class TimerHandler {
		public:
			bool onTimer(long time) noexcept
			{
				// perform timer action
				time+=1000;
				return true; // rearm timer 1000 units in the future
			}
			void finish() noexcept
			{
				delete this;
			}
			void register(tscb::generic_timer_service<long> *service) throw(std::bad_alloc)
			{
				// call us at point "6000" in time
				service->timer(boost::bind(&TimerHandler::onTimer, this, _1),
					6000);
			}
		}
	\endcode

	In the previous example, the <TT>onTimer</TT> method of the corresponding object
	will be called at the specified point in time; moreover the timer will
	rearm itself 1000 "units" in the future by returning <TT>true</TT>, effectively
	creating an (imprecise) periodic timer (see section \ref periodic_timers below
	how to create precise periodic timers). Returning <TT>false</TT> from the
	timer callback functions indicates that it does not want to be called again.

	The callback function is passed the current time (as it is known to the
	callback service provider) as first argument. It is very likely that this
	is slightly <I>after</I> the desired point in time (due to notification and
	scheduling latency); the implementation can find out by what amount of time
	the callback is to late by inspecting this value and comparing with the
	desired point in time.


	\section timer_callback_cookies Callback link handles for timer callbacks

	The \ref tscb::generic_timer_service::timer "generic_timer_service::timer" functions returns a
	reference to a callback link object that represents the connection between the
	callback service provider and the receiver. The return value
	can be stored by the caller:

	\code
		tscb::timer_callback link;
		link=service->timer<TimerHandler, &TimerHandler::onTimer, &IOHandler::finish>
			(tscb::current_time()+1000, this);
	\endcode

	The link object can later be used to cancel the timer:

	\code
		link->disconect();
	\endcode

	Afterwards, the timer function will not be called again. If the timer
	callback function is running concurrently in another thread and it
	returns <TT>True</TT> (indicating that it would like to rearm itself)
	it is cancelled <I>nevertheless</I>. Calling <TT>disconnect</TT> always
	takes precedence.

	\section periodic_timers Precise periodic timers

	Since timer callback functions may be called with a slight "lag", it is not
	possible to implement precise periodic timers by simply incrementing the
	current time with a fixed value to calculate the next callback time.
	Instead the receiver has to keep track of the "originally desired" point
	in time and used that as a base instead:

	\code
		class TimerHandler {
		public:
			long long next_callback_due;

			bool onTimer(long long time) noexcept
			{
				// calculate amount of time we are late
				long long lag=time-next_callback_due;
				// perform timer action

				next_callback_due += 1000;
				time = next_callback_due;
				return true; // rearm timer 1000 usecs in the future
			}
			void finish() noexcept
			{
				delete this;
			}
			void register(tscb::timer_service *service) throw(std::bad_alloc)
			{
				next_callback_due=current_time()+1000;

				// call us 1000 usecs in the future
				service->timer<TimerHandler, &TimerHandler::onTimer, &TimerHandler::finish>
					(tscb::next_callback_due, this);
			}
		}
	\endcode

	Programmers are encouraged to use this feature to provide if they want to
	be called again (either periodically, or with varying timeouts);
	it is more efficient to rearm an existing timer with a new timeout value
	than to unregister an expired timer and create a new one.

	\section timerqueue_dispatcher_descr Timer dispatchers

	Free-standing implementations of the \ref tscb::timer_service "timer_service"
	interface suitable for timer event dispatching are provided by the
	\ref tscb::generic_timerqueue_dispatcher "generic_timerqueue_dispatcher"
	class template (and its specialization,
	\ref tscb::timerqueue_dispatcher "timerqueue_dispatcher", respectively).
	The implementations use a fibonacci heap (see \ref fibheap_descr) to
	store pending timers.

	The \ref tscb::generic_timerqueue_dispatcher "generic_timerqueue_dispatcher"
	provides the \ref tscb::generic_timerqueue_dispatcher::run_queue "run_queue"
	member function; it expects the current time value to be given and
	will process all timers that have expired up to this point in time.
	If timers are added/modified during this queue run so that their
	expiry time is equal to, or before the current time, they will
	be processed as well.

	The function returns true if there are timers pending after processing
	the queue, and will indicate the point in time when the next callback
	is due. It should be called in the following fashion:

	\code
		tscb::timerqueue_dispatcher *dispatcher;
		...
		long long next_timer;
		long long now = tscb::current_time();
		bool timer_pending;
		do {
			next_timer=now;
			// process timers
			timer_pending=dispatcher->run_queue(next_timer);
			// no more timers pending? -> abort
			if (!timer_pending) break;
			// re-read current time
			now=tscb::current_time();
			// compare with next pending timer and run again, if required
		} while(now >= next_timer);

		// wait appropriate amount of time, until next timer is due
		if (timer_pending)
			sleep_milliseconds(next_timer-now);
	\endcode

	As shown above, the current time should be rechecked after processing the
	timer queue because processing the timer callbacks takes its time as well.

	The returned time value can be used to calculate the time to "sleep"
	until the next timer is due. Since timers can be added during the
	"sleep" period, the dispatcher cooperates with an
	\ref tscb::eventflag "eventflag" to interrupt the sleep if a timer
	has been added that would expire within the sleep period:

	\code
		tscb::eventflag * flag;
		tscb::timerqueue_dispatcher * dispatcher;
		...
		dispatcher = new tscb::timerqueue_dispatcher(flag);
	\endcode

	The caller must <I>atomically</I> wait on the eventflag and the
	timeout value calculated above. One way to achieve this is to
	couple the timer dispatching with an
	\ref tscb::ioready_dispatcher "ioready_dispatcher"; see
	\ref dispatcher_descr how to simultaneously dispatch timer
	and io events from one thread.
*/

/**
	\file timer
*/

#include <chrono>
#include <new>
#include <mutex>

#include <hcb/adt/fibonacci-heap.h>
#include <hcb/adt/intrusive-list.h>

#include <tscb/signal.h>
#include <tscb/eventflag.h>

#include <sys/time.h>
#include <time.h>

namespace tscb {

template<typename TimePoint>
class basic_timer_connection {
public:
#if 0
	static_assert(
		noexcept(std::declval<TimePoint &>() < std::declval<TimePoint &>()),
		"require noexcept time point comparison");
#endif
	static_assert(
		noexcept(std::declval<TimePoint &>() = std::declval<TimePoint &>()),
		"require noexcept time point assignment");
	static_assert(
		noexcept(std::declval<TimePoint &>() = std::move(std::declval<TimePoint &>())),
		"require noexcept time point assignment");

	/**
		\brief callback link for I/O readiness events on file descriptors

		This class represents a link that has been established by registering
		a callback for I/O readiness events on file (or socket) descriptor
		events (see \ref tscb::ioready_service). Like its base class,
		\ref tscb::abstract_callback, it supports cancellation, but additionally
		it also allows to dynamically change the event notification mask
		(which is much more efficient than to cancel the previous callback and
		register a new one).
	*/
	class link_type : public connection::link_type {
	public:
		using pointer = boost::intrusive_ptr<link_type>;

		~link_type() noexcept override
		{
		}

		void
		disconnect() noexcept override = 0;

		bool
		is_connected() const noexcept override = 0;

		virtual void
		set(TimePoint when) noexcept = 0;

		virtual const TimePoint &
		when() const noexcept = 0;

		virtual void
		suspend() noexcept = 0;

		virtual bool
		is_suspended() const noexcept = 0;
	};

	inline
	basic_timer_connection() noexcept {}

	inline explicit
	basic_timer_connection(boost::intrusive_ptr<link_type> link) noexcept
		: link_(std::move(link))
	{
	}

	inline
	basic_timer_connection(const basic_timer_connection & other) noexcept
		: link_(other.link_)
	{
	}

	inline
	basic_timer_connection(basic_timer_connection && other) noexcept
		: link_(std::move(other.link_))
	{
	}

	inline basic_timer_connection &
	operator=(const basic_timer_connection & other) noexcept
	{
		link_ = other.link_;
		return *this;
	}

	inline basic_timer_connection &
	operator=(basic_timer_connection && other) noexcept
	{
		link_ = std::move(other.link_);
		return *this;
	}

	inline void
	swap(basic_timer_connection & other) noexcept
	{
		link_.swap(other.link_);
	}

	inline void
	disconnect() noexcept
	{
		if (link_) {
			link_->disconnect();
			link_.reset();
		}
	}

	inline bool
	is_connected() const noexcept
	{
		return link_ && link_->is_connected();
	}

	inline void
	set(TimePoint when) noexcept
	{
		if (link_) {
			link_->set(when);
		}
	}

	inline void
	suspend() noexcept
	{
		if (link_) {
			link_->suspend();
		}
	}

	inline const typename link_type::pointer &
	link() const noexcept
	{
		return link_;
	}

	inline link_type *
	get() const noexcept
	{
		return link_.get();
	}

	inline bool
	suspended() const noexcept
	{
		if (link_) {
			return link_->is_suspended();
		} else {
			return true;
		}
	}

	inline TimePoint
	when() const noexcept
	{
		if (link_) {
			return link_->when();
		} else {
			return {};
		}
	}

	inline operator connection() const noexcept
	{
		return connection(link_);
	}

private:
	typename link_type::pointer link_;
};

/**
	\brief Scoped timer connection

	Variant of \ref timer_connection object that automatically
	breaks the connection when this object goes out of scope.

	\warning This class can be used by an object to track
	signal connections to itself, and have all connections
	broken automatically when the object is destroyed.
	Only do this when you know that all callback invocations
	as well as the destructor will always run from the
	same thread.
*/
template<typename TimePoint>
class basic_scoped_timer_connection {
public:
	inline ~basic_scoped_timer_connection() noexcept {
		disconnect();
	}

	inline basic_scoped_timer_connection() noexcept {}
	inline basic_scoped_timer_connection(const basic_timer_connection<TimePoint> & c) noexcept : connection_(c) {}
	inline basic_scoped_timer_connection(basic_timer_connection<TimePoint> && c) noexcept : connection_(std::move(c)) {}

	basic_scoped_timer_connection(const basic_scoped_timer_connection & other) = delete;
	basic_scoped_timer_connection & operator=(const basic_scoped_timer_connection & other) = delete;

	basic_scoped_timer_connection(basic_scoped_timer_connection && other) noexcept
	{
		swap(other);
	}
	basic_scoped_timer_connection &
	operator=(basic_scoped_timer_connection && other) noexcept
	{
		swap(other);
		return *this;
	}

	inline void
	swap(basic_scoped_timer_connection & other) noexcept
	{
		connection_.swap(other.connection_);
	}

	inline bool
	is_connected() const noexcept
	{
		return connection_.is_connected();
	}

	inline void
	set(TimePoint when) noexcept
	{
		connection_.set(std::move(when));
	}

	inline void
	suspend() noexcept
	{
		connection_.suspend();
	}

	inline void disconnect() noexcept
	{
		connection_.disconnect();
	}

	inline basic_scoped_timer_connection &
	operator=(const basic_timer_connection<TimePoint> & c) noexcept
	{
		disconnect();
		connection_ = c;
		return *this;
	}

protected:
	basic_timer_connection<TimePoint> connection_;
};


/**
	\brief Registration for timer events

	This class provides the registration interface for timer callbacks.
	Receivers can use the \ref timer methods of
	this class to indicate their interest in receiving callbacks
	at defined points in time. See section \ref timer_registration
	for examples on how to use this interface.

	The single template parameter, <TT>Timeval</TT>, of this class determines
	the data type used to represent points in time. The most
	useful specialization is given by \ref tscb::timer_service "timer_service"
	which represents time as 64 bit integers and is intended to cooperate with
	\ref current_time to provide a timer callback mechanism with
	microsecond resolution.
*/
template<typename TimePoint>
class basic_timer_service {
public:
	virtual
	~basic_timer_service() noexcept
	{
	}

	/**
		\brief register callback for timer event

		\param function
			Function to be called at specific point in time
		\param when
			The point in time when the callback should be executed

		This function requests a callback at a given point in time. The
		point in time is given as an absolute value, so if you want a
		relative point in time, use \ref tscb::current_time and add an
		appropriate time interval.

		No hard real-time guarantees are made about when the callback
		will be executed. It will usually be executed a little after the
		point in time indicated by <CODE>expires</CODE>, dependent on
		the resolution of the system clock (about 1 millisecond for
		most systems) and load of the system.

		For this reason the function is called with the current time (as
		known to the caller), so the callback can determine how much it is
		too late and act accordingly.

		The called function may either return <CODE>false</CODE>, in
		which case the timer is considered "cancelled" and will not
		be called again; or it may modify the value passed in
		<CODE>now</CODE> and return <CODE>true</CODE>, in which case it
		will be called again at the new point in time.
	*/
	virtual basic_timer_connection<TimePoint>
	timer(
		std::function<void(TimePoint)> function, TimePoint when)
		/* throw(std::bad_alloc)*/ = 0;

	virtual basic_timer_connection<TimePoint>
	one_shot_timer(
		std::function<void(TimePoint)> function, TimePoint when)
		/* throw(std::bad_alloc)*/ = 0;

	virtual basic_timer_connection<TimePoint>
	suspended_timer(
		std::function<void(TimePoint)> function)
		/* throw(std::bad_alloc)*/ = 0;

	virtual basic_timer_connection<TimePoint>
	one_shot_suspended_timer(
		std::function<void(TimePoint)> function)
		/* throw(std::bad_alloc)*/ = 0;
};

/**
	\brief Timer queue dispatcher

	This implementation of the \ref tscb::timer_service "timer_service"
	interface stores pending timers in a fibonacci heap and takes
	care of dispatching callbacks at appropriate points in time.
	To accomplish its task it requires \ref run_queue to be called
	periodically.

	See section \ref timerqueue_dispatcher_descr for examples how
	this class can be used.
*/
template<typename TimePoint>
class basic_timer_dispatcher : public basic_timer_service<TimePoint> {
public:
	using connection = basic_timer_connection<TimePoint>;
	class link_type final : public connection::link_type {
	public:
		using pointer = boost::intrusive_ptr<link_type>;

		~link_type() noexcept override
		{
		}

		link_type(
			basic_timer_dispatcher * master,
			std::function<void(TimePoint when)> function,
			TimePoint when,
			bool one_shot) noexcept
			: function_(std::move(function)) , when_(std::move(when))
			, master_(master) , hold_count_(1), suspended_(false)
			, one_shot_(one_shot)
		{
		}

		void
		disconnect() noexcept override
		{
			std::unique_lock<std::mutex> guard(registry_mutex_);
			basic_timer_dispatcher * master = master_.load(std::memory_order_relaxed);
			if (master) {
				{
					std::lock_guard<std::mutex> guard(master->mutex_);
					if (suspended_) {
						master->suspended_.erase(this);
					} else {
						master->active_.remove(this);
					}
					master_.store(nullptr, std::memory_order_relaxed);
				}
				guard.unlock();
				release_hold_count();
				intrusive_ptr_release(this);
			}
		}

		bool
		is_connected() const noexcept override
		{
			return master_.load(std::memory_order_relaxed) != nullptr;
		}

		void
		set(TimePoint when) noexcept override
		{
			std::lock_guard<std::mutex> guard(registry_mutex_);
			basic_timer_dispatcher * master = master_.load(std::memory_order_relaxed);
			if (master) {
				std::lock_guard<std::mutex> guard(master->mutex_);
				if (suspended_) {
					master->suspended_.erase(this);
				} else {
					master->active_.remove(this);
				}
				when_ = std::move(when);
				master->active_.insert(this);
				suspended_.store(false, std::memory_order_relaxed);
				if (master->active_.peek_min() == this && hold_count_.load(std::memory_order_relaxed) < 2) {
					master->timer_added_.set();
				}
			}
		}

		const TimePoint &
		when() const noexcept override
		{
			return when_;
		}

		void
		suspend() noexcept override
		{
			std::lock_guard<std::mutex> guard(registry_mutex_);
			basic_timer_dispatcher * master = master_.load(std::memory_order_relaxed);
			if (master) {
				std::lock_guard<std::mutex> guard(master->mutex_);
				if (suspended_) {
					master->suspended_.erase(this);
				} else {
					master->active_.remove(this);
				}
				master->suspended_.push_back(this);
				suspended_.store(true, std::memory_order_relaxed);
			}
		}

		bool
		is_suspended() const noexcept override
		{
			return suspended_.load(std::memory_order_relaxed);
		}

	private:
		inline void
		acquire_hold_count() noexcept
		{
			hold_count_.fetch_add(1, std::memory_order_relaxed);
		}

		inline void
		release_hold_count() noexcept
		{
			if (hold_count_.fetch_sub(1, std::memory_order_release) == 1) {
				std::atomic_thread_fence(std::memory_order_acquire);
				function_ = nullptr;
			}
		}

		std::function<void(TimePoint when)> function_;
		TimePoint when_;

		std::mutex registry_mutex_;
		std::atomic<basic_timer_dispatcher *> master_;
		hcb::adt::fibonacci_heap_anchor<link_type> anchor_;
		std::atomic<std::size_t> hold_count_;
		std::atomic<bool> suspended_;
		bool one_shot_;

		using heap_accessor = hcb::adt::fibonacci_heap_accessor<link_type, &link_type::anchor_>;
		class list_accessor {
		public:
			inline link_type *
			get_prev(const link_type * link) const noexcept
			{
				return link->anchor_.prev;
			}
			inline void
			set_prev(link_type * link, link_type * prev) const noexcept
			{
				link->anchor_.prev = prev;
			}
			inline link_type *
			get_next(const link_type * link) const noexcept
			{
				return link->anchor_.next;
			}
			inline void
			set_next(link_type * link, link_type * next) const noexcept
			{
				link->anchor_.next = next;
			}
		};
		struct before {
			inline bool operator()(const link_type & x, const link_type & y) noexcept
			{
				return x.when_ < y.when_;
			}
		};

		friend class basic_timer_dispatcher;
	};

	~basic_timer_dispatcher() noexcept override
	{
		while (detach_registered()) {}
	}

	inline explicit
	basic_timer_dispatcher(eventtrigger & timer_added)
		: timer_added_(timer_added), running_(0)
	{
	}

	/**
		\brief Determine when next timer is due (if any)

		Returns whether any timer is pending at all and the time point the
		earliest one is due.
		Note that this check may race with registration of
		new timers from other threads; to avoid missing timers
		the caller should therefore

		1. clear the \ref eventflag the timer dispatcher is
		associated with

		2. check if timers are pending

		3. atomically wait for timeout and the \ref eventflag

		If a timer is inserted between 2 and 3, the dispatcher
		thread will not wait in step 3, and a new iteration of the
		dispatching loop must be started.
	*/
	std::pair<bool, TimePoint>
	next_timer() const noexcept
	{
		std::unique_lock<std::mutex> guard(mutex_);
		link_type * link = active_.peek_min();
		if (link) {
			return {true, link->when_};
		} else {
			return {false, TimePoint()};
		}
	}

	/**
		\brief Run all due timers

		\param now
			Current point in time -- all timers expiring at or before this
			point in time will be executed.
		\param limit
			Upper bound for number timers executed
		\returns
			Number of timer functions executed

		Runs all timers which are due at the given point in time. (See
		\ref run_single below).
	*/
	std::size_t
	run(TimePoint now, std::size_t limit = std::numeric_limits<std::size_t>::max())
	{
		std::size_t count = 0;
		while (count < limit) {
			if (run_single(now)) {
				count += 1;
			} else {
				break;
			}
		}
		return count;
	}

	/**
		\brief Run single if any due

		\param now
			Current point in time -- any timer expiring at or before this
			point in time can be executed.
		\returns
			true if any timer callback was run, or false otherwise

		Determines the earliest timer due at given point in time. If there
		is any, it is first changed into "suspended" state. If the timer is
		of "one-shot" kind, it will automatically be disconnected.
		Afterwards, the associated callback function is run (and it may
		re-arm the timer).
	*/
	bool
	run_single(TimePoint now)
	{
		typename link_type::pointer link;
		{
			std::unique_lock<std::mutex> guard(mutex_);
			link_type * l = active_.peek_min();
			if (!l || now < l->when_) {
				return false;
			}
			link.reset(active_.extract_min(), false);
			intrusive_ptr_add_ref(link.get());
			suspended_.push_back(l);
			link->suspended_.store(true, std::memory_order_relaxed);
			link->acquire_hold_count();
		}

		if (link->one_shot_) {
			link->disconnect();
		}

		link->function_(now);

		// XXX: guard against exception thrown by function call
		link->release_hold_count();

		return true;
	}

	connection
	timer(
		std::function<void (TimePoint)> function, TimePoint when)
		/* throw(std::bad_alloc)*/ override
	{
		typename link_type::pointer link(new link_type(this, std::move(function), when, false));
		return register_timer(std::move(link), false);
	}

	basic_timer_connection<TimePoint>
	one_shot_timer(
		std::function<void(TimePoint)> function, TimePoint when)
		/* throw(std::bad_alloc)*/ override
	{
		typename link_type::pointer link(new link_type(this, std::move(function), when, true));
		return register_timer(std::move(link), false);
	}

	basic_timer_connection<TimePoint>
	suspended_timer(
		std::function<void(TimePoint)> function)
		/* throw(std::bad_alloc)*/ override
	{
		typename link_type::pointer link(new link_type(this, std::move(function), {}, false));
		return register_timer(std::move(link), true);
	}

	basic_timer_connection<TimePoint>
	one_shot_suspended_timer(
		std::function<void(TimePoint)> function)
		/* throw(std::bad_alloc)*/ override
	{
		typename link_type::pointer link(new link_type(this, std::move(function), {}, true));
		return register_timer(std::move(link), true);
	}

private:
	inline connection
	register_timer(
		typename link_type::pointer link, bool suspended) noexcept
	{
		bool need_wakeup = false;

		{
			std::lock_guard<std::mutex> guard(mutex_);
			intrusive_ptr_add_ref(link.get());
			link->suspended_.store(suspended, std::memory_order_relaxed);
			if (suspended) {
				suspended_.push_back(link.get());
			} else {
				active_.insert(link.get());
				need_wakeup = active_.peek_min() == link;
			}
		}

		if (need_wakeup) {
			timer_added_.set();
		}

		return connection(std::move(link));
	}

	bool
	detach_registered() noexcept
	{
		typename link_type::pointer link;
		{
			std::unique_lock<std::mutex> guard(mutex_);
			link.reset(active_.extract_min(), false);
			if (!link && !suspended_.empty()) {
				link.reset(suspended_.begin().ptr(), false);
				suspended_.erase(link.get());
			}
			if (link) {
				link->master_.store(nullptr, std::memory_order_relaxed);
			}
		}
		if (link) {
			link->function_ = nullptr;
		}
		return link != nullptr;
	}

private:
	using heap_type = hcb::adt::fibheap<
		link_type,
		typename link_type::heap_accessor,
		typename link_type::before
	>;
	using list_type = hcb::adt::intrusive_list<
		link_type,
		typename link_type::list_accessor
	>;

	mutable std::mutex mutex_;
	heap_type active_;
	list_type suspended_;
	eventtrigger & timer_added_;
	std::size_t running_;
};

extern template class basic_timer_connection<std::chrono::steady_clock::time_point>;
extern template class basic_scoped_timer_connection<std::chrono::steady_clock::time_point>;
extern template class basic_timer_service<std::chrono::steady_clock::time_point>;
extern template class basic_timer_dispatcher<std::chrono::steady_clock::time_point>;

using timer_connection = basic_timer_connection<std::chrono::steady_clock::time_point>;
using scoped_timer_connection = basic_scoped_timer_connection<std::chrono::steady_clock::time_point>;
using timer_service = basic_timer_service<std::chrono::steady_clock::time_point>;
using timer_dispatcher = basic_timer_dispatcher<std::chrono::steady_clock::time_point>;

}

#endif
