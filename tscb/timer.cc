/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/timer.h>

/**
\file timer.cc
*/

/**
	\page timer_descr Timer callbacks

	The template class \ref tscb::basic_timer_service "basic_timer_service"
	defines the interface to request callbacks on timer events. It supports
	one-shot timers, repeated times at fixed or varying intervals as well as
	dynamically changing timeouts and suspending / reactivating timers.

	A specialization of this template, \ref tscb::timer_service "timer_service",
	uses std::chrono::steady_clock::time_point to represent time values. It is
	intended to handle typical cases in programs needing to react to
	"real-world" timed events, in cooperation with
	\ref tscb::ioready_dispatcher "ioready_dispatcher" to handle io.

	\section timer_registration Registration for events

	The basic concept of a "timer" represents an event in the future when a
	specified function needs to be called. Timers can be in "active" state with
	a set due time, or they can be in "suspended" state. Programs can change
	state of a timer (suspended, or active with due time) at any point in time.
	As soon as the callback function starts running, the timer goes into
	"suspended" state -- setting a new due tim reactivates it.

	Once created, all operations around a timer (suspending, reactivating,
	changing due time, invocation of callback) are "noexcept" and do not
	involve any memory allocations. All operations are concurrency-safe.
	Cancelling a timer destroys it in a concurrency-safe fashion and disposes
	of its resources in a safe fashion (note the difference between "suspending"
	and "cancellation": the former is a state change that can be reversed,
	the latter finalizes the timer).

	The interface for programs to use in registering timers is given by
	\ref tscb::basic_timer_service "basic_timer_service":

	- \ref tscb::basic_timer_service::timer
	  "basic_timer_service::timer" creates a timer with a specific due
	  time.

	- \ref tscb::basic_timer_service::suspended_timer
	  "basic_timer_service::suspended_timer" creates a timer in
	  suspended state -- later setting a due time activates it

	- \ref tscb::basic_timer_service::one_shot_timer
	  "basic_timer_service::one_shot_timer" create a timer with a
	  specific due time that is automatically cancelled when its
	  callback is activated; setting new due time after this point has
	  no effect. This is a convenience method, its purpose can be
	  accomplished just using the other timer functions above and
	  explicitly cancelling the timer after expiration.

	- \ref tscb::basic_timer_service::one_shot_suspended_timer
	  "basic_timer_service::one_shot_suspended_timer" combines the two
	  above

	Basic usage example:

	\code
		class MyObject {
		public:
			void on_timer(long now) noexcept
			{
				// perform timer action
				if (...) {
					timer.set(timer.when() + 1000); // rearm timer 1000 units in the future
				} else {
					timer.cancel();
				}
			}

			void setup(tscb::basic_timer_service<long>& service)
			{
				// call us at point "6000" in time
				service.timer([this](long now){on_timer(now);}, 6000);
			}

		private:
			basic_timer_connection<TimerPoint> timer_;
		}
	\endcode

	The previous example creates a periodic timer that is run repeatedly
	until some condition is met. Note that the timer callback is given the
	current time as an argument -- this may be later than the due time
	originally set, timer function may want to compensate for any delay

	\section timer_callback_connections Callback link handles for timer callbacks

	The \ref tscb::basic_timer_service::timer "basic_timer_service::timer"
	and related functions return a link handle object that represents the
	connection between timer service and target.

	\code
		tscb::timer_callback link;
		link = service.timer(callback_function, when);
	\endcode

	The timer object can be controlled through operations on this handle object:

	- \ref tscb::basic_timer_connection::when
	  "basic_timer_connection::when" returns the due time when callback
	  should be invoked

	- \ref tscb::basic_timer_connection::set "basic_timer_connection::set"
	  sets a new due time and activates the timer if it was suspended before

	- \ref tscb::basic_timer_connection::suspend
	  "basic_timer_connection::suspend" suspends the timer, it will not
	  be called to previously specified due time

	- \ref tscb::basic_timer_connection::disconnect
	  "tscb::basic_timer_connection::disconnect" breaks the link and
	  cancels the timer. Afterwards, the timer function will not be
	  called again.

	\section basic_timer_dispatcher_descr Timer dispatchers

	Most programs will want to use \ref tscb::reactor
	"tscb::reactor" for both timer and I/O handling: It
	implements all of the above interface methods for timer handling
	and handles their interaction. For specialized purposes (and as a
	backend-implementation of \ref tscb::reactor
	"tscb::reactor") there are the two free-standing
	implementations \ref tscb::basic_timer_dispatcher
	"basic_timer_dispatcher" class template and its specialization,
	\ref tscb::timer_dispatcher "timer_dispatcher".

	\ref tscb::basic_timer_dispatcher "basic_timer_dispatcher" offers
	the following methods to deal with timers:

	- \ref tscb::basic_timer_dispatcher::run "run" processes all timers
	  (up to specified limit) that are presently due

	- \ref tscb::basic_timer_dispatcher::run_single "run_single"
	  processes a single timer if it is due already

	- \ref tscb::basic_timer_dispatcher::next_timer "next_timer"
	  returns due time for next timer, if any

	In operation, the "run" method would be called repeatedly, followed
	by calls to "next_timer" to determine wait time until next call.
	To handle the obvious race condition between asynchronous timer
	changes (e.g. from other threads), the timer dispatcher interfaces
	with an event flag that is set whenever the previously returned
	wait time needs to be shortened.

	Usage example:

	\code
		tscb::timer_dispatcher* dispatcher;
		...
		std::chrono::steady_clock::time_point now, next;
		bool any_pending;
		do {
			now = std::chrono::steady_clock::now();
			dispatcher->run(now);

			std::tie(any_pending, next) = dispatcher->next_timer();
		} while (any_pending && now >= next);

		// If any pending, now sleep for 'next - now' time interval.
		// Atomically suspend with respect to the wake_up functional
		// assigned to timer dispatcher.
	\endcode

	As shown above, the current time should be rechecked after
	processing the timer queue because processing the timer callbacks
	takes its time as well.

	The returned time value can be used to calculate the time to
	"sleep" until the next timer is due. Since timers can be added
	during the "sleep" period, the dispatcher cooperates with an
	interruptible dispatcher to which it can deliver a "wake up" event,
	such as \ref tscb::ioready_dispatcher "ioready_dispatcher":

	\code
		tscb::ioready_dispatcher* io = ...;
		...
		std::unique_ptr<tscb::timer_dispatcher> =
			std::make_unique<tscb::timer_dispatcher>([io](){io->wake_up;});
	\endcode

	The caller must <I>atomically</I> wait until timeout and the
	condition set by the wake_up functional passed to the timer
	dispatcher. See \ref reactor_descr for more information on this
	mechanism.
*/

