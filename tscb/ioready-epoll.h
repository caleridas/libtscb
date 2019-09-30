/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#ifndef TSCB_IOREADY_EPOLL_H
#define TSCB_IOREADY_EPOLL_H

#include <vector>

#include <sys/epoll.h>

#include <tscb/ioready.h>
#include <tscb/deferred.h>
#include <tscb/fd-handler-table.h>

namespace tscb {

/**
	\brief Dispatcher for IO readiness events using the
	<TT>epoll_*</TT> system calls

	This class supports collecting the IO readiness state of
	a set of file descriptors using the <TT>epoll_*</TT> family of system
	calls, and dispatching callbacks to receivers that have
	registered themselves for events on specific file descriptors.

	The <TT>epoll_*</TT> family of system calls provide the fastest
	possible way to observe the state of a set of file descriptors
	on Linux systems. Like
	\ref tscb::ioready_dispatcher_kqueue "ioready_dispatcher_kqueue"
	all relevant operations are O(1), i.e. independent from
	the number of descriptors being watched.

	Moreover, the \ref dispatch method can usefully be called from
	multiple threads.
*/
class ioready_dispatcher_epoll : public ioready_dispatcher {
public:
	~ioready_dispatcher_epoll() noexcept;

	ioready_dispatcher_epoll() /*throw(std::bad_alloc, std::runtime_error)*/;

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


protected:
	class link_type;

	void
	process_events(const epoll_event events[], std::size_t nevents, uint32_t cookie);

	void
	synchronize() noexcept;

	int epoll_fd_;
	fd_handler_table fdtab_;
	pipe_eventflag wakeup_flag_;
	deferrable_rwlock lock_;

	friend class read_guard<ioready_dispatcher_epoll>;
	friend class async_write_guard<ioready_dispatcher_epoll>;
};

}

#endif
