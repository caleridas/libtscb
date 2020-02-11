/* -*- C++ -*-
 * (c) 2010 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/config.h>
#include <tscb/detail/fd-handler-table.h>

namespace tscb {
namespace detail {

/**
	\class fd_handler_table::delayed_handler_release
	\brief Auxiliary structure holding handlers to be released.

	This auxiliary class is used as a container for handlers that have
	been removed and can be finalized now that it is guaranteed that
	they cannot take part in any notification anymore.

	Users should simply destroy any instance of this class at
	appropriate point in time, or explicitly call the \ref clear
	method. See class description of \ref fd_handler_table regarding
	synchronization and finalizing handlers.


	\fn fd_handler_table::delayed_handler_release::delayed_handler_release(delayed_handler_release && other)
	\brief Move handlers to be released.


	\fn fd_handler_table::delayed_handler_release& fd_handler_table::delayed_handler_release::operator=(delayed_handler_release && other)
	\brief Move handlers to be released.


	\fn fd_handler_table::delayed_handler_release::clear
	\brief Release all handlers.
*/

/**
	\class fd_handler_table::link_type
	\brief Abstract base for I/O dispatchers using fd handler table


	\typedef fd_handler_table::link_type::pointer
	\brief Reference-counted pointer representation for link objects.


	\fn fd_handler_table::link_type::link_type(
			std::function<void(ioready_events)> fn,
			int fd,
			ioready_events event_mask) noexcept
	\param fn Callback function to be installed.
	\param fd File descriptor to be monitored
	\param event_mask Events to be monitored initially.
	\brief Create link for I/O readiness monitoring

	Creates a link object for monitoring file descriptor for events and
	triggering callback function.


	\fn fd_handler_table::link_type::disconnect
	\brief Break the connection

	To be overridden by I/O dispatcher implementation to provide the
	behavior required by \ref ioready_connection::disconnect. Should
	end up calling \ref fd_handler_table::remove.


	\fn fd_handler_table::link_type::is_connected
	\brief Determine whether connection is active.
	\return True if represents an active connection, false otherwise.

	Determines whether connection is active.


	\fn fd_handler_table::link_type::modify
	\brief Modify event mask.
	\param new_event_mask New event mask to be installed

	To be overridden by I/O dispatcher implementation to provide the
	behavior required by \ref ioready_connection::modify. Should
	end up calling \ref fd_handler_table::modify.


	\fn fd_handler_table::link_type::fd
	\brief Get file descriptor.
	\returns File descriptor associated with this link.

	Returns the file descriptor that this callback link wants to
	monitor. To be used by dispatcher implementations to register with
	operating system.
*/

fd_handler_table::link_type::~link_type() noexcept
{
}

/**
	\brief Get current event mask.
	\returns Current event mask.

	Retrieve present event mask for this link. Overridable by I/O
	dispatcher implementation, although not recommended.
*/
ioready_events
fd_handler_table::link_type::event_mask() const noexcept
{
	return event_mask_;
}


fd_handler_table::delayed_handler_release::~delayed_handler_release() noexcept
{
	clear();
}

fd_handler_table::delayed_handler_release::delayed_handler_release(link_type * link)
	: link_(link)
{
}

fd_handler_table::delayed_handler_release::delayed_handler_release() noexcept
	: link_(nullptr)
{
}

fd_handler_table::delayed_handler_release::delayed_handler_release(
	delayed_handler_release && other) noexcept
	: link_(other.link_)
{
	other.link_ = nullptr;
}

fd_handler_table::delayed_handler_release &
fd_handler_table::delayed_handler_release::operator=(delayed_handler_release && other) noexcept
{
	clear();
	link_ = other.link_;
	other.link_ = nullptr;
	return *this;
}

void
fd_handler_table::delayed_handler_release::clear() noexcept
{
	link_type * current = link_;
	while (current) {
		link_type * next = current->inactive_next_;
		current->fn_ = nullptr;
		intrusive_ptr_release(current);
		current = next;
	}
	link_ = nullptr;
}