namespace tscb {

/**
	\class basic_timer_connection
	\brief Represent timer callback connections
	\headerfile tscb/timer.h <tscb/timer.h>

	Represent and control a timer callback connection. This allows an
	application to \ref set "set" a new due time, \ref suspend
	"suspend" the timer or \ref disconnect "disconnect" it to stop
	it from being called again.


	\fn basic_timer_connection::basic_timer_connection()
	\brief Construct empty (disconnected) connection object.

	Constructs a connection object that is initially in a a
	"disconnected" state. Active connections may be assigned to it
	later.

	\post !this->\ref is_connected "is_connected()"


	\fn basic_timer_connection::basic_timer_connection(detail::intrusive_ptr<link_type> link)
	\brief Construct connection object referencing link.

	Constructs a connection object that references the given link. This
	is usually an internal operation only required when implementing
	new connection types.

	\post !this->\ref is_connected "is_connected()" == link && link->is_connected()


	\fn basic_timer_connection::basic_timer_connection(const basic_timer_connection & other)
	\brief Create shared connection object.
	\param other Other connection to share state with.

	Constructs a connection object sharing state with an existing
	connection object.


	\fn basic_timer_connection::basic_timer_connection(basic_timer_connection && other)
	\brief Transfer connection from another object.
	\param other Other connection object to transfer from.

	Constructs a connection object taking over state with of an existing
	connection object.

	\post !other.\ref is_connected "is_connected()"



	\fn basic_timer_connection & basic_timer_connection::operator=(const basic_timer_connection & other)
	\brief Assign connection making a new reference.

	Make this connection object reference the same connection as the
	other. Note that the old connection represented by this object
	is <I>not</I> \ref disconnect "disconnected" automatically.

	\post this->\ref is_connected "is_connected()" == other.is_connected()


	\fn basic_timer_connection & basic_timer_connection::operator=(basic_timer_connection && other)
	\brief Transfer connection from other connection.

	Make this connection object reference the same connection as the
	other and turn other connection into inactive state. Note that the
	old connection represented by this object is <I>not</I> \ref
	disconnect "disconnected" automatically.

	\post !other.\ref is_connected "is_connected()"


	\fn basic_timer_connection::disconnect
	\brief Disconnect timer callback

	Break the callback link, stop timer callback entirely. This
	operation can safely be called when callback is in progress,
	potentially even from other threads. It is guaranteed that the
	corresponding callback function will not be called in any
	execution that "happens after" the return of this call. This
	means:

	- in the same thread, the callback will not be invoked anymore

	- in other threads it might be still invoked if processing of
	  timers races with this disconnect call

	- in other threads it will not be called "after" this disconnect
	  call returns XXX: define "after"

	The callback function corresponding to the connection will be
	deleted -- if possible, before this call returns (if the signal
	source is not active), but possibly deferred if the signal
	source is presently processing its callback chain.


	\fn basic_timer_connection::set
	\brief Set new due time.
	\param when New due time.

	Set new time when timer is due. If timer was suspended, then
	this call reactivates it. It is safe to call this method when
	timer callback is active, potentially even from other threads.
	It is guaranteed that the correspnoding callback function will
	not be called at the previous due time in any execution that
	"happens after" the return of this call. This means:

	- in the same thread, the callback will be called at the new
	  due time

	- in other threads it might be still invoked if processing of
	  timers races with call to set new due time

	- in other threads it is also guaranteed to not be called
	  anymore if there is any other form of synchronization
	  employed aftre this call returns

	Calling this method from the timer callback itself has well-defined
	behavior in that it rearms the timer to be called at a different
	point in time. If this method is called concurrently from multiple
	threads, then behavior is only defined in that "any" of the times
	set become the new due time - in the absence of further
	synchronization it is however unspecified which. Behavior is
	similar with concurrent calls to \ref
	tscb::basic_timer_connection::suspend
	"basic_timer_connection::suspend".


	\fn basic_timer_connection::suspend
	\brief Suspend timer

	Deactivates the timer: if it was active before with a due time
	then this is cleared. It is safe to call this method when
	timer callback is active, potentially even from other threads.
	It is guaranteed that the correspnoding callback function will
	not be called at the previous due time in any execution that
	"happens after" the return of this call. This means:

	- in the same thread, the callback will be called at the new
	  due time

	- in other threads it might be still invoked if processing of
	  timers races with call to set new due time

	- in other threads it is also guaranteed to not be called
	  anymore if there is any other form of synchronization
	  employed aftre this call returns

	See comments in \ref tscb::basic_timer_connection::set
	"basic_timer_connection::set" on concurrency.


	\fn basic_timer_connection::when
	\brief Get current due time.
	\returns Current due time.

	Returns next time that callback is due. This is not meaningful if
	timer is suspended or if it is disconnected.

	\pre !this->\ref suspended()


	\fn basic_timer_connection::suspended
	\brief Check whether timer is suspended.
	\returns Whether timer is suspended.

	Indicates whether timer is suspended. Callbacks for suspended timers
	are never invoked. This is not meaningful if timer is disconnected.


	\fn basic_timer_connection::operator connection() const noexcept
	\brief Downcast to simple connection.


	\fn basic_timer_connection::is_connected
	\brief Determine whether connection is active.
	\return True if represents an active connection, false otherwise.

	Determines whether connection is active.


	\fn basic_timer_connection::swap
	\brief Swap contents with other connection object.


	\fn basic_timer_connection::link
	\brief Raw access to refcounted link object.


	\fn basic_timer_connection::get
	\brief Raw access (borrowed) to link object.

*/

/**
	\class basic_timer_connection::link_type
	\brief Callback link timers.

	This class represents a link that has been established by
	registering a callback on timer expiry.


	\typedef basic_timer_connection::link_type::pointer
	\brief Reference-counted pointer representation for \ref link_type.

	\fn basic_timer_connection::link_type::set
	\brief Set new due time.
	\param when New due time.

	To be overridden by timer dispatcher implementation to provide
	the behavior required by \ref basic_timer_connection::set.


	\fn basic_timer_connection::link_type::suspend
	\brief Suspend timer.

	To be overridden by timer dispatcher implementation to provide
	the behavior required by \ref basic_timer_connection::suspend.


	\fn basic_timer_connection::link_type::when
	\brief Get current due time.
	\returns Current due time.

	To be overridden by timer dispatcher implementation to provide
	the behavior required by \ref basic_timer_connection::when.


	\fn basic_timer_connection::link_type::suspended
	\brief Check whether timer is suspended.
	\returns Whether timer is suspended.

	To be overridden by timer dispatcher implementation to provide
	the behavior required by \ref basic_timer_connection::suspended.
*/

/**
	\class scoped_basic_timer_connection
	\brief Represent scoped timer callback connections
	\headerfile tscb/timer.h <tscb/timer.h>

	Wrapper control object for a link between an event source and a
	receiver. scoped_basic_timer_connection objects may either refer to
	an active link or be "empty". Applications can \ref disconnect
	"disconnect" an active link through it, and scoped_connection
	automatically disconnects a link when destroyed. See also \ref
	basic_timer_connection.

	XXX: reference caveats for thread-safety


	\fn scoped_basic_timer_connection::scoped_basic_timer_connection()
	\brief Construct empty (disconnected) connection object.

	Constructs a connection object that is initially in a a
	"disconnected" state. Active connections may be assigned to it
	later.

	\post !this->\ref is_connected "is_connected()"


	\fn scoped_basic_timer_connection::scoped_basic_timer_connection(const basic_timer_connection & other)
	\brief Create shared connection object.
	\param other Other connection to share state with.

	Constructs a connection object sharing state with an existing
	connection object. The connection will be disconnected when this
	object goes out of scope.


	\fn scoped_basic_timer_connection::scoped_basic_timer_connection(scoped_basic_timer_connection && other)
	\brief Transfer connection from another object.
	\param other Other connection object to transfer from.

	Constructs a connection object taking over state of an existing
	connection object.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_basic_timer_connection::scoped_basic_timer_connection(basic_timer_connection && other)
	\brief Transfer connection from another object.
	\param other Other connection object to transfer from.

	Constructs a connection object taking over state of an existing
	connection object.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_basic_timer_connection & scoped_basic_timer_connection::operator=(const basic_timer_connection & other)
	\brief Assign connection making a new reference.

	Make this connection object reference the same connection as the
	other. The old connection represented by this object is implicitly
	disconnected first.

	\post this->\ref is_connected "is_connected()" == other.is_connected()


	\fn scoped_basic_timer_connection & scoped_basic_timer_connection::operator=(scoped_basic_timer_connection && other)
	\brief Transfer connection from other connection.

	Transfer connection from other object. Note that the old connection
	represented by this object is implicitly \ref disconnect
	"disconnected"..

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_basic_timer_connection::disconnect
	\brief Disconnect timer callback

	Break the callback link, stop timer callback entirely. This
	operation can safely be called when callback is in progress,
	potentially even from other threads. It is guaranteed that the
	corresponding callback function will not be called in any
	execution that "happens after" the return of this call. This
	means:

	- in the same thread, the callback will not be invoked anymore

	- in other threads it might be still invoked if processing of
	  timers races with this disconnect call

	- in other threads it will not be called "after" this disconnect
	  call returns XXX: define "after"

	The callback function corresponding to the connection will be
	deleted -- if possible, before this call returns (if the signal
	source is not active), but possibly deferred if the signal
	source is presently processing its callback chain.


	\fn scoped_basic_timer_connection::set
	\brief Set new due time.
	\param when New due time.

	Set new time when timer is due. If timer was suspended, then
	this call reactivates it. It is safe to call this method when
	timer callback is active, potentially even from other threads.
	It is guaranteed that the correspnoding callback function will
	not be called at the previous due time in any execution that
	"happens after" the return of this call. This means:

	- in the same thread, the callback will be called at the new
	  due time

	- in other threads it might be still invoked if processing of
	  timers races with call to set new due time

	- in other threads it is also guaranteed to not be called
	  anymore if there is any other form of synchronization
	  employed aftre this call returns

	Calling this method from the timer callback itself has well-defined
	behavior in that it rearms the timer to be called at a different
	point in time. If this method is called concurrently from multiple
	threads, then behavior is only defined in that "any" of the times
	set become the new due time - in the absence of further
	synchronization it is however unspecified which. Behavior is
	similar with concurrent calls to \ref
	tscb::scoped_basic_timer_connection::suspend
	"scoped_basic_timer_connection::suspend".


	\fn scoped_basic_timer_connection::suspend
	\brief Suspend timer

	Deactivates the timer: if it was active before with a due time
	then this is cleared. It is safe to call this method when
	timer callback is active, potentially even from other threads.
	It is guaranteed that the correspnoding callback function will
	not be called at the previous due time in any execution that
	"happens after" the return of this call. This means:

	- in the same thread, the callback will be called at the new
	  due time

	- in other threads it might be still invoked if processing of
	  timers races with call to set new due time

	- in other threads it is also guaranteed to not be called
	  anymore if there is any other form of synchronization
	  employed aftre this call returns

	See comments in \ref tscb::scoped_basic_timer_connection::set
	"scoped_basic_timer_connection::set" on concurrency.


	\fn scoped_basic_timer_connection::when
	\brief Get current due time.
	\returns Current due time.

	Returns next time that callback is due. This is not meaningful if
	timer is suspended or if it is disconnected.

	\pre !this->\ref suspended()


	\fn scoped_basic_timer_connection::suspended
	\brief Check whether timer is suspended.
	\returns Whether timer is suspended.

	Indicates whether timer is suspended. Callbacks for suspended timers
	are never invoked. This is not meaningful if timer is disconnected.


	\fn scoped_basic_timer_connection::is_connected
	\brief Determine whether connection is active.
	\return True if represents an active connection, false otherwise.

	Determines whether connection is active.


	\fn scoped_basic_timer_connection::swap
	\brief Swap contents with other connection object.


	\fn scoped_basic_timer_connection::link
	\brief Raw access to refcounted link object.


	\fn scoped_basic_timer_connection::get
	\brief Raw access (borrowed) to link object.
*/

/**
	\class basic_timer_service
	\brief Registration for timer events.
	\headerfile tscb/timer.h <tscb/timer.h>

	This class provides the registration interface for timer callbacks.
	Receivers can use the \ref timer methods of this class to indicate
	their interest in receiving callbacks at defined points in time.
	See section \ref timer_registration for examples on how to use this
	interface.

	The single template parameter, <TT>TimePoint</TT>, of this class
	determines the data type used to represent points in time. The most
	useful specialization is given by \ref tscb::timer_service
	"timer_service" which uses std::chrono::steady_clock to interface
	with OS.


	\fn basic_timer_service::timer
	\brief Register callback for timer event.

	\param function
		Function to be called at specific point in time
	\param when
		The point in time when the callback should be executed
	\returns
		Connection handle for the timer callback.

	This function requests a callback at a given point in time. The
	point in time is given as an absolute value.

	No hard real-time guarantees are made about when the callback will
	be executed. It will usually be executed a little after the point
	in time indicated by <CODE>when</CODE>, dependent on the resolution
	of the clock and load of the system.

	The function is called with the current time (as known to the
	caller), so the callback can determine if it is delayed and adapt
	accordingly.

	May throw std::bad_alloc if timer structure could not be allocated.


	\fn basic_timer_service::one_shot_timer
	\brief Register callback for one-shot timer event.

	\param function
		Function to be called at specific point in time
	\param when
		The point in time when the callback should be executed
	\returns
		Connection handle for the timer callback.

	This function requests a callback at a given point in time. The
	point in time is given as an absolute value, so if you want a
	relative point in time, you need to compute absolute point in time
	based on current time first.

	No hard real-time guarantees are made about when the callback will
	be executed. It will usually be executed a little after the point
	in time indicated by <CODE>expires</CODE>, dependent on the
	resolution of the clock and load of the system.

	The function is called with the current time (as known to the
	caller), so the callback can determine if it is delayed and adapt
	accordingly.

	The timer is one-shot and implicitly cancelled when the callback
	is called. It cannot be re-armed.

	May throw std::bad_alloc if timer structure could not be allocated.


	\fn basic_timer_service::suspended_timer
	\brief Register callback for suspended timer.

	\param function
		Function to be called at specific point in time
	\returns
		Connection handle for the timer callback.

	This function installs a timer callback in suspended state. The
	timer structure is allocated and prepared, but no due time is set.
	It can later be activated by calling \ref
	basic_timer_connection::set.

	May throw std::bad_alloc if timer structure could not be allocated.


	\fn basic_timer_service::one_shot_suspended_timer
	\brief Register callback for one shot suspended timer.

	\param function
		Function to be called at specific point in time
	\returns
		Connection handle for the timer callback.

	This function installs a timer callback in suspended state. The
	timer structure is allocated and prepared, but no due time is set.
	It can later be activated by calling \ref
	basic_timer_connection::set.

	The timer is one-shot and implicitly cancelled when the callback
	is called. It cannot be re-armed.

	May throw std::bad_alloc if timer structure could not be allocated.
*/

/**
	\class basic_timer_dispatcher
	\brief Dispatcher for timer events.
	\headerfile tscb/timer.h <tscb/timer.h>

	This implementation of the \ref tscb::basic_timer_service
	"basic_timer_service" manages a queue of timers and takes care of
	dispatching all callbacks that are due.

	See section \ref basic_timer_dispatcher_descr for usage examples.


	\fn basic_timer_dispatcher::basic_timer_dispatcher(std::function<void()> timer_added)
	\brief Create timer dispatcher.
	\param timer_added Event to be triggered when earlier timer is added.

	Creates a new timer dispatcher. The \p timer_added function will be
	called whenever a new timer is added such that the result of an
	earlier call to \ref next_timer is invalidated.


	\fn basic_timer_dispatcher::next_timer
	\brief Determine when next timer is due (if any)
	\returns
		(false, anything) if no timer is pending or (true, earliest) to
		indicate earliest timer due

	Returns whether any timer is pending at all and the time point the
	earliest one is due. Note that this check may race with
	registration of new timers from other threads; to avoid missing
	timers the caller should therefore

	1. clear the wake up condition that the timer dispatcher is
	associated with

	2. check if timers are pending

	3. atomically wait for timeout and the wake up condition

	If a timer is inserted between 2 and 3, the dispatcher
	thread will not wait in step 3, and a new iteration of the
	dispatching loop must be started.


	\fn basic_timer_dispatcher::run
	\brief Run all due timers.
	\param now
		Current point in time -- all timers expiring at or before this
		point in time will be executed.
	\param limit
		Upper bound for number timers executed.
	\returns
		Number of timer functions executed.

	Runs all timers which are due at the given point in time. Also see
	\ref run_single below.


	\fn basic_timer_dispatcher::run_single
	\brief Run single callback if any is due.
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

template class basic_timer_connection<std::chrono::steady_clock::time_point>;
template class scoped_basic_timer_connection<std::chrono::steady_clock::time_point>;
template class basic_timer_service<std::chrono::steady_clock::time_point>;
template class basic_timer_dispatcher<std::chrono::steady_clock::time_point>;

#ifdef DOXYGEN_GENERATE

/* This code is not real, it just spells out the template
 * specializations done above to "trick" doxygen into producing explicit
 * documentation for the specializations. */

class timer_connection {
private:
	class link_type;

public:
	timer_connection() noexcept;

