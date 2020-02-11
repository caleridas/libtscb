/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/config.h>
#include <tscb/ioready.h>

/** \file ioready.cc */

/**
	\page ioready_descr I/O readiness

	The class \ref tscb::ioready_service "ioready_service" defines the
	interface which receivers of I/O readiness callbacks can use to register
	themselves. Several concrete implementations of this interface exist and
	may be used on different platforms.

	\section ioready_registration Registration for events

	Interested receivers can register functions to be called when file
	descriptors are ready for IO at the
	\ref tscb::ioready_service "ioready_service" interface.
	Receivers can use the
	\ref tscb::ioready_service::watch "ioready_service::watch" functions
	for this purpose; they can be used in the following fashion:

	\code
		class my_io_handler {
		public:
			~my_io_handler()
			{
				::close(descriptor_);
			}

			void bind(tscb::ioready_service *svc)
			{
				svc->watch(
					[this](tscb::ioready_events events)
					{
						handle_input(events);
					}, descriptor_, tscb::ioready_input);
			}

		private:
			void handle_input(tscb::ioready_events event_mask) noexcept
			{
				// process data
			}

			int descriptor_;
		};
	\endcode

	Note that particularly for multi-threaded operation it is strongly
	recommended to use some sort of "smart pointer" to hold the
	reference to the target object -- example using \p std::shared_ptr:

	\code
		class my_io_handler : public std::enable_shared_from_this<my_io_handler> {
		public:
			~my_io_handler()
			{
				::close(descriptor_);
			}

			void bind(tscb::ioready_service *svc)
			{
				svc->watch(
					[p{enable_shared_from_this()}](tscb::ioready_events events)
					{
						p->handle_input(events);
					}, descriptor_, tscb::ioready_input);
			}

		private:
			void handle_input(tscb::ioready_events event_mask) noexcept
			{
				// process data
			}

			int descriptor_;
	\endcode

	Example using \p boost::intrusive_ptr (also applies to \p
	Glib::RefPtr and similar):

	\code
		class my_io_handler {
		public:
			~my_io_handler()
			{
				::close(descriptor_);
			}

			void bind(tscb::ioready_service *svc)
			{
				svc->watch(
					[p{boost::intrusive_ptr<my_io_handler>(this)}](tscb::ioready_events events)
					{
						p->handle_input(events);
					}, descriptor_, tscb::ioready_input);
			}

		private:
			void handle_input(tscb::ioready_events event_mask) noexcept
			{
				// process data
			}

			int descriptor_;
			std::atomic<std::size_t> refcount_;

			friend inline void intrusive_ptr_add_ref(my_io_handler* p) {...}
			friend inline void intrusive_ptr_release(my_io_handler* p) {...}
		};
	\endcode

	\p libtscb guarantees that the function object registered to \ref
	tscb::ioready_service::watch "watch" is kept alive for as long as
	it might possibly be called due to an event, and that it is
	destroyed as early as possible when no event can occur anymore.

	\section ioready_callback_cookies Callback connection handles for ioready callbacks

	The \ref tscb::ioready_service::watch "ioready_service::watch"
	functions return a connection handle that represents the link
	between the callback service provider and the receiver. The return
	value can be stored by the caller:

	\code
		int fd = ...;
		tscb::ioready_connection conn;
		conn = service->watch([fd](tscb::ioready_events){...});
	\endcode

	The connection object can later be used for two purposes: 1. Modify
	the event mask:

	\code
		conn.modify(tscb::ioready_input | tscb::ioready_output);
	\endcode

	This call changes the state the file descriptor is checked for. The
	event mask may be ioready_none which temporarily disables
	notification for both input and output, but note that error
	notifications <I>may</I> still be generated.

	2. Break the connection:

	\code
		conn.disconnect();
	\endcode

	The function will no longer be invoked afterwards (see section \ref
	page_concurrency for a precise definition of the guarantee). The
	function object along with bound parameters will be destroyed
	(possibly immediately, possibly later). It is the caller's
	responsibility to ensure that the file descriptor remains valid (is
	not closed) until the function object is destroyed. See above for
	safe mechanisms to ensure liveness of target object, in principle
	this can be applied to the file descriptor directly:

	\code
		class fd_tracker {
		public:
			fd_tracker(int fd) noexcept : fd_(fd) {}
			~fd_tracker() {::close(fd);}
			inline int fd() const noexcept { return fd_; }
		private:
			int fd_;
		};

		main()
		{

			int fd = socket(...);
			boost::shared_ptr<fd_tracker> fdt(new fd_tracker(fd));
			...
			ioready->watch(
				[fdt](tscb::ioready_events events) {...},
				fdt->fd(), tscb::ioready_input);
		}
	\endcode

	[Side node: \ref tscb::ioready_connection "ioready_connection"
	objects may be downcast to \ref tscb::connection "connection"
	objects, losing the ability to modify the event mask]

	\section ioready_dispatcher_descr ioready dispatchers

	Free-standing implementations of the \ref tscb::ioready_service
	"ioready_service" interface derive from the \ref
	tscb::ioready_dispatcher "ioready_dispatcher" interface. Operating
	system-dependent mechanisms are used to query the state information
	of watched file descriptors. Specifically, the following methods
	are supported:

	- Using the <TT>select</TT> system call:
	  \ref tscb::ioready_dispatcher_select "ioready_dispatcher_select"
	  (available all Posix systems)

	- Using the <TT>poll</TT> system call:
	  \ref tscb::ioready_dispatcher_poll "ioready_dispatcher_poll"
	  (available most Posix systems)

	- Using the <TT>epoll</TT> family of system calls:
	  \ref tscb::ioready_dispatcher_epoll "ioready_dispatcher_epoll"
	  (available on Linux systems)

	- Using the <TT>kqueue</TT> family of system calls:
	  \ref tscb::ioready_dispatcher_kqueue "ioready_dispatcher_kqueue"
	  (available on BSD and derived systems)

	The \ref tscb::ioready_dispatcher "ioready_dispatcher" interface
	adds two methods in addition to those inherited from \ref
	tscb::ioready_service "ioready_service". The first method, \ref
	tscb::ioready_dispatcher::dispatch "ioready_dispatcher::dispatch",
	drives the dispatching mechanism and invokes the callbacks
	registered previously (see section \ref ioready_registration):

	\code
		tscb::ioready_dispatcher *dispatcher = ...;

		std::chrono::steady_clock::duration timeout(1000);

		for (;;) {
			dispatcher->dispatch(&timeout, 16);
		}
	\endcode

	(Refer to \ref tscb::ioready_dispatcher::dispatch
	"ioready_dispatcher::dispatch" for a full description of the
	function). Like in the example above, at least one thread must
	periodically call \ref tscb::ioready_dispatcher::dispatch
	"dispatch"; the function will check the registered receivers and
	process pending IO readiness callbacks.

	The second method, \ref tscb::ioready_dispatcher::wake_up "wake_up"
	allows to notify an ongoing or subsequent \ref
	tscb::ioready_dispatcher::dispatch "dispatch" call to return
	prematurely. \ref tscb::ioready_dispatcher::wake_up "wake_up" is
	both thread-safe and async-signal safe: It can safely be called
	from other threads or even signal handler contexts.

	Applications can use this capability as a mechanism to force a
	premature exit from the dispatcher loop. This is useful when the
	thread busy with dispatching I/O readiness events must also check
	other event sources (e.g. timers, see section \ref
	basic_timer_dispatcher_descr).

	Note:

	- depending on the implementation,
	  \ref tscb::ioready_dispatcher::wake_up "wake_up" may cause one,
	  many, or all threads currently in \ref
	  tscb::ioready_dispatcher::dispatch "ioready_dispatcher::dispatch"
	  to return prematurely.

	- \ref tscb::ioready_dispatcher::wake_up "wake_up" causes premature
	  exit from \ref tscb::ioready_dispatcher::dispatch "dispatch" only
	  once -- unless the trigger is activated again after the call
	  returns, the next call to \ref tscb::ioready_dispatcher::dispatch
	  "dispatch" will wait for events normally.
*/