void
fd_handler_table::deallocate_old_tables() noexcept
{
	table * tab = table_.load(std::memory_order_relaxed);
	table * old = tab->old_;
	tab->old_ = nullptr;
	while (old) {
		table * next = old->old_;
		delete old;
		old = next;
	}
}

ioready_events
fd_handler_table::chain::compute_event_mask() const noexcept
{
	ioready_events mask = ioready_none;

	link_type * link = active_.load(std::memory_order_relaxed);
	while (link) {
		mask |= link->event_mask();
		link = link->active_next_.load(std::memory_order_relaxed);
	}

	return mask;
}

/* may throw std::bad_alloc */
fd_handler_table::table::table(size_t capacity)
	: capacity_(capacity), entries_(new std::atomic<chain *>[capacity]), old_(nullptr)
{
	for (std::size_t n = 0; n < capacity; ++n) {
		entries_[n].store(nullptr, std::memory_order_relaxed);
	}
}

void
fd_handler_table::table::clear_entries() noexcept
{
	for (size_t n = 0; n < capacity_; ++n) {
		delete entries_[n].load(std::memory_order_relaxed);
	}
}

fd_handler_table::~fd_handler_table() noexcept
{
	table * tab = table_.load(std::memory_order_consume);
	tab->clear_entries();
	while (tab) {
		table * tmp = tab->old_;
		delete tab;
		tab = tmp;
	}
}

/**
	\class fd_handler_table
	\brief Handler table for file descriptor events.
	\headerfile tscb/detail/fd-handler-table.h <tscb/detail/fd-handler-table.h>

	This auxiliary class maintains the handler functions for events on
	file descriptors. It is to be used in conjunction with OS-specific
	mechanisms to retrieve events on file descriptors, and helps with
	dispatching them to the correct observer functions.

	The class implements three categories of public members methods:

	- "non-mutating": these methods do not (directly) change internal state,
	  but merely call out into other functions (that are allowed to
	  recursively call into "mutating" functions). They are the
	  following methods:
	  - notify
	  - disconnect_all
	- "mutating": these methods update the handler registry in specific
	  ways. They are the following methods:
	  - insert
	  - modify
	  - remove
	- "synchronizing": function that performs asynchronous cleanups.
	  This contains only the following method:
	  - synchronize

	The concurrency rules are as follows:

	- "non-mutating" functions can be run concurrently with every
	  "mutating" function, but cannot run concurrently with
	  "synchronize"

	- a "mutating" functions can only run concurrently with
	  "non-mutating" functions but not with any other "mutating"
	  function or "synchronize"
	- "synchronize" cannot run concurrently with any other function
	  including itself.

	\warning
	This class by itself needs synchronization to be handled by caller
	as described above. If used incorrectly it will also leak memory of
	registered links. It is essential that "disconnect_all" is called
	at an appropriate point before destructing this object. This class
	is not a general purpose API but an internal implementation tool
	used in the different I/O handler implementations.
*/

/**
	\brief Create empty handler table.

	Creates a new table mapping file descriptors to chains of callback
	functions. The table is empty (i.e. does not map any file
	descriptor) but has some space reserved initially as an
	optimization.

	May throw std::bad_alloc if initial table cannot be allocated.

	\param initial Hint for initial size of table
*/
fd_handler_table::fd_handler_table(size_t initial)
	: table_(new table(initial))
	, inactive_(nullptr)
	, cookie_(0)
	, need_cookie_sync_(false)
{
}

/**
	\brief Register notifier link

	Registers the given link for the file descriptor that it holds. The
	old and new event mask effective for the file descriptor are
	computed and passed back. This allows the caller to determine
	necessary change of registration for the file descriptor with the
	operating system.

	The function may need to enlarge the internal table and may throw
	std::bad_alloc on failure. In this case, the state of object
	remains unchanged (strong exception safety guarantee). On success
	this will acquire a new reference for the link (increase its
	reference count).

	This is a "mutating" function (see concurrency notes in class
	description).

	\param link The link to be inserted.

	\param old_mask Previous event mask for this file descriptor
	(before inserting this link).

	\param new_mask New event mask for this file descriptor (after
	inserting this link).

	\pre link has never been registered with any other \ref
	fd_handler_table instance

	\post link is registered /or/ std::bad_alloc is thrown
*/