	inline explicit
	timer_connection(detail::intrusive_ptr<link_type> link) noexcept;

	inline
	timer_connection(const timer_connection & other) noexcept;

	inline
	timer_connection(timer_connection && other) noexcept;

	inline timer_connection &
	operator=(const timer_connection & other) noexcept;

	inline timer_connection &
	operator=(timer_connection && other) noexcept;

	inline void
	swap(timer_connection & other) noexcept;

	inline void
	disconnect() noexcept;

	inline bool
	is_connected() const noexcept;

	inline void
	set(std::chrono::steady_clock::time_point when) noexcept;

	inline void
	suspend() noexcept;

	inline const typename link_type::pointer &
	link() const noexcept;

	inline link_type *
	get() const noexcept;

	inline bool
	suspended() const noexcept;

	inline std::chrono::steady_clock::time_point
	when() const noexcept;

	inline operator connection() const noexcept;

private:
	typename link_type::pointer link_;
};

/**
	\class timer_connection
	\brief Represent timer callback connections
	\headerfile tscb/timer.h <tscb/timer.h>

	Represent and control a timer callback connection. This allows an
	application to \ref set "set" a new due time, \ref suspend
	"suspend" the timer or \ref disconnect "disconnect" it to stop
	it from being called again.


	\fn timer_connection::timer_connection()
	\brief Construct empty (disconnected) connection object.

	Constructs a connection object that is initially in a a
	"disconnected" state. Active connections may be assigned to it
	later.

	\post !this->\ref is_connected "is_connected()"


	\fn timer_connection::timer_connection(detail::intrusive_ptr<link_type> link)
	\brief Construct connection object referencing link.

	Constructs a connection object that references the given link. This
	is usually an internal operation only required when implementing
	new connection types.

	\post !this->\ref is_connected "is_connected()" == link && link->is_connected()


	\fn timer_connection::timer_connection(const timer_connection & other)
	\brief Create shared connection object.
	\param other Other connection to share state with.

	Constructs a connection object sharing state with an existing
	connection object.


	\fn timer_connection::timer_connection(timer_connection && other)
	\brief Transfer connection from another object.
	\param other Other connection object to transfer from.

	Constructs a connection object taking over state with of an existing
	connection object.

	\post !other.\ref is_connected "is_connected()"



	\fn timer_connection & timer_connection::operator=(const timer_connection & other)
	\brief Assign connection making a new reference.

	Make this connection object reference the same connection as the
	other. Note that the old connection represented by this object
	is <I>not</I> \ref disconnect "disconnected" automatically.

	\post this->\ref is_connected "is_connected()" == other.is_connected()


	\fn timer_connection & timer_connection::operator=(timer_connection && other)
	\brief Transfer connection from other connection.

	Make this connection object reference the same connection as the
	other and turn other connection into inactive state. Note that the
	old connection represented by this object is <I>not</I> \ref
	disconnect "disconnected" automatically.

	\post !other.\ref is_connected "is_connected()"




	\fn timer_connection::disconnect
	\brief Disconnect timer callback

	Break the callback link, stop timer callback entirely. This
	operation can safely be called when callback is in progress,
	potentially even from other threads. It is guaranteed that the
	corresponding callback function will not be called in any
	execution that "happens after" the return of this call. This
	means:

	- in the same thread, the callback will not be invoked anymore

	- in other threads it might be still invoked if processing of
	  timers races with this disconnect call

	- in other threads it will not be called "after" this disconnect
	  call returns XXX: define "after"

	The callback function corresponding to the connection will be
	deleted -- if possible, before this call returns (if the signal
	source is not active), but possibly deferred if the signal
	source is presently processing its callback chain.


	\fn timer_connection::set
	\brief Set new due time.
	\param when New due time.

	Set new time when timer is due. If timer was suspended, then
	this call reactivates it. It is safe to call this method when
	timer callback is active, potentially even from other threads.
	It is guaranteed that the correspnoding callback function will
	not be called at the previous due time in any execution that
	"happens after" the return of this call. This means:

	- in the same thread, the callback will be called at the new
	  due time

	- in other threads it might be still invoked if processing of
	  timers races with call to set new due time

	- in other threads it is also guaranteed to not be called
	  anymore if there is any other form of synchronization
	  employed aftre this call returns

	Calling this method from the timer callback itself has well-defined
	behavior in that it rearms the timer to be called at a different
	point in time. If this method is called concurrently from multiple
	threads, then behavior is only defined in that "any" of the times
	set become the new due time - in the absence of further
	synchronization it is however unspecified which. Behavior is
	similar with concurrent calls to \ref
	tscb::timer_connection::suspend "timer_connection::suspend".


	\fn timer_connection::suspend
	\brief Suspend timer

	Deactivates the timer: if it was active before with a due time
	then this is cleared. It is safe to call this method when
	timer callback is active, potentially even from other threads.
	It is guaranteed that the correspnoding callback function will
	not be called at the previous due time in any execution that
	"happens after" the return of this call. This means:

	- in the same thread, the callback will be called at the new
	  due time

	- in other threads it might be still invoked if processing of
	  timers races with call to set new due time

	- in other threads it is also guaranteed to not be called
	  anymore if there is any other form of synchronization
	  employed aftre this call returns

	See comments in \ref tscb::timer_connection::set
	"timer_connection::set" on concurrency.


	\fn timer_connection::when
	\brief Get current due time.
	\returns Current due time.

	Returns next time that callback is due. This is not meaningful if
	timer is suspended or if it is disconnected.

	\pre !this->\ref suspended()


	\fn timer_connection::suspended
	\brief Check whether timer is suspended.
	\returns Whether timer is suspended.

	Indicates whether timer is suspended. Callbacks for suspended timers
	are never invoked. This is not meaningful if timer is disconnected.


	\fn timer_connection::operator connection() const noexcept
	\brief Downcast to simple connection.


	\fn timer_connection::is_connected
	\brief Determine whether connection is active.
	\return True if represents an active connection, false otherwise.

	Determines whether connection is active.


	\fn timer_connection::swap
	\brief Swap contents with other connection object.


	\fn timer_connection::link
	\brief Raw access to refcounted link object.


	\fn timer_connection::get
	\brief Raw access (borrowed) to link object.

*/

class scoped_timer_connection {
public:
	inline ~scoped_timer_connection() noexcept;

