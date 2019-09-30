/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#ifndef TSCB_IOREADY_POLL_H
#define TSCB_IOREADY_POLL_H

#include <vector>

#include <sys/poll.h>

#include <tscb/ioready.h>
#include <tscb/deferred.h>
#include <tscb/fd-handler-table.h>

namespace tscb {

/**
	\brief Dispatcher for IO readiness events using the
	<TT>poll</TT> system call

	This class supports collecting the IO readiness state of
	a set of file descriptors using the <TT>poll</TT> system
	call, and dispatching callbacks to receivers that have
	registered themselves for events on specific file descriptors.

	The <TT>poll</TT> system call usually performs considerably
	better than <TT>select</TT>, though it has the same
	asymptotic behaviour (and is thus not very well-suited for
	watching large numbers of mostly idle descriptors).
*/
class ioready_dispatcher_poll : public ioready_dispatcher {
public:
	~ioready_dispatcher_poll() noexcept override;
	ioready_dispatcher_poll() /*throw(std::bad_alloc, std::runtime_error)*/;

	size_t
	dispatch(
		const std::chrono::steady_clock::duration * timeout,
		std::size_t limit) override;

	size_t
	dispatch_pending(
		std::size_t limit) override;

	eventtrigger &
	get_eventtrigger() noexcept override;

	ioready_connection
	watch(
		std::function<void(tscb::ioready_events)> function,
		int fd, tscb::ioready_events event_mask) /* throw(std::bad_alloc) */
		override;

private:
	class link_type;

	class polltab_index_alloc_guard;

	class polltab_index_entry {
	public:
		std::size_t use_count = 0;
		std::size_t index;
	};

	/** \brief \internal Reserve index in poll table for given fd

		\returns whether a new entry was allocated (as opposed t
		Must be called with polltab_mutex_ held.
	 */
	std::size_t
	allocate_polltab_entry(std::size_t fd) /* throw(std::bad_alloc) */;

	/** \brief \internal Update polltab entry for given fd

		Must be called with polltab_mutex_ held.
	 */
	void
	update_polltab_entry(std::size_t fd, ioready_events mask) noexcept;

	/** \brief \internal Free polltab entry for given fd

		Must be called with polltab_mutex_ held.
	 */
	void
	free_polltab_entry(std::size_t fd) noexcept;

	std::pair<std::unique_ptr<struct pollfd[]>, std::size_t>
	get_polltab();

	std::size_t
	handle_events(struct pollfd * ptab, std::size_t ptab_size, std::size_t limit, uint32_t cookie);

	void
	synchronize() noexcept;

	static ioready_events
	translate_os_to_tscb(int ev) noexcept;

	static int
	translate_tscb_to_os(ioready_events ev) noexcept;

	std::mutex polltab_mutex_;
	std::vector<struct pollfd> polltab_;
	std::vector<polltab_index_entry> polltab_index_;
	fd_handler_table fdtab_;

	deferrable_rwlock lock_;
	friend class read_guard<ioready_dispatcher_poll>;
	friend class async_write_guard<ioready_dispatcher_poll>;

	pipe_eventflag wakeup_flag_;

	ioready_connection pipe_callback_;
};

}

#endif
