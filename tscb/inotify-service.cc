/* -*- C++ -*-
 * (c) 2022 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/inotify-service.h>

/**
	\page inotify_descr Inotify interface

	The \ref tscb::inotify_service interface provides registration
	for inode change monitoring.

	\section inotify_registration Registration for events

	Interested receivers can register functions to be called when file
	descriptors are ready for IO at the
	\ref tscb::inotify_service "inotify_service" interface.
	Receivers can use the
	\ref tscb::inotify_service::inode_watch "inotify_service::inode_watch" functions
	for this purpose; they can be used in the following fashion:

	\code
		class my_inotify_handler {
		public:
			void bind(tscb::inotify_service *svc)
			{
				svc->inode_watch(
					[this](tscb::inotify_events events, uint32_t cookie, const char* name)
					{
						handle_create_dir(name);
					}, "/path/to/dir", IN_CREATE);
			}

		private:
			void handle_input(const char* name) noexcept
			{
				// process data
			}
		};
	\endcode

	Note that particularly for multi-threaded operation it is strongly
	recommended to use some sort of "smart pointer" to hold the
	reference to the target object -- example using \p std::shared_ptr:

	\code
		class my_inotify_handler : public std::enable_shared_from_this<my_inotify_handler> {
		public:
			void bind(tscb::inotify_service *svc)
			{
				svc->inode_watch(
					[p(shared_from_this())](tscb::inotify_events events, uint32_t cookie, const char* name)
					{
						p->handle_create_dir(name);
					}, "/path/to/dir", IN_CREATE);
			}

		private:
			void handle_input(const char* name) noexcept
			{
				// process data
			}
		}

	\endcode

	\p libtscb guarantees that the function object registered to \ref
	tscb::inotify_service::inode_watch "inode_watch" is kept alive for as long as
	it might possibly be called due to an event, and that it is
	destroyed as early as possible when no event can occur anymore.

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

	\section inotify_dispatcher_descr inotify dispatcher

	An implementation of the \ref tscb::inotify_service
	"inotify_service" interface is provided by \ref
	tscb::inotify_dispatcher "inotify_dispatcher". In addition to
	registration, it provides the following methods to handle events:

	- \ref tscb::inotify_dispatcher::dispatch "inotify_dispatcher::dispatch":
	  fetch pending events from the kernel and handle them

	- \ref tscb::inotify_dispatcher::fd "inotify_dispatcher::fd":
	  allow access to the underlying filedescriptor (borrowing it),
	  typically in order to wire it up to
	  \ref tscb::reactor_service "reactor_service" or
	  \ref tscb::ioready_service "ioready_service"
*/

