/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_IOREADY_SELECT_H
#define TSCB_IOREADY_SELECT_H

#include <sys/select.h>

#include <mutex>
#include <vector>

#include <tscb/detail/eventflag.h>
#include <tscb/ioready.h>
#include <tscb/detail/deferred-locks.h>
#include <tscb/detail/fd-handler-table.h>

namespace tscb {

class ioready_dispatcher_select : public ioready_dispatcher {
public:
	~ioready_dispatcher_select() noexcept override;

	ioready_dispatcher_select();

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

	detail::fd_handler_table fdtab_;
	detail::deferrable_rwlock lock_;
	detail::pipe_eventflag wakeup_flag_;
};

}

#endif
