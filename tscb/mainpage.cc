/* -*- C++ -*-
 * (c) 2004 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

/* okay this is not _really_ a source file, but a placeholder for the main
documentation page; I don't know where else to put it */

/**
\mainpage libtscb - Thread-safe callback services

This library provides primitives for notifications via callbacks,
particular in multi-threaded programs. This includes user-defined
signal sources, timers, file descriptor read/write readiness, OS
signals and child process state. It features a highly convenient API,
minimal overhead, strong concurrency guarantees, "no memory allocation"
behavior in all fast paths. The library is exception-tolerant, i.e. it
is fully compatible with code using exceptions, but does not require
other code to do so unless programmer wishes to gracefully handle fatal
"resource exhaustion" conditions (memory and file descriptors; these
are the only cases where \p libtscb throws).

\section sec_functionality_overview Functionality overview

The library supports the following kinds of events and
means to dispatch pending notifications:

- \ref signal_descr "User-defined signals and slots": Chains of
  callback functions with a user-defined function signature. See
  also \ref tscb::signal "signal" class.

- \ref ioready_descr "I/O readiness": Chains of callbacks
  registered to watch file descriptors for read/write readiness events
  (using \p epoll, \p kqueue, \p poll or \p select). See \ref
  tscb::ioready_service "ioready_service" and \ref
  tscb::ioready_dispatcher "ioready_dispatcher" interfaces.

- \ref timer_descr "Timers": Callbacks registered to be invoked
  at specified points in time. See \ref tscb::timer_service
  "timer_service" interface and \ref tscb::timer_dispatcher
  "timer_dispatcher" class.

- \ref workqueue_descr "Workqueue": Callbacks representing (repeated
  or one shot) "delayed work" that is supposed to be performed at a
  later point in time. See \ref tscb::workqueue_service
  "workqueue_service" interface and \ref tscb::workqueue "workqueue"
  class.

- \ref childproc_monitor_descr "Child process monitoring": Callbacks
  invoked on a child process terminating. See \ref
  tscb::childproc_monitor_service "childproc_monitor_service" interface
  and \ref tscb::childproc_monitor "childproc_monitor".

- \ref inotify_descr "Inotify": Notify about inode (filesystem) state
  changes.

- \ref reactor_descr "Reactor" (aka "event driven mainloop"):
  Combines I/O, timers, workqueue (and optionally integrates with child
  process monitoring) to provide the complete interface required for
  event-driven programming. See \ref tscb::reactor_service
  "reactor_service" interface and \ref tscb::reactor
  "reactor" handler class.

See \ref reactor_usage_example for a compilable example showing how \p tscb
operates.

\section sec_design_goals Design goals and principles

- API: Straight API, 'raw' OS interface integration, expose details for
  user control. See \ref page_api.

- Performance: Near-optimal performance for all primitives, see
  \ref page_performance "Performance"

- Concurrency: Thread safety throughout, efficient synchronization,
  but no prescribed app concurrency architecture; see
  \ref page_concurrency

- Correctness & testability: Unit test, formal validation of
  synchronization mechanisms, testability for code using this library.

\section sec_implementation notes Implementation notes

Notes about implementation internals are provided TODO here. This covers
aspects not considered part of the public API such as the the
synchronization mechanisms used. See \ref page_implementation_notes.

*/