	inline scoped_timer_connection() noexcept {}
	inline scoped_timer_connection(const timer_connection& c) noexcept : connection_(c) {}
	inline scoped_timer_connection(timer_connection&& c) noexcept : connection_(std::move(c)) {}

	scoped_timer_connection(const scoped_timer_connection & other) = delete;
	scoped_timer_connection & operator=(const scoped_timer_connection & other) = delete;

	scoped_timer_connection(scoped_timer_connection && other) noexcept;
	scoped_timer_connection &
	operator=(scoped_timer_connection && other) noexcept;

	inline void
	swap(scoped_timer_connection & other) noexcept;

	inline bool
	is_connected() const noexcept;

	inline void
	set(std::chrono::steady_clock::time_point when) noexcept;

	inline std::chrono::steady_clock::time_point
	when() const noexcept;

	inline void
	suspend() noexcept;

	inline bool
	suspended() const noexcept;

	inline void disconnect() noexcept;

	inline scoped_timer_connection &
	operator=(const timer_connection& c) noexcept;

	inline const typename link_type::pointer &
	link() const noexcept;

	inline link_type *
	get() const noexcept;
};

/**
	\class scoped_timer_connection
	\brief Represent scoped timer callback connections
	\headerfile tscb/timer.h <tscb/timer.h>

	Wrapper control object for a link between an event source and a
	receiver. scoped_timer_connection objects may either refer to
	an active link or be "empty". Applications can \ref disconnect
	"disconnect" an active link through it, and scoped_connection
	automatically disconnects a link when destroyed. See also \ref
	timer_connection.

	XXX: reference caveats for thread-safety


	\fn scoped_timer_connection::scoped_timer_connection()
	\brief Construct empty (disconnected) connection object.

	Constructs a connection object that is initially in a a
	"disconnected" state. Active connections may be assigned to it
	later.

	\post !this->\ref is_connected "is_connected()"


	\fn scoped_timer_connection::scoped_timer_connection(const timer_connection & other)
	\brief Create shared connection object.
	\param other Other connection to share state with.

	Constructs a connection object sharing state with an existing
	connection object. The connection will be disconnected when this
	object goes out of scope.


	\fn scoped_timer_connection::scoped_timer_connection(scoped_timer_connection && other)
	\brief Transfer connection from another object.
	\param other Other connection object to transfer from.

	Constructs a connection object taking over state of an existing
	connection object.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_timer_connection::scoped_timer_connection(timer_connection && other)
	\brief Transfer connection from another object.
	\param other Other connection object to transfer from.

	Constructs a connection object taking over state of an existing
	connection object.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_timer_connection & scoped_timer_connection::operator=(const timer_connection & other)
	\brief Assign connection making a new reference.

	Make this connection object reference the same connection as the
	other. The old connection represented by this object is implicitly
	disconnected first.

	\post this->\ref is_connected "is_connected()" == other.is_connected()


	\fn scoped_timer_connection & scoped_timer_connection::operator=(scoped_timer_connection && other)
	\brief Transfer connection from other connection.

	Transfer connection from other object. Note that the old connection
	represented by this object is implicitly \ref disconnect
	"disconnected"..

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_timer_connection::disconnect
	\brief Disconnect timer callback

	Break the callback link, stop timer callback entirely. This
	operation can safely be called when callback is in progress,
	potentially even from other threads. It is guaranteed that the
	corresponding callback function will not be called in any
	execution that "happens after" the return of this call. This
	means:

	- in the same thread, the callback will not be invoked anymore

	- in other threads it might be still invoked if processing of
	  timers races with this disconnect call

	- in other threads it will not be called "after" this disconnect
	  call returns XXX: define "after"

	The callback function corresponding to the connection will be
	deleted -- if possible, before this call returns (if the signal
	source is not active), but possibly deferred if the signal
	source is presently processing its callback chain.


	\fn scoped_timer_connection::set
	\brief Set new due time.
	\param when New due time.

	Set new time when timer is due. If timer was suspended, then
	this call reactivates it. It is safe to call this method when
	timer callback is active, potentially even from other threads.
	It is guaranteed that the correspnoding callback function will
	not be called at the previous due time in any execution that
	"happens after" the return of this call. This means:

	- in the same thread, the callback will be called at the new
	  due time

	- in other threads it might be still invoked if processing of
	  timers races with call to set new due time

	- in other threads it is also guaranteed to not be called
	  anymore if there is any other form of synchronization
	  employed aftre this call returns

	Calling this method from the timer callback itself has well-defined
	behavior in that it rearms the timer to be called at a different
	point in time. If this method is called concurrently from multiple
	threads, then behavior is only defined in that "any" of the times
	set become the new due time - in the absence of further
	synchronization it is however unspecified which. Behavior is
	similar with concurrent calls to \ref
	tscb::scoped_timer_connection::suspend
	"scoped_timer_connection::suspend".


	\fn scoped_timer_connection::suspend
	\brief Suspend timer

	Deactivates the timer: if it was active before with a due time
	then this is cleared. It is safe to call this method when
	timer callback is active, potentially even from other threads.
	It is guaranteed that the correspnoding callback function will
	not be called at the previous due time in any execution that
	"happens after" the return of this call. This means:

	- in the same thread, the callback will be called at the new
	  due time

	- in other threads it might be still invoked if processing of
	  timers races with call to set new due time

	- in other threads it is also guaranteed to not be called
	  anymore if there is any other form of synchronization
	  employed aftre this call returns

	See comments in \ref tscb::scoped_timer_connection::set
	"scoped_timer_connection::set" on concurrency.


	\fn scoped_timer_connection::when
	\brief Get current due time.
	\returns Current due time.

	Returns next time that callback is due. This is not meaningful if
	timer is suspended or if it is disconnected.

	\pre !this->\ref suspended()


	\fn scoped_timer_connection::suspended
	\brief Check whether timer is suspended.
	\returns Whether timer is suspended.

	Indicates whether timer is suspended. Callbacks for suspended timers
	are never invoked. This is not meaningful if timer is disconnected.


	\fn scoped_timer_connection::is_connected
	\brief Determine whether connection is active.
	\return True if represents an active connection, false otherwise.

	Determines whether connection is active.


	\fn scoped_timer_connection::swap
	\brief Swap contents with other connection object.


	\fn scoped_timer_connection::link
	\brief Raw access to refcounted link object.


	\fn scoped_timer_connection::get
	\brief Raw access (borrowed) to link object.
*/

class timer_service {
public:
	virtual ~timer_service();

