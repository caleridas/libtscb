/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/ioready-select.h>


namespace tscb {

class ioready_dispatcher_select::link_type final : public fd_handler_table::link_type {
public:
	using pointer = boost::intrusive_ptr<link_type>;

	link_type(
		ioready_dispatcher_select * master,
		std::function<void(ioready_events)> fn,
		int fd,
		ioready_events event_mask) noexcept
		: fd_handler_table::link_type(std::move(fn), fd, event_mask)
		, master_(master)
	{
	}

	~link_type() noexcept override
	{
	}

	void
	disconnect() noexcept override
	{
		std::unique_lock<std::mutex> rguard(registration_mutex_);

		ioready_dispatcher_select * master = master_.load(std::memory_order_relaxed);

		if (master) {
			async_write_guard<ioready_dispatcher_select> guard(*master);
			ioready_events old_mask, new_mask;
			master->fdtab_.remove(this, old_mask, new_mask);
			{
				if (old_mask != new_mask) {
					master->update_fdsets(fd(), new_mask);
				}
				master->wakeup_flag_.set();
			}
			master_.store(nullptr, std::memory_order_relaxed);
			rguard.unlock();
		}
	}

	bool
	is_connected() const noexcept override
	{
		return master_.load(std::memory_order_relaxed) != nullptr;
	}