/**
\page page_api API description

The API of the library is designed to interface directly with OS-level
constructs (file descriptors, process ids, ...) and base language
constructs (\p std::chrono, \p std::function). No abstractions are
provided for these based on the philosophy that this makes integration
as easy as possible.

\section Summary

The library provides the following services -- generally providing
an interface/implementation separation for unit testing purposes:

<table>
 <tr>
  <td></td>
  <td>\ref tscb::signal<td>
  <td>User-defined signals. (See notes on implementation/interface
  separation below).</td>
 </tr>
 <tr>
  <td>\ref tscb::ioready_service</td>
  <td>
   \ref tscb::ioready_dispatcher (abstract) <br>
   \ref tscb::ioready_dispatcher_epoll <br>
   \ref tscb::ioready_dispatcher_kqueue <br>
   \ref tscb::ioready_dispatcher_poll <br>
   \ref tscb::ioready_dispatcher_select
  </td>
  <td>
   Notification for events on file descriptors. Dispatching is expected
   to be performed by implementations through \ref
   tscb::ioready_dispatcher interface to abstract OS-specific
   mechanisms.
  </td>
 </tr>
 <tr>
  <td>\ref tscb::timer_service</td>
  <td>
   \ref tscb::timer_dispatcher
  </td>
  <td>
   Timer events against steady clock (monotonic clock).
  </td>
 </tr>
 <tr>
  <td>\ref tscb::basic_timer_service</td>
  <td>
   \ref tscb::basic_timer_dispatcher
  </td>
  <td>
   Timer events against user-defined time and clock (template classes).
  </td>
 </tr>
 <tr>
  <td>\ref tscb::workqueue_service</td>
  <td>
   \ref tscb::workqueue
  </td>
  <td>
   Deferred procedures (delayed work or async-safe triggered work).
  </td>
 </tr>
 <tr>
  <td>\ref tscb::childproc_monitor_service</td>
  <td>
   \ref tscb::childproc_monitor
  </td>
  <td>
   Watch child process exit.
  </td>
 </tr>
 <tr>
  <td>\ref tscb::reactor_service</td>
  <td>
   \ref tscb::reactor
  </td>
  <td>
   Watch file descriptors, steady clock timers and delayed work (with
   tie-ins for child process monitoring).
  </td>
 </tr>
</table>

In the above table, the \p *_service classes provide the base interface
to be used by clients wishing to register callbacks for notification.
The interface is mockable in order to allow unit testing of clients
using event injection (or generally injecting behaviors through mocks).
The only exception is \ref tscb::signal where this distinction is not
useful as any abstraction is better handled in use code:

\code
  class thermometer_service {
  public:
    tscb::connection watch_temperature(std::function<void(double)> fn) = 0;
  };

  class thermometer : public thermometer_service {
  public:
    tscb::connection watch_temperature(std::function<void(double)> fn) override
    {
      // maybe: schedule one notification about present temperature
      // immediately?
      return temp_change_.connect(std::move(fn));
    }
  private:
    tscb::signal<void(double)> temp_change_;
  };
\endcode

\section sec_quickref_api API quick reference

\subsection sec_quickref_api_user User-defined signals

Definition of service provider.
\code
	tscb::signal<void(Type1, Type2, ...)> notifier;
\endcode

Provider use (notify all registered callbacks.
\code
	notifier(arg1, arg2, ...);
\endcode

Client use (connect and disconnect).
\code
	tscb::connection c = notifier.connect([](Type1, Type2, ...) {...};);
	c.disconnect();
\endcode

\subsection sec_quickref_api_reactor Reactor

Definition of service provider.
\code
	tscb::reactor reactor;
\endcode

Provider use: check for events, invoke associated callbacks.
\code
	for (;;) {
		reactor.dispatch();
	}
\endcode

Registration-only interface for client use.
\code
	tscb::reactor_service& svc = reactor;
\endcode

Client use: Watch file descriptors.
\code
	int fd;  // Assume some file descriptor.
	tscb::ioready_connection cio = svc.watch(
		[fd](tscb::ioready_events ev) {
			// check ev, handle fd
		}, fd, tscb::ioready_read | tscb::ioready_write);
	cio.modify(tscb::ioready_read);  // Change event. Also cf. tscb::ioready_none.
	cio.disconnect();
\endcode

Client use: Timers.
\code
	using time_point = std::chrono::steady_clock::time_point;
	using now = std::chrono::steady_clock::now;
	tscb::timer_connection ctimer = svc.timer(
		[](time_point now) {
			// Perform action.
			// NB: timer suspended when called, see below to reactivate.
		},
		now() + std::chrono::seconds(1));
	// Timer suspended after called.
	ctimer.suspend();  // Temporarily suspend timer.
	ctimer.set(now + std::chrono::seconds(2));  // Set new timeout + reactivate.
	ctimer.disconnect();

	// Variations of timer registration.
	ctimer = svc.suspended_timer([](time_point now){...});  // Init in suspended state.
	ctimer = svc.one_shot_timer([](time_point now){...}, when);  // Auto-disconnect after call, cannot be reactivated.
	ctimer = svc.one_shot_suspended_timer([](time_point now){...});  // Combine above.
\endcode

Client use: workqueues (deferred procedures).
\code
	tscb::connection cwq;
	std::function<void()> trigger;

	// Register procedure.
	std::tie(cwq, trigger) = svc.register_deferred_procedure([](){...});

	// Trigger previously registered procedure (will be executed later).
	trigger();

	// Unregister.
	cwq.disconnect();

	// Variant: the returned trigger function is async-safe.
	std::tie(cwq, trigger) = svc.register_async_deferred_procedure([](){...});

	// Register procedure for single execution (equivalent to
	// registering, triggering, and immediately unregistering after
	// first execution).
	svc.queue_procedure([](){...});
\endcode

\subsection sec_quickref_api_childproc_monitor Child process monitoring

Definition of service provider.

\code
	childproc_monitor processes(false);
\endcode

Provider use: see \ref childproc_monitor_descr how to integrate
with \ref tscb::reactor reactor or \ref tscb::workqueue workqueue.

Registration-only interface for client use.
\code
	tscb::childproc_monitor_service& svc = process_svc;
\endcode

Client use: Monitor child process exit.

\code
	pid_t pid;  // Child process id, e.g. from fork().
	tscb::connection c = process_svc.watch_childproc(
		[](int retcode, const rusage&)
		{
			...
		});

	// Disconnect when losing interest in async process notification
	// (caution: without further ado, the child process will not be
	// "reaped", user code must take care of this in other ways).
	c.disconnect();
\endcode

\subsection sec_quickref_api_ioready IO notification

Note: this is exposed for very specific application scenarios, in
general prefer \ref sec_quickref_api_reactor.

Definition of service provider.
\code
	std::unique_ptr<tscb::ioready_dispatcher> io(tscb::ioready_dispatcher::create());
\endcode

Provider use: check for events, invoke associated callbacks.
\code
	for (;;) {
		io.dispatch();
	}
\endcode

Provider use: dispatch presently pending (does not block).
\code
	io.dispatch_pending();
\end

Registration-only interface for client use.
\code
	tscb::ioready_service& svc = io;
\endcode

Client use: Watch file descriptors.
\code
	int fd;  // Assume some file descriptor.
	tscb::ioready_connection cio = svc.watch(
		[fd](tscb::ioready_events ev) {
			// check ev, handle fd
		}, fd, tscb::ioready_read | tscb::ioready_write);
	cio.modify(tscb::ioready_read);  // Change event. Also cf. tscb::ioready_none.
	cio.disconnect();
\endcode

\subsection sec_quickref_api_timer Timer notification

Note: this is exposed for very specific application scenarios, in
general prefer \ref sec_quickref_api_reactor. Also note that
\ref tscb::timer_dispatcher is the same as \ref tscb::basic_timer_dispatcher
specialized using \p std::chrono::steady_clock::time_point.

Definition of service provider.
\code
	std::function<void()> trigger_fn;  // See ioready_dispatcher::wake_up
	tscb::basic_timer_dispatcher<TimePoint> timers(trigger_fn);
\endcode

Provider use: check for events, invoke associated callbacks.
\code
	TimePoint now;
	timers.run(now);
\endcode

Registration-only interface for client use.
\code
	tscb::basic_timer_service<TimePoint>& svc = timers;
\endcode

Client use: Timers.
\code
	tscb::basic_timer_connection<TimePoint> ctimer = svc.timer(
		[](TimePoint now) {
			// Perform action.
			// NB: timer suspended when called, see below to reactivate.
		},
		some_time_point_in_future());
	// Timer suspended after called.
	ctimer.suspend();  // Temporarily suspend timer.
	ctimer.set(now + std::chrono::seconds(2));  // Set new timeout + reactivate.
	ctimer.disconnect();

	// Variations of timer registration.
	ctimer = svc.suspended_timer([](TimePoint now){...});  // Init in suspended state.
	ctimer = svc.one_shot_timer([](TimePoint now){...}, when);  // Auto-disconnect after call, cannot be reactivated.
	ctimer = svc.one_shot_suspended_timer([](TimePoint now){...});  // Combine above.
\endcode

\subsection sec_quickref_api_workqueue Workqueues (deferred procedures)

Note: this is exposed for very specific application scenarios, in
general prefer \ref sec_quickref_api_reactor.

Definition of service provider.
\code
	std::function<void()> trigger_fn;  // See ioready_dispatcher::wake_up
	tscb::workqueue wq(trigger_fn);
\endcode

Provider use: check for events, invoke associated callbacks.
\code
	wq.dispatch();
\endcode

Registration-only interface for client use.
\code
	tscb::workqueue_service& svc = wq;
\endcode


Client use: workqueues (deferred procedures).
\code
	tscb::connection cwq;
	std::function<void()> trigger;

	// Register procedure.
	std::tie(cwq, trigger) = svc.register_deferred_procedure([](){...});

	// Trigger previously registered procedure (will be executed later).
	trigger();

	// Unregister.
	cwq.disconnect();

	// Variant: the returned trigger function is async-safe.
	std::tie(cwq, trigger) = svc.register_async_deferred_procedure([](){...});

	// Register procedure for single execution (equivalent to
	// registering, triggering, and immediately unregistering after
	// first execution).
	svc.queue_procedure([](){...});
\endcode

*/


