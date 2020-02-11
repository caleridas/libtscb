/* -*- C++ -*-
 * (c) 2004 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/connection.h>

namespace tscb {

/**
	\class connection
	\brief Control of a connection between caller and callee.
	\headerfile tscb/connection.h <tscb/connection.h>

	Control object for a link between an event source and a receiver.
	Connection objects may either refer to an active link or be
	"empty". Applications can \ref disconnect "disconnect" an active
	link through it. it. The library defines more refined connection
	types, allowing more detailed control for specific kinds of
	connections, but every other connection type can be downcast to
	\ref connection.

	Multiple connection objects may refer to the same link in the same
	way as std::shared_ptr does, although it is advisable to keep only
	a unique reference at a time to avoid confusion.


	\fn connection::connection
	\brief Construct empty (disconnected) connection object.

	Constructs a connection object that is initially in a a
	"disconnected" state. Active connections may be assigned to it
	later.

	\post !this->\ref is_connected "is_connected()"


	\fn connection::connection(detail::intrusive_ptr<link_type> link)
	\brief Construct connection object referencing link.

	Constructs a connection object that references the given link. This
	is usually an internal operation only required when implementing
	new connection types.

	\post !this->\ref is_connected "is_connected()" == link && link->is_connected()


	\fn connection::connection(const connection & other)
	\brief Create shared connection object.
	\param other Other connection to share state with.

	Constructs a connection object sharing state with an existing
	connection object.


	\fn connection::connection(connection && other)
	\brief Transfer connection from another object.
	\param other Other connection object to transfer from.

	Constructs a connection object taking over state with of an existing
	connection object.

	\post !other.\ref is_connected "is_connected()"


	\fn connection & connection::operator=(const connection & other)
	\brief Assign connection making a new reference.

	Make this connection object reference the same connection as the
	other. Note that the old connection represented by this object
	is <I>not</I> \ref disconnect "disconnected" automatically.

	\post this->\ref is_connected "is_connected()" == other.is_connected()


	\fn connection & connection::operator=(connection && other)
	\brief Transfer connection from other connection.

	Make this connection object reference the same connection as the
	other and turn other connection into inactive state. Note that the
	old connection represented by this object is <I>not</I> \ref
	disconnect "disconnected" automatically.

	\post !other.\ref is_connected "is_connected()"


	\fn connection::disconnect
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


	\fn connection::is_connected
	\brief Determine whether connection is active.
	\return True if represents an active connection, false otherwise.

	Determines whether connection is active.


	\fn connection::swap
	\brief Swap contents with other connection object.


	\fn connection::link
	\brief Raw access to refcounted link object.


	\fn connection::get
	\brief Raw access (borrowed) to link object.
*/

/**
	\class scoped_connection
	\brief Scoped control of a connection between caller and callee.
	\headerfile tscb/connection.h <tscb/connection.h>

	Wrapper control object for a link between an event source and a
	receiver. scoped_connection objects may either refer to an active
	link or be "empty". Applications can \ref disconnect "disconnect"
	an active link through it, and scoped_connection automatically
	disconnects a link when destroyed. See also \ref connection.

	XXX: reference caveats for thread-safety


	\typedef scoped_connection::link_type
	\brief Alias for \ref connection::link_type.

	\fn scoped_connection::scoped_connection
	\brief Construct empty (disconnected) connection object.

	Constructs a scoped_connection object that is initially in a a
	"disconnected" state. Active connections may be assigned to it
	later.

	\post !this->\ref is_connected "is_connected()"


	\fn scoped_connection::scoped_connection(const connection & other)
	\brief Create shared connection object.
	\param other Other connection to share state with.

	Constructs a connection object sharing state with an existing
	connection object.


	\fn scoped_connection::scoped_connection(scoped_connection && other)
	\brief Transfer connection from another object.
	\param other Other connection object to transfer from.

	Constructs a connection object taking over state with of an existing
	connection object.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_connection::scoped_connection(connection && other)
	\brief Transfer connection from another object.
	\param other Other connection object to transfer from.

	Constructs a connection object taking over state with of an existing
	connection object.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_connection & scoped_connection::operator=(scoped_connection && other)
	\brief Transfer connection from other connection.

	Transfer connection from other. If this previously represented an
	active connection, it is implicitly disconnected.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_connection & scoped_connection::operator=(const connection& other)
	\brief Copy connection reference from other connection.

	Copy connection reference from other. If this previously
	represented an active connection, it is implicitly disconnected.

	\post !other.\ref is_connected "is_connected()"


	\fn scoped_connection::disconnect
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


	\fn scoped_connection::is_connected
	\brief Determine whether connection is active.
	\return True if represents an active connection, false otherwise.

	Determines whether connection is active.


	\fn scoped_connection::swap
	\brief Swap contents with other connection object.


	\fn scoped_connection::link
	\brief Raw access to refcounted link object.


	\fn scoped_connection::get
	\brief Raw access (borrowed) to link object.
*/

/**
	\class connection::link_type
	\brief Abstract base of link between sender and receiver.

	This object represents the actual internal link between a
	sender/caller, from which notification is requested, to a
	reciever/callee, to which notification is to be delivered. The
	contain the function object to be called as well as linkage logic
	specific to the kind of link they represent. They are
	reference-counted objects referenced by \ref connection objects as
	well as sender logic to track active callbacks.


	\typedef connection::link_type::pointer
	\brief Reference-counted pointer representation for link objects.

	\fn connection::link_type::disconnect
	\brief Break the connection

	Calling this function will break the notification connection. It
	will cease notifications to be delivered some time after
	this function has returned.


	\fn connection::link_type::is_connected
	\brief Determine whether connection is active.
	\return True if represents an active connection, false otherwise.

	Determines whether connection is active.


	\fn connection::link_type::reference_count
	\brief Number of references to link object.
	\return Number of references.

	Returns number of references to link object. Every \ref connection
	object referring to it will hold a reference, if link is active
	then sender will hold another reference.
*/

connection::link_type::~link_type() noexcept
{
}

}
