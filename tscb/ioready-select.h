/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#ifndef TSCB_IOREADY_SELECT_H
#define TSCB_IOREADY_SELECT_H

#include <sys/select.h>

#include <mutex>
#include <vector>

#include <tscb/ioready.h>
#include <tscb/deferred.h>
#include <tscb/fd-handler-table.h>

namespace tscb {

/**
	\brief Dispatcher for IO readiness events using the
	<TT>select</TT> system call

	This class supports collecting the IO readiness state of
	a set of file descriptors using the <TT>select</TT> system
	call, and dispatching callbacks to receivers that have
	registered themselves for events on specific file descriptors.

	<TT>select</TT> is the most portable system call to determine
	the IO readiness state of a set of descriptors, but also
	by far the slowest. It has a hard (compile-time) limitation
	on the number of permissible descriptors, and is O(n) in
	the number of descriptors watched.

	Use of this dispatcher should be avoided if possible, choose
	one of the better performing alternatives instead and fall
	back to \ref tscb::ioready_dispatcher_select "ioready_dispatcher_select"
	only if nothing else is available.
*/
class ioready_dispatcher_select : public ioready_dispatcher {
public:
	~ioready_dispatcher_select() noexcept override;

	ioready_dispatcher_select() /*throw(std::bad_alloc, std::runtime_error)*/;

	std::size_t
	dispatch(
		const std::chrono::steady_clock::duration * timeout,
		std::size_t limit) override;

	std::size_t
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

	std::mutex fdset_mutex_;
	fd_set readfds_, writefds_, exceptfds_;
	int maxfd_;

	std::size_t
	handle_events(
		const fd_set & readfds, const fd_set & writefds, const fd_set & exceptfds,
		int maxfd,
		std::size_t limit,
		uint32_t cookie);

	void
	synchronize() noexcept;

	void
	update_fdsets(int fd, ioready_events mask) noexcept;

	fd_handler_table fdtab_;

	deferrable_rwlock lock_;
	pipe_eventflag wakeup_flag_;

	friend class read_guard<ioready_dispatcher_select>;
	friend class async_write_guard<ioready_dispatcher_select>;
};

}

#endif
