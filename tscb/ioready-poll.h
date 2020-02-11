/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_IOREADY_POLL_H
#define TSCB_IOREADY_POLL_H

#include <vector>

#include <sys/poll.h>

#include <tscb/detail/eventflag.h>
#include <tscb/ioready.h>
#include <tscb/detail/deferred-locks.h>
#include <tscb/detail/fd-handler-table.h>

namespace tscb {

class ioready_dispatcher_poll final : public ioready_dispatcher {
public:
	~ioready_dispatcher_poll() noexcept override;
	ioready_dispatcher_poll();

	size_t
	dispatch(
		const std::chrono::steady_clock::duration * timeout,
		std::size_t limit) override;

	size_t
	dispatch_pending(
		std::size_t limit) override;

	virtual void
	wake_up() noexcept override;

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

	detail::fd_handler_table fdtab_;
	detail::deferrable_rwlock lock_;
	detail::pipe_eventflag wakeup_flag_;
};

}

#endif