/**
\page page_performance Performance

\section sec_performance_comparison Performance comparison (measurements)

Comparison with other libraries; given values are normalized to
number of CPU clock cycles per operation (smaller is better)

\subsection performance_signal Signal/slot mechanism

Debian Linux 5.0, gcc-4.3.2, Intel Celeron \@2GHz
<TABLE>
	<TR>
		<TH>Implementation</TH>
		<TH>call<BR/>single callback</TH>
		<TH>call<BR/>10 callbacks</TH>
		<TH>connect+disconnect</TH>
		<TH>comments</TH>
	</TR>

	<TR>
		<TD>open-coded<BR> (<TT>std::list</TT> of function pointers)</TD>
		<TD>16</TD>
		<TD>150</TD>
		<TD>92</TD>
		<TD>not thread-safe</TD>
	</TR>
	<TR>
		<TD>open-coded<BR> (<TT>std::list</TT> of <TT>std::function</TT> objects)</TD>
		<TD>33</TD>
		<TD>320</TD>
		<TD>254</TD>
		<TD>not thread-safe</TD>
	</TR>
	<TR>
		<TD><TT>tscb::signal</TT></TD>
		<TD>120</TD>
		<TD>436</TD>
		<TD>1286</TD>
		<TD>thread-safe</TD>
	</TR>
	<TR>
		<TD><TT>sigc::signal</TT></TD>
		<TD>280</TD>
		<TD>400</TD>
		<TD>1216</TD>
		<TD>not thread-safe</TD>
	</TR>
	<TR>
		<TD><TT>boost::signal</TT></TD>
		<TD>432</TD>
		<TD>1310</TD>
		<TD>3362</TD>
		<TD>not thread-safe</TD>
	</TR>
	<TR>
		<TD><TT>boost::signals2</TT></TD>
		<TD>593</TD>
		<TD>2803</TD>
		<TD>2146</TD>
		<TD>thread-safe</TD>
	</TR>
</TABLE>

Debian Linux 5.0, gcc-4.3.2, DEC Alpha EV6 \@500MHz
<TABLE>
	<TR>
		<TH>Implementation</TH>
		<TH>call<BR/>single callback</TH>
		<TH>call<BR/>10 callbacks</TH>
		<TH>connect+disconnect</TH>
		<TH>comments</TH>
	</TR>

	<TR>
		<TD>open-coded<BR> (<TT>std::list</TT> of function pointers)</TD>
		<TD>16</TD>
		<TD>141</TD>
		<TD>458</TD>
		<TD>not thread-safe</TD>
	</TR>
	<TR>
		<TD>open-coded<BR> (<TT>std::list</TT> of <TT>std::function</TT> objects)</TD>
		<TD>32</TD>
		<TD>333</TD>
		<TD>576</TD>
		<TD>not thread-safe</TD>
	</TR>
	<TR>
		<TD><TT>tscb::signal</TT></TD>
		<TD>157</TD>
		<TD>472</TD>
		<TD>2164</TD>
		<TD>thread-safe</TD>
	</TR>
	<TR>
		<TD><TT>sigc::signal</TT></TD>
		<TD>576</TD>
		<TD>812</TD>
		<TD>2885</TD>
		<TD>not thread-safe</TD>
	</TR>
	<TR>
		<TD><TT>boost::signal</TT></TD>
		<TD>796</TD>
		<TD>1810</TD>
		<TD>11241</TD>
		<TD>not thread-safe</TD>
	</TR>
</TABLE>

\subsection performance_reactor I/O dispatching

<TT>n</TT> pipe pairs, <TT>n</TT> handler functions that read a token out of one
pipe and write it into the next one. Numbers indicate clock cycles
per <I>single</I> dispatch operation (one forwarding of the token to the
next pipe).

Debian Linux 5.0, gcc-4.3.2, Intel Celeron \@2GHz
<TABLE>
	<TR>
		<TH>Implementation</TH>
		<TH>32 pipe pairs</TH>
		<TH>64 pipe pairs</TH>
		<TH>128 pipe pairs</TH>
	</TR>
	<TR>
		<TD>open-coded<BR>(<TT>epoll_wait+read+write</TT>)</TD>
		<TD>2425</TD>
		<TD>2439</TD>
		<TD>2477</TD>
	</TR>
	<TR>
		<TD><TT>ACE</TT></TD>
		<TD>3469</TD>
		<TD>3460</TD>
		<TD>3494</TD>
	</TR>
	<TR>
		<TD>\ref tscb::reactor "tscb::reactor"</TD>
		<TD>3292</TD>
		<TD>3308</TD>
		<TD>3344</TD>
	</TR>
	<TR>
		<TD><TT>boost::asio</TT></TD>
		<TD>11406</TD>
		<TD>11426</TD>
		<TD>11536</TD>
	</TR>
</TABLE>

*/