namespace tscb {

/**
	\class inotify_connection
	\brief Control of a inotify connection between caller and callee.
	\headerfile tscb/inotify.h <tscb/inotify.h>

	Control object for a link between an inotify dispatcher and a receiver.
	inotify_connection objects may either refer to an active link or be
	"empty". Applications can \ref disconnect "disconnect" an active
	link through it. it.

	Multiple inotify_connection objects may refer to the same link in
	the same way as std::shared_ptr does, although it is advisable to
	keep only a unique reference at a time to avoid confusion.


	\fn inotify_connection::inotify_connection
	\brief Construct empty (disconnected) inotify_connection object.

	Constructs a inotify_connection object that is initially in a a
	"disconnected" state. Active connections may be assigned to it
	later.

	\post !this->\ref is_connected "is_connected()"


	\fn inotify_connection::inotify_connection(detail::intrusive_ptr<link_type> link)
	\brief Construct inotify_connection object referencing link.

	Constructs a inotify_connection object that references the given link. This
	is usually an internal operation only required when implementing
	new inotify_connection types.

	\post !this->\ref is_connected "is_connected()" == link && link->is_connected()


	\fn inotify_connection::inotify_connection(const inotify_connection & other)
	\brief Create shared inotify_connection object.
	\param other Other inotify_connection to share state with.

	Constructs an inotify_connection object sharing state with an existing
	inotify_connection object.


	\fn inotify_connection::inotify_connection(inotify_connection && other)
	\brief Transfer inotify_connection from another object.
	\param other Other inotify_connection object to transfer from.

	Constructs a inotify_connection object taking over state with of an existing
	inotify_connection object.

	\post !other.\ref is_connected "is_connected()"


	\fn inotify_connection & inotify_connection::operator=(const inotify_connection & other)
	\brief Assign inotify_connection making a new reference.

	Make this inotify_connection object reference the same inotify_connection as the
	other. Note that the old inotify_connection represented by this object
	is <I>not</I> \ref disconnect "disconnected" automatically.

	\post this->\ref is_connected "is_connected()" == other.is_connected()


	\fn inotify_connection & inotify_connection::operator=(inotify_connection && other)
	\brief Transfer inotify_connection from other inotify_connection.

	Make this inotify_connection object reference the same inotify_connection as the
	other and turn other inotify_connection into inactive state. Note that the
	old inotify_connection represented by this object is <I>not</I> \ref
	disconnect "disconnected" automatically.

	\post !other.\ref is_connected "is_connected()"


	\fn inotify_connection::disconnect
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

	The callback function corresponding to the inotify_connection will
	eventually be deleted -- if possible, before this call returns (if
	the signal source is not active), but possibly deferred if the
	signal source is presently processing its callback chain.

	\post !this->\ref is_connected "is_connected()"


	\fn inotify_connection::is_connected
	\brief Determine whether inotify_connection is active.
	\return True if represents an active inotify_connection, false otherwise.

	Determines whether inotify_connection is active.


	\fn inotify_connection::swap
	\brief Swap contents with other inotify_connection object.


	\fn inotify_connection::link
	\brief Raw access to refcounted link object.


	\fn inotify_connection::get
	\brief Raw access (borrowed) to link object.


	\fn inotify_connection::operator connection() const noexcept
	\brief Downcast to simple connection.
*/

/**
	\class inotify_connection::link_type
	\brief callback link for I/O readiness events on file descriptors

	This class represents a link that has been established by
	registering a callback for inotify events
	(see \ref tscb::inotify_service). It supports
	the base operations also provided by \ref connection::link_type
	and additionally control over the file descriptor event mask.


	\typedef inotify_connection::link_type::pointer
	\brief Reference counted pointer type for link_type.
*/

inotify_connection::link_type::~link_type() noexcept
{
}

/**
	\class scoped_inotify_connection
	\brief Scoped control of a connection for inotify notification.
	\headerfile tscb/inotify-service.h <tscb/inotify-service.h>

	Wrapper control object for a link for I/O callbacks.
	scoped_inotify_connection objects may either refer to an active
	link or be "empty". Applications can \ref disconnect "disconnect"
	an active link through it, and scoped_inotify_connection
	automatically disconnects a link when destroyed. See also \ref
	connection.

	XXX: reference caveats for thread-safety


	\typedef scoped_inotify_connection::link_type
	\brief Alias for \ref inotify_connection::link_type.

	\fn scoped_inotify_connection::scoped_inotify_connection
	\brief Construct empty (disconnected) connection object.

	Constructs a scoped_inotify_connection object that is initially in a a
	"disconnected" state. Active connections may be assigned to it
	later.

	\post !this->\ref is_connected "is_connected()"


	\fn scoped_inotify_connection::scoped_inotify_connection(const inotify_connection & other)
	\brief Create shared connection object.
	\param other Other connection to share state with.

	Constructs a connection object sharing state with an existing
	connection object.


	\fn scoped_inotify_connection::scoped_inotify_connection(scoped_inotify_connection && other)
	\brief Transfer connection from another object.
	\param other Other connection object to transfer from.

	Constructs a connection object taking over state with of an existing
	connection object.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_inotify_connection::scoped_inotify_connection(inotify_connection && other)
	\brief Transfer connection from another object.
	\param other Other connection object to transfer from.

	Constructs a connection object taking over state with of an existing
	connection object.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_inotify_connection & scoped_inotify_connection::operator=(scoped_inotify_connection && other)
	\brief Transfer connection from other connection.

	Transfer connection from other. If this previously represented an
	active connection, it is implicitly disconnected.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_inotify_connection & scoped_inotify_connection::operator=(const inotify_connection& other)
	\brief Copy connection reference from other connection.

	Copy connection reference from other. If this previously
	represented an active connection, it is implicitly disconnected.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_inotify_connection::disconnect
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


	\fn scoped_inotify_connection::is_connected
	\brief Determine whether connection is active.
	\return True if represents an active connection, false otherwise.

	Determines whether connection is active.


	\fn scoped_inotify_connection::swap
	\brief Swap contents with other connection object.

	\fn scoped_inotify_connection::link
	\brief Raw access to refcounted link object.

	\fn scoped_inotify_connection::get
	\brief Raw access (borrowed) to link object.
*/

/**
	\class inotify_service
	\brief Interface for inotify events registration.

	Defines the interface to allow listening for inotify
	events.

	\fn inotify_service::inode_watch
	\brief Watch specified inode and register change callback.

	\param function
		Callback to invoke when event occurs.
	\param path
		File path of inode to watch.
	\param event_mask
		Events to subscribe to.
	\return
		Connection object for this watch.

	Sets up to watch given events on inotify identified by path.
	If the watch cannot be set up (e.g. the path does not exist),
	this will return an "empty" connection object. Caller can
	check this via the \ref inotify_connection::is_connected
	method.
*/

inotify_service::~inotify_service() noexcept
{
}

}
