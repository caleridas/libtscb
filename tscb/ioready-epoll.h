/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_IOREADY_EPOLL_H
#define TSCB_IOREADY_EPOLL_H

#include <sys/epoll.h>

#include <tscb/detail/eventflag.h>
#include <tscb/ioready.h>
#include <tscb/detail/deferred-locks.h>
#include <tscb/detail/fd-handler-table.h>

namespace tscb {

class ioready_dispatcher_epoll final : public ioready_dispatcher {
public:
	~ioready_dispatcher_epoll() noexcept;

	ioready_dispatcher_epoll();

	std::size_t
	dispatch(
		const std::chrono::steady_clock::duration * timeout,
		std::size_t limit) override;

	std::size_t
	dispatch_pending(
		std::size_t limit) override;

	virtual void
	wake_up() noexcept override;

	ioready_connection
	watch(
		std::function<void(tscb::ioready_events)> function,
		int fd, tscb::ioready_events event_mask)
		override;

private:
	class link_type;

	void
	process_events(const epoll_event events[], std::size_t nevents, uint32_t cookie);

	void
	synchronize() noexcept;

	int epoll_fd_;
	detail::fd_handler_table fdtab_;
	detail::pipe_eventflag wakeup_flag_;
	detail::deferrable_rwlock lock_;
};

}

#endif