void
fd_handler_table::insert(
	link_type * link,
	ioready_events & old_mask,
	ioready_events & new_mask)
{
	chain * ch = get_create_chain(link->fd_);

	/* Note: from this point onwards, no memory allocation is performed anymore,
	 * no exception can be thrown. */
	intrusive_ptr_add_ref(link);

	/* compute old/new event mask */
	old_mask = ioready_none;
	link_type * tmp = ch->active_.load(std::memory_order_relaxed);
	while (tmp) {
		old_mask |= tmp->event_mask_;
		tmp = tmp->active_next_.load(std::memory_order_relaxed);
	}
	new_mask = old_mask | link->event_mask_;

	/* prepare element */
	link->prev_ = ch->last_;
	link->next_ = nullptr;
	link->active_next_.store(nullptr, std::memory_order_relaxed);

	/* we are now going to "publish" this element; since we may be
	inserting multiple references, just issue a thread fence once
	and use relaxed memory order */
	atomic_thread_fence(std::memory_order_release);

	/* add element to active list; find all elements that have been removed
	from the full list and thus terminate the active list; point them to
	the newly-added element */

	tmp = ch->last_;

	for (;;) {
		if (!tmp) {
			if (ch->active_.load(std::memory_order_relaxed) == 0) {
				ch->active_.store(link, std::memory_order_relaxed);
			}
			break;
		}
		if (tmp->active_next_.load(std::memory_order_relaxed)) {
			break;
		}
		tmp->active_next_.store(link, std::memory_order_relaxed);
		tmp = tmp->prev_;
	}

	if (ch->last_) {
		ch->last_->next_ = link;
	} else {
		ch->first_ = link;
	}

	ch->last_ = link;
}

/**
	\brief Unregister notifier link

	Removes the given link. The old and new event mask effective for
	the file descriptor are computed and passed back. This allows the
	caller to determine necessary change of registration for the file
	descriptor with the operating system.

	The reference for this link is not dropped immediately, but will be
	transferred out by the "synchronize" operation below.

	This is a "mutating" function (see concurrency notes in class
	description).

	\param link The link to be removed.

	\param old_mask Previous event mask for this file descriptor
	(before removing this link).

	\param new_mask New event mask for this file descriptor (after
	removing this link).

	\pre link has been registered via \ref insert

	\post link is not registered
*/
void
fd_handler_table::remove(
	link_type * link,
	ioready_events & old_mask,
	ioready_events & new_mask) noexcept
{
	chain * ch = get_chain(link->fd_);

	/* remove protocol: remove element from active list; we have to make
	sure that all elements that pointed to "us" within
	the active chain now point to the following element,
	so this element is skipped from within the active chain */
	link_type * tmp = link->prev_;
	link_type * next = link->active_next_.load(std::memory_order_relaxed);
	for (;;) {
		if (!tmp) {
			if (ch->active_.load(std::memory_order_relaxed) == link) {
				ch->active_.store(next, std::memory_order_release);
			}
			break;
		}
		if (tmp->active_next_.load(std::memory_order_relaxed) != link) {
			break;
		}
		tmp->active_next_.store(next, std::memory_order_release);
		tmp = tmp->prev_;
	}

	/* compute old/new event masks */
	new_mask = ioready_none;
	tmp = ch->active_.load(std::memory_order_relaxed);
	while (tmp) {
		new_mask |= tmp->event_mask_;
		tmp = tmp->active_next_.load(std::memory_order_relaxed);
	}
	old_mask = new_mask | link->event_mask_;

	/* If this is the last callback registered for this descriptor,
	 * then user program might close and reuse it immediately. This
	 * could lead to a pending event for the old descriptor to be
	 * delivered and interpreted as an event for the new chain. Guard
	 * against this by incrementing global cookie and assigning it to
	 * the chain: Any event with "lower" cookie value will be disregarded
	 * as belonging to the old descriptor */
	if (ch->active_.load(std::memory_order_relaxed) == 0) {
		uint32_t old_cookie = cookie_.fetch_add(1, std::memory_order_relaxed);
		uint32_t new_cookie = old_cookie + 1;
		ch->cookie_.store(new_cookie, std::memory_order_relaxed);
		if (((old_cookie ^ new_cookie) & (1<<16)) != 0) {
			need_cookie_sync_ = true;
		}
	}

	/* put into list of elements marked for deferred deletion */
	link->inactive_next_ = inactive_;
	inactive_ = link;
}