	virtual timer_connection
	timer(
		std::function<void(std::chrono::steady_clock::time_point)> function, std::chrono::steady_clock::time_point when) = 0;

	virtual timer_connection
	one_shot_timer(
		std::function<void(std::chrono::steady_clock::time_point)> function, std::chrono::steady_clock::time_point when) = 0;

	virtual timer_connection
	suspended_timer(
		std::function<void(std::chrono::steady_clock::time_point)> function) = 0;

	virtual timer_connection
	one_shot_suspended_timer(
		std::function<void(std::chrono::steady_clock::time_point)> function) = 0;
};

/**
	\class timer_service
	\brief Registration for timer events.
	\headerfile tscb/timer.h <tscb/timer.h>

	This class provides the registration interface for timer callbacks.
	Receivers can use the \ref timer methods of this class to indicate
	their interest in receiving callbacks at defined points in time.
	See section \ref timer_registration for examples on how to use this
	interface.


	\fn timer_service::timer
	\brief Register callback for timer event.

	\param function
		Function to be called at specific point in time
	\param when
		The point in time when the callback should be executed
	\returns
		Connection handle for the timer callback.

	This function requests a callback at a given point in time. The
	point in time is given as an absolute value.

	No hard real-time guarantees are made about when the callback will
	be executed. It will usually be executed a little after the point
	in time indicated by <CODE>when</CODE>, dependent on the resolution
	of the clock and load of the system.

	The function is called with the current time (as known to the
	caller), so the callback can determine if it is delayed and adapt
	accordingly.

	May throw std::bad_alloc if timer structure could not be allocated.


	\fn timer_service::one_shot_timer
	\brief Register callback for one-shot timer event.

	\param function
		Function to be called at specific point in time
	\param when
		The point in time when the callback should be executed
	\returns
		Connection handle for the timer callback.

	This function requests a callback at a given point in time. The
	point in time is given as an absolute value, so if you want a
	relative point in time, use std::chrono::steady_clock::now() to
	compute appropriate absolute point in time.

	No hard real-time guarantees are made about when the callback will
	be executed. It will usually be executed a little after the point
	in time indicated by <CODE>expires</CODE>, dependent on the
	resolution of the clock and load of the system.

	The function is called with the current time (as known to the
	caller), so the callback can determine if it is delayed and adapt
	accordingly.

	The timer is one-shot and implicitly cancelled when the callback
	is called. It cannot be re-armed.

	May throw std::bad_alloc if timer structure could not be allocated.


	\fn timer_service::suspended_timer
	\brief Register callback for suspended timer.

	\param function
		Function to be called at specific point in time
	\returns
		Connection handle for the timer callback.

	This function installs a timer callback in suspended state. The
	timer structure is allocated and prepared, but no due time is set.
	It can later be activated by calling \ref
	timer_connection::set.

	May throw std::bad_alloc if timer structure could not be allocated.


	\fn timer_service::one_shot_suspended_timer
	\brief Register callback for one shot suspended timer.

	\param function
		Function to be called at specific point in time
	\returns
		Connection handle for the timer callback.

	This function installs a timer callback in suspended state. The
	timer structure is allocated and prepared, but no due time is set.
	It can later be activated by calling \ref
	timer_connection::set.

	The timer is one-shot and implicitly cancelled when the callback
	is called. It cannot be re-armed.

	May throw std::bad_alloc if timer structure could not be allocated.
*/

class timer_dispatcher final : public timer_service {
public:
	~timer_dispatcher() noexcept override;