	void
	modify(ioready_events new_event_mask) noexcept
	{
		std::unique_lock<std::mutex> guard(registration_mutex_);

		ioready_dispatcher_select * master = master_.load(std::memory_order_relaxed);

		if (master) {
			async_write_guard<ioready_dispatcher_select> guard(*master);
			ioready_events old_mask, new_mask;
			master->fdtab_.modify(this, new_event_mask, old_mask, new_mask);
			{
				if (old_mask != new_mask) {
					master->update_fdsets(fd(), new_mask);
				}
				master->wakeup_flag_.set();
			}
		}
	}

private:
	std::mutex registration_mutex_;
	std::atomic<ioready_dispatcher_select *> master_;
};

ioready_dispatcher_select::ioready_dispatcher_select()
	/* throw(std::bad_alloc, std::runtime_error) */
	: maxfd_(0)
{
	FD_ZERO(&readfds_);
	FD_ZERO(&writefds_);
	FD_ZERO(&exceptfds_);

	watch(
		[this](ioready_events) {},
		wakeup_flag_.readfd_, ioready_input);
}

ioready_dispatcher_select::~ioready_dispatcher_select() noexcept
{
	/* we can assume

	- no thread is actively dispatching at the moment
	- no user can register new callbacks at the moment

	if those conditions are not met, we are in big trouble anyway, and
	there is no point doing anything about it
	*/

	while(lock_.read_lock()) {
		synchronize();
	}
	bool any_disconnected = fdtab_.disconnect_all();
	if (lock_.read_unlock()) {
		/* the above cancel operations will cause synchronization
		to be performed at the next possible point in time; if
		there is no concurrent cancellation, this is now */
		synchronize();
	} else if (any_disconnected) {
		/* this can only happen if some callback link was
		cancelled while this object is being destroyed; in
		that case we have to suspend the thread that is destroying
		the object until we are certain that synchronization has
		been performed */

		lock_.write_lock_sync();
		synchronize();

		/* note that synchronize implicitly calls sync_finished,
		which is equivalent to write_unlock_sync for deferrable_rwlocks */
	}
}

eventtrigger &
ioready_dispatcher_select::get_eventtrigger() noexcept
{
	return wakeup_flag_;
}

std::size_t
ioready_dispatcher_select::handle_events(
	const fd_set & readfds, const fd_set & writefds, const fd_set & exceptfds,
	int maxfd,
	std::size_t limit,
	uint32_t cookie)
{
	read_guard<ioready_dispatcher_select> guard(*this);

	std::size_t count = 0;
	for (int fd = 0; fd < maxfd; ++fd) {
		if (count >= limit) {
			break;
		}
		int r = FD_ISSET(fd, &readfds);
		int w = FD_ISSET(fd, &writefds);
		int e = FD_ISSET(fd, &exceptfds);
		if (r || w || e) {
			ioready_events ev = ioready_none;
			if (r) {
				ev = ioready_input;
			}
			if (w) {
				ev |= ioready_output;
			}
			/* deliver exception events to everyone */
			if (e) {
				ev |= ioready_error|ioready_input|ioready_output;
			}

			fdtab_.notify(fd, ev, cookie);
			++count;
		}
	}

	return count;
}

std::size_t
ioready_dispatcher_select::dispatch(
	const std::chrono::steady_clock::duration *timeout,
	std::size_t limit)
{
	read_guard<ioready_dispatcher_select> guard(*this);

	uint32_t cookie = fdtab_.cookie();

	fd_set l_readfds, l_writefds, l_exceptfds;
	int l_maxfd;
	{
		std::lock_guard<std::mutex> guard(fdset_mutex_);
		l_readfds = readfds_;
		l_writefds = writefds_;
		l_exceptfds = exceptfds_;
		l_maxfd = maxfd_;
	}

	struct timeval tv, * select_timeout;
	if (timeout) {
		uint64_t usecs = std::chrono::duration_cast<std::chrono::microseconds>(
			(*timeout) + std::chrono::microseconds(1) - std::chrono::steady_clock::duration(1)).count();
		tv.tv_sec = usecs/1000000;
		tv.tv_usec = usecs%1000000;
		select_timeout = &tv;
	} else {
		select_timeout = nullptr;
	}

	wakeup_flag_.start_waiting();

	if (wakeup_flag_.flagged_.load(std::memory_order_relaxed) != 0) {
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		select_timeout = &tv;
	}

	int count = select(l_maxfd, &l_readfds, &l_writefds, &l_exceptfds, select_timeout);

	wakeup_flag_.stop_waiting();

	std::size_t handled = 0;
	if (count > 0) {
		handled = handle_events(l_readfds, l_writefds, l_exceptfds, l_maxfd, std::min(limit, static_cast<std::size_t>(count)), cookie);
	}

	wakeup_flag_.clear();

	return handled;
}

std::size_t
ioready_dispatcher_select::dispatch_pending(
	std::size_t limit)
{
	read_guard<ioready_dispatcher_select> guard(*this);

	uint32_t cookie = fdtab_.cookie();

	fd_set l_readfds, l_writefds, l_exceptfds;
	int l_maxfd;
	{
		std::lock_guard<std::mutex> guard(fdset_mutex_);
		l_readfds = readfds_;
		l_writefds = writefds_;
		l_exceptfds = exceptfds_;
		l_maxfd = maxfd_;
	}

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	int count = select(l_maxfd, &l_readfds, &l_writefds, &l_exceptfds, &tv);

	std::size_t handled = 0;
	if (count > 0) {
		handled = handle_events(l_readfds, l_writefds, l_exceptfds, l_maxfd, std::min(limit, static_cast<std::size_t>(count)), cookie);
	}

	wakeup_flag_.clear();

	return handled;
}

ioready_connection
ioready_dispatcher_select::watch(
	std::function<void(tscb::ioready_events)> function,
	int fd, tscb::ioready_events event_mask) /* throw(std::bad_alloc) */
{
	link_type::pointer link(new link_type(this, std::move(function), fd, event_mask));
	{
		async_write_guard<ioready_dispatcher_select> guard(*this);

		ioready_events old_mask, new_mask;
		fdtab_.insert(link.get(), old_mask, new_mask);
		if (old_mask != new_mask) {
			update_fdsets(fd, new_mask);
		}
	}

	wakeup_flag_.set();

	return ioready_connection(std::move(link));
}

void
ioready_dispatcher_select::update_fdsets(int fd, ioready_events mask) noexcept
{
	std::lock_guard<std::mutex> guard(fdset_mutex_);

	if ((mask & ioready_input) != 0) {
		FD_SET(fd, &readfds_);
	} else {
		FD_CLR(fd, &readfds_);
	}
	if ((mask & ioready_output) != 0) {
		FD_SET(fd, &writefds_);
	} else {
		FD_CLR(fd, &writefds_);
	}
	if (mask != 0) {
		FD_SET(fd, &exceptfds_);
	} else {
		FD_CLR(fd, &exceptfds_);
	}

	if (mask) {
		if (fd >= maxfd_) maxfd_ = fd + 1;
	} else if (fd == maxfd_ - 1) {
		for (;;) {
			maxfd_--;
			if (!maxfd_ ||
				FD_ISSET(maxfd_ - 1, &readfds_) ||
				FD_ISSET(maxfd_ - 1, &writefds_) ||
				FD_ISSET(maxfd_ - 1, &exceptfds_)) {
				break;
			}
		}
	}
}

void
ioready_dispatcher_select::synchronize() noexcept
{
	fd_handler_table::delayed_handler_release rel = fdtab_.synchronize();

	lock_.sync_finished();

	rel.clear();
}

ioready_dispatcher *
create_ioready_dispatcher_select() /* throw(std::bad_alloc, std::runtime_error) */
{
	return new ioready_dispatcher_select();
}

}