namespace tscb {

/**
	\class ioready_events
	\brief I/O readiness event mask

	Bitmask encoding possible events on a file descriptor. When
	requesting notification through \ref tscb::ioready_service::watch
	"ioready_service::watch", the caller should build a mask consisting
	of the bitwise | (or) of all events it is interested in. When
	receiving notification, set bits describe the event that occurred.

	The empty event set is represented by ioready_none.

	Possible event bits:

	- ioready_input
	- ioready_output
	- ioready_error
	- ioready_hangup


	\var ioready_none
	\brief Empty event mask


	\var ioready_input
	\brief Input event mask

	Bit indicating "descriptor ready for input"; see
	\ref tscb::ioready_service::watch "ioready_service::watch".


	\var ioready_output
	\brief Output event mask

	Bit indicating "descriptor ready for output"; see
	\ref tscb::ioready_service::watch "ioready_service::watch".


	\var ioready_error
	\brief Error event mask

	Bit indicating "error on descriptor"; see \ref
	tscb::ioready_service::watch "ioready_service::watch". Note that
	you do not have to explicitly request this type of event -- when
	requesting ioready_input or ioready_output events, this event may
	always be delivered on an error condition.


	\var ioready_hangup
	\brief Hangup event mask

	Bit indicating "hangup by peer on descriptor"; see \ref
	tscb::ioready_service::watch "ioready_service::watch". Note that
	you do not have to explicitly request this type of event -- when
	requesting ioready_input or ioready_output events, this event may
	always be delivered on an error condition.
*/


/**
	\class ioready_connection
	\brief Control of a ioready connection between caller and callee.
	\headerfile tscb/ioready.h <tscb/ioready.h>

	Control object for a link between an I/O dispatcher and a receiver:
	This represents file descriptor I/O state event callbacks.
	ioready_connection objects may either refer to an active link or be
	"empty". Applications can \ref disconnect "disconnect" an active
	link through it. it. The library defines more refined
	ioready_connection types, allowing more detailed control for
	specific kinds of connections, but every other ioready_connection
	type can be downcast to \ref ioready_connection.

	Multiple ioready_connection objects may refer to the same link in
	the same way as std::shared_ptr does, although it is advisable to
	keep only a unique reference at a time to avoid confusion.


	\fn ioready_connection::ioready_connection
	\brief Construct empty (disconnected) ioready_connection object.

	Constructs a ioready_connection object that is initially in a a
	"disconnected" state. Active connections may be assigned to it
	later.

	\post !this->\ref is_connected "is_connected()"


	\fn ioready_connection::ioready_connection(detail::intrusive_ptr<link_type> link)
	\brief Construct ioready_connection object referencing link.

	Constructs a ioready_connection object that references the given link. This
	is usually an internal operation only required when implementing
	new ioready_connection types.

	\post !this->\ref is_connected "is_connected()" == link && link->is_connected()


	\fn ioready_connection::ioready_connection(const ioready_connection & other)
	\brief Create shared ioready_connection object.
	\param other Other ioready_connection to share state with.

	Constructs a ioready_connection object sharing state with an existing
	ioready_connection object.


	\fn ioready_connection::ioready_connection(ioready_connection && other)
	\brief Transfer ioready_connection from another object.
	\param other Other ioready_connection object to transfer from.

	Constructs a ioready_connection object taking over state with of an existing
	ioready_connection object.

	\post !other.\ref is_connected "is_connected()"


	\fn ioready_connection & ioready_connection::operator=(const ioready_connection & other)
	\brief Assign ioready_connection making a new reference.

	Make this ioready_connection object reference the same ioready_connection as the
	other. Note that the old ioready_connection represented by this object
	is <I>not</I> \ref disconnect "disconnected" automatically.

	\post this->\ref is_connected "is_connected()" == other.is_connected()


	\fn ioready_connection & ioready_connection::operator=(ioready_connection && other)
	\brief Transfer ioready_connection from other ioready_connection.

	Make this ioready_connection object reference the same ioready_connection as the
	other and turn other ioready_connection into inactive state. Note that the
	old ioready_connection represented by this object is <I>not</I> \ref
	disconnect "disconnected" automatically.

	\post !other.\ref is_connected "is_connected()"


	\fn ioready_connection::disconnect
	\brief Disconnect the callback.

	Break the callback link, stop callback from being delivered
	subsequently. This operation can safely be called when callback
	is in progress, potentially even from other threads. It is
	guaranteed that the corresponding callback function will not
	be called in any execution that "happens after" the return
	of this call. This means:

	- in the same thread, the callback will not be invoked anymore

	- in other threads it might be still invoked if processing of
	  races with the disconnect operation

	- in other threads the callback will also not be called "after"
	  the disconnect call returns anymore XXX define "after"

	The callback function corresponding to the ioready_connection will
	eventually be deleted -- if possible, before this call returns (if
	the signal source is not active), but possibly deferred if the
	signal source is presently processing its callback chain.

	\post !this->\ref is_connected "is_connected()"


	\fn ioready_connection::is_connected
	\brief Determine whether ioready_connection is active.
	\return True if represents an active ioready_connection, false otherwise.

	Determines whether ioready_connection is active.


	\fn ioready_connection::modify
	\brief Change event mask.
	\param events

	Changes event mask for which notification is requested. This takes
	effect on every execution that happens-after the return of this
	call.


	\fn ioready_connection::event_mask
	\brief Get current event mask.
	\returns Current event mask.

	Returns current event mask if connection is active (ioready_none
	otherwise).


	\fn ioready_connection::swap
	\brief Swap contents with other ioready_connection object.


	\fn ioready_connection::link
	\brief Raw access to refcounted link object.


	\fn ioready_connection::get
	\brief Raw access (borrowed) to link object.


	\fn ioready_connection::operator connection() const noexcept
	\brief Downcast to simple connection.
*/



/**
	\class ioready_connection::link_type
	\brief callback link for I/O readiness events on file descriptors

	This class represents a link that has been established by
	registering a callback for I/O readiness events on file (or socket)
	descriptor events (see \ref tscb::ioready_service). It supports
	the base operations also provided by \ref connection::link_type
	and additionally control over the file descriptor event mask.


	\typedef ioready_connection::link_type::pointer
	\brief Reference counted pointer type for link_type.


	\fn ioready_connection::link_type::modify
	\brief Modify event mask.

	To be overridden by I/O dispatcher implementation to provide
	the behavior required by \ref ioready_connection::modify.


	\fn ioready_connection::link_type::event_mask
	\brief Get current event mask.

	To be overridden by I/O dispatcher implementation to provide
	the behavior required by \ref ioready_connection::event_mask.
*/

ioready_connection::link_type::~link_type() noexcept
{
}

/**
	\class scoped_ioready_connection
	\brief Scoped control of a connection for I/O readiness notification.
	\headerfile tscb/connection.h <tscb/connection.h>

	Wrapper control object for a link for I/O callbacks.
	scoped_ioready_connection objects may either refer to an active
	link or be "empty". Applications can \ref disconnect "disconnect"
	an active link through it, and scoped_ioready_connection
	automatically disconnects a link when destroyed. See also \ref
	connection.

	XXX: reference caveats for thread-safety


	\typedef scoped_ioready_connection::link_type
	\brief Alias for \ref ioready_connection::link_type.

	\fn scoped_ioready_connection::scoped_ioready_connection
	\brief Construct empty (disconnected) connection object.

	Constructs a scoped_ioready_connection object that is initially in a a
	"disconnected" state. Active connections may be assigned to it
	later.

	\post !this->\ref is_connected "is_connected()"


	\fn scoped_ioready_connection::scoped_ioready_connection(const ioready_connection & other)
	\brief Create shared connection object.
	\param other Other connection to share state with.

	Constructs a connection object sharing state with an existing
	connection object.


	\fn scoped_ioready_connection::scoped_ioready_connection(scoped_ioready_connection && other)
	\brief Transfer connection from another object.
	\param other Other connection object to transfer from.

	Constructs a connection object taking over state with of an existing
	connection object.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_ioready_connection::scoped_ioready_connection(ioready_connection && other)
	\brief Transfer connection from another object.
	\param other Other connection object to transfer from.

	Constructs a connection object taking over state with of an existing
	connection object.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_ioready_connection & scoped_ioready_connection::operator=(scoped_ioready_connection && other)
	\brief Transfer connection from other connection.

	Transfer connection from other. If this previously represented an
	active connection, it is implicitly disconnected.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_ioready_connection & scoped_ioready_connection::operator=(const ioready_connection& other)
	\brief Copy connection reference from other connection.

	Copy connection reference from other. If this previously
	represented an active connection, it is implicitly disconnected.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_ioready_connection::disconnect
	\brief Disconnect the callback.

	Break the callback link, stop callback from being delivered
	subsequently. This operation can safely be called when callback
	is in progress, potentially even from other threads. It is
	guaranteed that the corresponding callback function will not
	be called in any execution that "happens after" the return
	of this call. This means:

	- in the same thread, the callback will not be invoked anymore

	- in other threads it might be still invoked if processing of
	  races with the disconnect operation

	- in other threads the callback will also not be called "after"
	  the disconnect call returns anymore XXX define "after"

	The callback function corresponding to the connection will
	eventually be deleted -- if possible, before this call returns (if
	the signal source is not active), but possibly deferred if the
	signal source is presently processing its callback chain.

	\post !this->\ref is_connected "is_connected()"


	\fn scoped_ioready_connection::is_connected
	\brief Determine whether connection is active.
	\return True if represents an active connection, false otherwise.

	Determines whether connection is active.


	\fn scoped_ioready_connection::swap
	\brief Swap contents with other connection object.


	\fn scoped_ioready_connection::modify
	\brief Change event mask.
	\param events

	Changes event mask for which notification is requested. This takes
	effect on every execution that happens-after the return of this
	call.


	\fn scoped_ioready_connection::event_mask
	\brief Get current event mask.
	\returns Current event mask.

	Returns current event mask if connection is active (ioready_none
	otherwise).


	\fn scoped_ioready_connection::link
	\brief Raw access to refcounted link object.


	\fn scoped_ioready_connection::get
	\brief Raw access (borrowed) to link object.
*/

/**
	\class ioready_service
	\brief Registration for IO readiness events

	This class provides the registration interface for IO readiness
	callbacks. Receivers can use the \ref watch methods
	of this class to indicate their interest in readiness events
	on a specific file descriptor. See section \ref ioready_registration
	for examples on how to use this interface.


	\fn ioready_service::watch
	\brief Register callback for file descriptor event.

	\param function
		Function to be called in case of readiness for I/O.
	\param fd
		The file descriptor which should be monitored for events.
	\param event_mask
		Set of events for which notification should be delivered.
	\return
		Connection handle.

	This function requests callbacks for IO readiness on a file
	descriptor (on Unix systems the objects for which events can be
	delivered include pipes, sockets, terminal lines, and a number of
	special device files).

	The event_mask indicates what events the callee
	is interested in; it is a bitwise "or" of one of the
	following constants:

	- ioready_input: descriptor is ready for reading
	- ioready_output: descriptor is ready for writing
	- ioready_error: an (unspecified) error occurred on the descriptor
	- ioready_hangup: remote end has closed the connection

	It is also possible to pass ioready_none to allocate all structures
	required for notification, but do not request specific notification
	immediately. This can later be changed through \ref
	ioready_connection::modify to request actual events. Note that \ref
	ioready_connection::modify is noexcept, so it is useful to
	pre-allocate and later activate in context that does not allow
	exceptions anymore.

	The passed function object will be called with a parameter
	indicating the set of events that have occurred. The returned
	connection object may be used to modify the set of watched events
	or cancel the callback.
*/

ioready_service::~ioready_service() noexcept
{
}

/**
	\class ioready_dispatcher
	\brief Dispatcher for I/O readiness events.

	Defines the interface for free-standing I/O readiness dispatching.
	Several system specific implementations provide the OS mechanisms to
	perform this function.


	\fn ioready_dispatcher::dispatch
	\brief Dispatch an event or wait until timeout

	\param timeout
		Timeout or nullptr.
	\param limit
		Maximum number of events to be processed.
	\return
		Number of events processed.

	Checks state of all registered file descriptors and processes
	registered callback functions.

	All pending events are processed up to \p limit  number of events;
	unprocessed events will be processed in further calls to \ref
	ioready_dispatcher::dispatch "dispatch".

	The function will return indicating the number of events processed
	as soon as one the following conditions becomes true:

	- at least one (and up to \p limit) number of events have been processed

	- no event was processed, but a timeout has occured waiting
	  for events, as indicated by the \p timeout parameter; if
	  \p timeout is nullptr, this cannot occur as this function will
	  wait indefinitely

	- the \ref wake_up has been called at least once since last return
	  from \ref ioready_dispatcher::dispatch "dispatch"

	- registration: depending on the dispatcher implementation
	  dispatching may be interrupted if callbacks are registered,
	  modified or cancelled by another thread

	The function is generally reentrant, multiple threads can enter the
	dispatching loop simultaneously. However depending on the concrete
	dispatcher implementation the resulting behaviour may not be
	terribly useful, as some dispatchers will attempt to dispatch the
	same event in multiple threads.


	\fn ioready_dispatcher::dispatch_pending
	\brief Dispatch a number of presently pending events.

	\param limit
		Maximum number of events to be processed.
	\return
		Number of events processed.

	Check state of all registered file descriptors and process
	registered callback functions.

	All pending events are processed up to the \p limit number of
	events. Unprocessed events will be processed in further calls to
	\ref ioready_dispatcher::dispatch "dispatch".

	The function will return immediately after processing events (if
	any). It will not wait for events if none are pending.

	The function is generally reentrant, multiple threads can enter the
	dispatching loop simultaneously. However depending on the concrete
	dispatcher implementation the behaviour may not be terribly useful,
	as some dispatchers will attempt to dispatch the same event in
	multiple threads.


	\fn ioready_dispatcher::wake_up()
	\brief Wake up event dispatcher prematurely.

	Interrupts \ref ioready_dispatcher::dispatch "dispatch": One active
	call (or the next subsequent call if none is active at present) to
	\ref ioready_dispatcher::dispatch "dispatch" function will return
	prematurely even if neither timeout has been reached nor any I/O
	events occured. This function is async-signal safe and thread-safe.

	This functionality is mostly useful in multi-threaded programs to
	interrupt one dispatcher, e.g. because a timeout has to be
	shortened. It is also permissible to activate the trigger from a
	posix signal handler to have the main thread process asynchronously
	queued events.


	\fn ioready_dispatcher::create
	\brief Instantiate ioready_dispatcher

	Instantiates a "standalone" \ref ioready_dispatcher, using the
	dispatching mechanism most suitable for the current platform. The
	function will select the "best" dispatcher available on the current
	platform dynamically; the order of preference is:

	- \ref ioready_dispatcher_kqueue (*BSD and Mac OS X)
	- \ref ioready_dispatcher_epoll (Linux)
	- \ref ioready_dispatcher_poll
	- \ref ioready_dispatcher_select

	May throw std::bad_alloc on allocation error or std::runtime_error
	on exceeding OS resources (e.g. file descriptor limits).
*/

ioready_dispatcher::~ioready_dispatcher() noexcept
{
}

static ioready_dispatcher *
create_ioready_dispatcher_probe() /*throw(std::bad_alloc, std::runtime_error)*/;

#ifdef HAVE_KQUEUE
ioready_dispatcher *
create_ioready_dispatcher_kqueue() /*throw(std::bad_alloc, std::runtime_error)*/;
#endif
#ifdef HAVE_EPOLL
ioready_dispatcher *
create_ioready_dispatcher_epoll() /*throw(std::bad_alloc, std::runtime_error)*/;
#endif
#ifdef HAVE_POLL
ioready_dispatcher *
create_ioready_dispatcher_poll() /*throw(std::bad_alloc, std::runtime_error)*/;
#endif
#ifdef HAVE_SELECT
ioready_dispatcher *
create_ioready_dispatcher_select() /*throw(std::bad_alloc, std::runtime_error)*/;
#endif

namespace {

typedef ioready_dispatcher *(*ioready_dispatcher_creator_func_t)();

static std::atomic<ioready_dispatcher_creator_func_t> ioready_dispatcher_creator_func
	=&create_ioready_dispatcher_probe;

static ioready_dispatcher_creator_func_t probe_functions[] = {
#ifdef HAVE_KQUEUE
	&create_ioready_dispatcher_kqueue,
#endif
#ifdef HAVE_EPOLL
	&create_ioready_dispatcher_epoll,
#endif
#ifdef HAVE_POLL
	&create_ioready_dispatcher_poll,
#endif
#ifdef HAVE_SELECT
	&create_ioready_dispatcher_select,
#endif
	0
};

}

static ioready_dispatcher *
create_ioready_dispatcher_probe()
{
	for (const auto& fn : probe_functions) {
		try {
			ioready_dispatcher* dispatcher = fn();
			ioready_dispatcher_creator_func.store(fn, std::memory_order_relaxed);
			return dispatcher;
		}
		catch (std::runtime_error &) {
			continue;
		}
	}
	throw std::runtime_error("No dispatcher implementation available");
}

std::unique_ptr<ioready_dispatcher>
ioready_dispatcher::create()
{
	auto fn = ioready_dispatcher_creator_func.load(std::memory_order_relaxed);
	return std::unique_ptr<ioready_dispatcher>(fn());
}

ioready_dispatcher *
create_ioready_dispatcher()
{
	return ioready_dispatcher::create().release();
}


}