	inline explicit
	timer_dispatcher(std::function<void()> timer_added);

	std::pair<bool, std::chrono::steady_clock::time_point>
	next_timer() const noexcept;

	std::size_t
	run(std::chrono::steady_clock::time_point now, std::size_t limit = std::numeric_limits<std::size_t>::max());

	bool
	run_single(TimePoint now);

	timer_connection
	timer(
		std::function<void (std::chrono::steady_clock::time_point)> function, std::chrono::steady_clock::time_point when)
		override;

	timer_connection
	one_shot_timer(
		std::function<void(std::chrono::steady_clock::time_point)> function, std::chrono::steady_clock::time_point when)
		override;

	timer_connection
	suspended_timer(
		std::function<void(std::chrono::steady_clock::time_point)> function)
		override;

	timer_connection
	one_shot_suspended_timer(
		std::function<void(std::chrono::steady_clock::time_point)> function)
		override;
};

/**
	\class timer_dispatcher
	\brief Dispatcher for timer events.
	\headerfile tscb/timer.h <tscb/timer.h>

	This implementation of the \ref tscb::timer_service
	"timer_service" manages a queue of timers and takes care of
	dispatching all callbacks that are due.

	See section \ref basic_timer_dispatcher_descr for usage examples.


	\fn timer_dispatcher::timer_dispatcher(std::function<void()> timer_added)
	\brief Create timer dispatcher.
	\param timer_added Event to be triggered when earlier timer is added.

	Creates a new timer dispatcher. The \p timer_added function will be
	called whenever a new timer is added such that the result of an
	earlier call to \ref next_timer is invalidated.


	\fn timer_dispatcher::next_timer
	\brief Determine when next timer is due (if any)
	\returns
		(false, anything) if no timer is pending or (true, earliest) to
		indicate earliest timer due

	Returns whether any timer is pending at all and the time point the
	earliest one is due.
	Note that this check may race with registration of
	new timers from other threads; to avoid missing timers
	the caller should therefore

	1. clear the wake up condition that the timer dispatcher is
	associated with

	2. check if timers are pending

	3. atomically wait for timeout and the wake up condition

	If a timer is inserted between 2 and 3, the dispatcher
	thread will not wait in step 3, and a new iteration of the
	dispatching loop must be started.


	\fn timer_dispatcher::run
	\brief Run all due timers.
	\param now
		Current point in time -- all timers expiring at or before this
		point in time will be executed.
	\param limit
		Upper bound for number timers executed.
	\returns
		Number of timer functions executed.

	Runs all timers which are due at the given point in time. Also see
	\ref run_single below.


	\fn timer_dispatcher::run_single
	\brief Run single callback if any is due.
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


#endif

}