/**
	\brief Change event mask for link.

	Changes event mask for link to the given mask. The old and new
	event mask effective for the file descriptor are computed and
	passed back. This allows the caller to determine necessary change
	of registration for the file descriptor with the operating system.

	This is a "mutating" function (see concurrency notes in class
	description).

	\param link The for which the event mask should be modified.

	\param mask The new event mask to be installed for the given
	callback link.

	\param old_mask Previous event mask for this file descriptor
	(before modifying this link).

	\param new_mask New event mask for this file descriptor (after
	modifying this link).

	\pre link has been registered via \ref insert

	\post link->event_mask() == mask
*/
void
fd_handler_table::modify(
	link_type * link,
	ioready_events mask,
	ioready_events & old_mask,
	ioready_events & new_mask) noexcept
{
	chain * ch = get_chain(link->fd_);
	old_mask = ch->compute_event_mask();
	link->event_mask_ = mask;
	new_mask = ch->compute_event_mask();
}

/**
	\brief Call "disconnect" on each link

	Calls the \ref link_type::disconnect "disconnect" method of each
	link registered. This method is virtual and to be implemented by
	actual I/O dispatcher using \ref fd_handler_table, but is expected
	to result in calling \ref remove for each link as a consequence.

	Returns true if anything was disconnected.

	This is a "non-mutating" function (see concurrency notes in class
	description).
*/
bool
fd_handler_table::disconnect_all() noexcept
{
	bool any_disconnected = false;
	table * tab = table_.load(std::memory_order_consume);
	for (size_t n = 0; n < tab->capacity_; n++) {
		chain * ch = tab->entries_[n].load(std::memory_order_consume);
		if (!ch) {
			continue;
		}
		link_type * link = ch->active_.load(std::memory_order_consume);
		while (link) {
			any_disconnected = true;
			link->disconnect();
			link = ch->active_.load(std::memory_order_consume);
		}
	}

	return any_disconnected;
}

/**
	\fn fd_handler_table::cookie
	\brief Determine "call cookie" for fd reuse

	Determine the "cookie" to be passed into the "notify" operation
	below for handling actual events. The use pattern is at follows:

	- obtain "cookie" for later use
	- obtain event(s) on file descriptor(s) from operating system
	- deliver notification to observers, verifying by cookie that
	  the file descriptors have not changed intermittently

	This is a "non-mutating" function (see concurrency notes in class
	description).
*/

/**
	\brief Notify all callbacks for fd

	Notify the callbacks registered for the given file descriptor. If
	cookie on file descriptor has changed since obtaining it (see
	cookie function above), then notification is dropped.

	If any of the called observer function throws, the exception is
	passed through to caller. That "may" cause notifications to be lost
	if there are multiple observers on a single file descriptor. This
	function never throws otherwise.

	This is a "non-mutating" function (see concurrency notes in class
	description).

	\param fd File descriptor for which notifications should be delivered.

	\param events Events observed on file descriptor

	\param call_cookie Cookie to determine changes to file descriptors
	racing with OS notification. See \ref cookie method.
*/
void
fd_handler_table::notify(int fd, ioready_events events, uint32_t call_cookie)
{
	std::size_t index = static_cast<std::size_t>(fd);
	table * tab = table_.load(std::memory_order_consume);
	if (index >= tab->capacity_) {
		return;
	}

	chain * ch = tab->entries_[index].load(std::memory_order_consume);
	if (!ch) {
		return;
	}

	int32_t delta = ch->cookie_.load(std::memory_order_relaxed) - call_cookie;
	if (delta > 0) {
		return;
	}

	link_type * link = ch->active_.load(std::memory_order_consume);
	while (link) {
		if ((events & link->event_mask_) != 0) {
			link->fn_(events & link->event_mask_);
		}
		link = link->active_next_.load(std::memory_order_consume);
	}
}

