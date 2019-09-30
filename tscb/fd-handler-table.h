/* -*- C++ -*-
 * (c) 2010 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#ifndef TSCB_FD_HANDLER_TABLE_H
#define TSCB_FD_HANDLER_TABLE_H

#include <cstdint>

#include <tscb/ioready.h>

namespace tscb {

/** \brief Handle table for file descriptor events

	This auxiliary class maintains the handler functions for events on file
	descriptors. It is to be used in conjunction with OS-specific mechanisms
	to retrieve events on file descriptors, and helps with dispatching them to
	the correct observer functions.

	The class implements three categories of public members methods:

	- "non-mutating": these methods do not (directly) change internal state,
	  but merely call out into other functions (that are allowed to recursively
	  call into "mutating" functions). They are the following methods:
	  - notify
	  - disconnect_all
	- "mutating": these methods update the handler registry in specific ways.
	  They are the following methods:
	  - insert
	  - modify
	  - remove
	- "synchronizing": function that performs asynchronous cleanups. This
	  contains only the following method:
	  - synchronize

	The concurrency rules are as follows:
	- "non-mutating" functions can be run concurrently with every "mutating"
	  function, but cannot run concurrently with "synchronize"
	- a "mutating" functions can only run concurrently with "non-mutating"
	  functions but not with any other "mutating" function or "synchronize"
	- "synchronize" cannot run concurrently with any other function including
	  itself.

	CAUTION: the class by itself needs synchronization to be handled by
	caller, and if used incorrectly will also leak memory of registered
	links. It is essential that "disconnect_all" is called at an appropriate
	point before destructing this object.
*/
class fd_handler_table {
public:
	class delayed_handler_release;

	class link_type : public ioready_connection::link_type {
	public:
		using pointer = boost::intrusive_ptr<link_type>;

		link_type(
			std::function<void(ioready_events)> fn,
			int fd,
			ioready_events event_mask) noexcept
			: fn_(std::move(fn)), fd_(fd), event_mask_(event_mask)
		{
		}

		~link_type() noexcept override;

		void
		disconnect() noexcept override = 0;

		bool
		is_connected() const noexcept override = 0;

		virtual void
		modify(ioready_events new_event_mask) noexcept = 0;

		ioready_events
		event_mask() const noexcept override;

		inline int
		fd() const noexcept
		{
			return fd_;
		}

		inline void
		clear_function() noexcept
		{
			fn_ = nullptr;
		}

	private:
		std::function<void(ioready_events)> fn_;
		std::atomic<link_type *> active_next_;
		link_type * prev_;
		link_type * next_;
		link_type * inactive_next_;
		int fd_;
		ioready_events event_mask_;

		friend class fd_handler_table;
		friend class delayed_handler_release;
	};

	class delayed_handler_release {
	public:
		~delayed_handler_release() noexcept;
		delayed_handler_release() noexcept;
		delayed_handler_release(const delayed_handler_release &) = delete;
		delayed_handler_release(delayed_handler_release && other) noexcept;

		delayed_handler_release &
		operator=(const delayed_handler_release &) = delete;
		delayed_handler_release &
		operator=(delayed_handler_release && other) noexcept;

		void
		clear() noexcept;

	private:
		delayed_handler_release(link_type * link);

		link_type * link_;

		friend class fd_handler_table;
	};

	~fd_handler_table() noexcept;

	explicit
	fd_handler_table(size_t initial = 32)  /*throw(std::bad_alloc)*/;

	/** \brief Register notifier link

		Registers the given link for the file descriptor that it holds.
		The old and new event mask effective for the file descriptor are
		computed and passed back into the given references as well.

		This will acquire a new reference for the link (increase its reference
		count).

		This is a "mutating" function (see concurrency notes above).
	*/
	void
	insert(
		link_type * link,
		ioready_events & old_mask,
		ioready_events & new_mask) /*throw(std::bad_alloc)*/;

	/** \brief Unregister notifier link

		Removes the given link.
		The old and new event mask effective for the file descriptor are
		computed and passed back into the given references as well.

		The reference for this link is not dropped immediately, but will be
		transferred out by the "synchronize" operation below.

		This is a "mutating" function (see concurrency notes above).
	*/
	void
	remove(
		link_type * link,
		ioready_events & old_mask,
		ioready_events & new_mask) noexcept;

	/** \brief Change event mask for link.

		Changes event mask for link to the given mask.
		The old and new event mask effective for the file descriptor are
		computed and passed back into the given references as well.

		This is a "mutating" function (see concurrency notes above).
	*/
	void
	modify(
		link_type * link,
		ioready_events mask,
		ioready_events & old_mask,
		ioready_events & new_mask) noexcept;

	/** \brief Call "disconnect" on each link

		Calls the "disconnect" method of each link registered. The method is
		virtual, but is expected to result in calling "remove" for each link
		as a consequence.

		Returns true if anything was disconnected.

		This is a "non-mutating" function (see concurrency notes above).
	*/
	bool
	disconnect_all() noexcept;

	/** \brief Determine "call cookie" for fd reuse

		Determine the "cookie" to be passed into the "notify" operation below
		for handling actual events. The use pattern is at follows:

		- obtain "cookie" for later use
		- obtain event(s) on file descriptor(s) from operating system
		- deliver notification to observers, verifying by cookie that
		  the file descriptors have not changed intermittently

		This is a "non-mutating" function (see concurrency notes above).
	*/
	inline uint32_t
	cookie() const noexcept;

	/** \brief Notify all callbacks for fd

		Notify the callbacks registered for the given file descriptor. If cookie
		on file descriptor has changed since obtaining it (see cookie function
		above), then notification is dropped.

		If any of the called observer function throws, the exception is passed
		through to caller. That "may" cause notifications to be lost if there
		are multiple observers on a single file descriptor.

		This is a "non-mutating" function (see concurrency notes above).
	*/
	void
	notify(int fd, ioready_events events, uint32_t call_cookie);

	/** \brief Synchronize to resolve concurrent readers/writers

		This function must be called at least once after any of the mutating
		functions above. It returns a descriptor of handlers that can be
		cleared -- this should be done without holding any locks.

		This is a "synchronizing" function (see concurrency notes above).
	*/
	delayed_handler_release
	synchronize() noexcept;

private:
	struct chain {
		chain() noexcept : active_(nullptr), first_(nullptr), last_(nullptr), cookie_(0) {}

		std::atomic<link_type *> active_;
		link_type * first_;
		link_type * last_;
		std::atomic<uint32_t> cookie_;

		ioready_events
		compute_event_mask() const noexcept;
	};

	struct table {
		explicit
		table(size_t initial) /* throw(std::bad_alloc) */;

		void
		clear_entries() noexcept;

		size_t capacity_;
		std::unique_ptr<std::atomic<chain *>[]> entries_;
		table * old_;
	};

	/** \brief Compute event mask for descriptor

		Computes the effective event mask to check for on the given file
		descriptor.

		This operation must be called with write lock held.
	*/
	ioready_events
	compute_event_mask(int fd) noexcept;

	chain *
	get_create_chain(int fd) /* throw(std::bad_alloc) */;

	chain *
	get_chain(int fd) noexcept;

	table *
	extend_table(
		table * tab,
		std::size_t required_capacity) /*throw(std::bad_alloc)*/;

	void
	deallocate_old_tables() noexcept;

	std::atomic<table *> table_;
	link_type * inactive_;
	std::atomic<uint32_t> cookie_;
	bool need_cookie_sync_;
};

inline uint32_t
fd_handler_table::cookie() const noexcept
{
	return cookie_.load(std::memory_order_relaxed);
}

}

#endif