/**
\page page_concurrency Concurrency

Let A,B,C,... denote receivers (i.e. functions that can be registered as
callbacks) and X,Y,Z event sources (e.g.
\ref tscb::signal "signal"s, \ref tscb::reactor "reactor"s
with registration and notification capability). The library
generally supports the following operations:

- <B>Registration</B> of a new callback A,B,... at a notification service X,Y
- <B>Modification</B> of callbacks A,B... pending at X,Y (e.g. changing the set
of events the receiver is interested in)
- <B>Deregistration</B> of callback A,B,... previously registered to X,Y
- <B>Notification</B> through X,Y to all registered callbacks
- <B>Deconstruction</B> of the event provider X,Y (which therefore ceases
to deliver notifications)

All implementations provide the following <I>concurrency</I> guarantees:

- Any concurrent
  <B>Registration</B> A<SUB>1</SUB>, A<SUB>2</SUB>,... to X<SUB>1</SUB>, X<SUB>2</SUB>
  <B>Deregistration</B> B<SUB>1</SUB>, B<SUB>2</SUB> from X<SUB>1</SUB>, X<SUB>2</SUB>,
  <B>Modification</B> B<SUB>1</SUB>, B<SUB>2</SUB> at X<SUB>1</SUB>, X<SUB>2</SUB>,
  <B>Notification</B> through X<SUB>1</SUB>, X<SUB>2</SUB>,
  <B>Deconstruction</B> of Y<SUB>1</SUB>, Y<SUB>2</SUB>
  is safe.
- Any concurrent
  <B>Deregistration</B> B<SUB>1</SUB>, B<SUB>2</SUB> from X<SUB>1</SUB>, X<SUB>2</SUB>,
  <B>Modification</B> B<SUB>1</SUB>, B<SUB>2</SUB> at X<SUB>1</SUB>, X<SUB>2</SUB>,
  <B>Deconstruction</B> of X<SUB>1</SUB>, X<SUB>2</SUB>
  is safe.

All implementations provide the following <I>reentrancy</I> guarantee:
From within a callback A registered to service X, the following
operations may be performed:

- <B>Registration</B> B to X,Y
- <B>Deregistration</B> A,C from X,Y
- <B>Modification</B> A,C at X,Y
- <B>Notification</B> through X,Y
- <B>Deconstruction</B> of Z

Finally, the implementation provides the folliwing <I>consistency</I>
guarantee: If a callback is deregistered it will not be invoked
"subsequently" from the same or other threads. For the same thread,
"subsequently" refers to the normal flow of execution after the \ref
tscb::connection::disconnect "disconnect" operation returns. For other
threads this means that if

- \ref tscb::connection::disconnect "disconnect" returns in thread A
- thread A accesses a memory location with "release" semantics<
- thread B accesses the same memory location with "acquire" semantics

then the corresponding callback will not be invoked from thread B. Note
that this provides essentially "causal consistency" which is what can
also be reasonably expected intuitively.
*/




/**
\page page_implementation_notes Implementation notes

\section sec_deferred_synchronization Deferred synchronization

To achieve design goals of both efficiency on concurrency-safety,
libtscb makes extensive use of <I>deferred synchronization</I>. This
implementation technique allows to have concurrent / re-entrant readers
and writers on the same data structures: Readers incur almost no cost
(besides recording entry into read-side critical section) and operate
basically unobstructed. Modifications by writers are structured
carefully into "two-phase" changes: The first phase operates while
readers might be active, only non-destructive modifications are allowed
then. All potentially destructive modifications are queued up for the
second phase that is run as soon as all readers have left the read-side
critical section.

The synchronization primitives are exposed in headers but are not
considered part of the public API. A functional description is provided
for interested readers solely as a means to understand the operation.

*/


namespace tscb {
}