/**
	\brief Synchronize to resolve concurrent readers/writers

	This function must be called at least once after any of the mutating
	functions above. It returns a descriptor of handlers that can be
	cleared -- this should be done without holding any locks.

	This is a "synchronizing" function (see concurrency notes in class
	description).

	\returns a \ref delayed_handler_release structure. Destroying this
	structure finalizes handlers that are removed but may have been
	racing with concurrent notification.
*/
fd_handler_table::delayed_handler_release
fd_handler_table::synchronize() noexcept
{
	deallocate_old_tables();
	table * tab = table_.load(std::memory_order_relaxed);

	/* remove inactive callbacks */
	link_type * link = inactive_;
	while (link) {
		chain * ch = tab->entries_[link->fd_].load(std::memory_order_relaxed);
		if (link->prev_) {
			link->prev_->next_ = link->next_;
		} else {
			ch->first_ = link->next_;
		}
		if (link->next_) {
			link->next_->prev_ = link->prev_;
		} else {
			ch->last_ = link->prev_;
		}
		link = link->inactive_next_;
	}

	if (need_cookie_sync_) {
		need_cookie_sync_ = false;
		uint32_t current_cookie = cookie_.load(std::memory_order_relaxed);
		for (size_t n = 0; n < tab->capacity_; ++n) {
			chain * ch = tab->entries_[n];
			if (ch) {
				ch->cookie_.store(current_cookie, std::memory_order_relaxed);
			}
		}
	}

	/* return first inactive callback so they can be deallocated
	outside the lock */
	link = inactive_;
	inactive_ = nullptr;

	return delayed_handler_release(link);
}

ioready_events
fd_handler_table::compute_event_mask(int fd) noexcept
{
	chain * ch = get_chain(fd);
	return ch ? ch->compute_event_mask() : ioready_none;
}

/* may throw std::bad_alloc */
fd_handler_table::chain *
fd_handler_table::get_create_chain(int fd)
{
	if (fd < 0) {
		throw std::bad_alloc();
	}
	std::size_t index = static_cast<std::size_t>(fd);

	table * tab = table_.load(std::memory_order_relaxed);
	if (index >= tab->capacity_) {
		tab = extend_table(tab, index + 1);
	}

	chain * ch = tab->entries_[index].load(std::memory_order_relaxed);
	if (!ch) {
		ch = new chain();
		tab->entries_[index].store(ch, std::memory_order_release);
	}

	return ch;
}

fd_handler_table::chain *
fd_handler_table::get_chain(int fd) noexcept
{
	std::size_t index = static_cast<std::size_t>(fd);
	table * tab = table_.load(std::memory_order_relaxed);
	return index < tab->capacity_ ? tab->entries_[index].load(std::memory_order_relaxed) : nullptr;
}

/* may throw std::bad_alloc */
fd_handler_table::table *
fd_handler_table::extend_table(table * tab, std::size_t required_capacity)
{
	std::size_t new_capacity = std::max(tab->capacity_ * 2, required_capacity);
	table * newtab = new table(new_capacity);
	for (size_t n = 0; n < tab->capacity_; ++n) {
		newtab->entries_[n].store(tab->entries_[n].load(std::memory_order_relaxed), std::memory_order_relaxed);
	}

	newtab->old_ = tab;

	table_.store(newtab, std::memory_order_release);
	tab = newtab;

	return tab;
}

}
}
