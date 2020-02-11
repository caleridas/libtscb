/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/ioready-poll.h>

#include <sys/fcntl.h>
#include <string.h>

#include <memory>

namespace tscb {

/**
	\class ioready_dispatcher_poll
	\brief
		Dispatcher for IO readiness events using the
		\p poll system call
	\headerfile tscb/ioready-poll.h <tscb/ioready-poll.h>

	This class supports collecting the IO readiness state of
	a set of file descriptors using the <TT>poll</TT> system
	call, and dispatching callbacks to receivers that have
	registered themselves for events on specific file descriptors.

	The <TT>poll</TT> system call usually performs considerably
	better than <TT>select</TT>, though it has the same
	asymptotic behaviour (and is thus not very well-suited for
	watching large numbers of mostly idle descriptors).
*/


/* exclude private nested class from doxygen */
/** \cond false */

class ioready_dispatcher_poll::link_type final
	: public detail::fd_handler_table::link_type {
public:
	using pointer = detail::intrusive_ptr<link_type>;

	using read_guard = detail::read_guard<
		ioready_dispatcher_poll,
		&ioready_dispatcher_poll::lock_,
		&ioready_dispatcher_poll::synchronize
	>;

	using write_guard = detail::async_write_guard<
		ioready_dispatcher_poll,
		&ioready_dispatcher_poll::lock_,
		&ioready_dispatcher_poll::synchronize
	>;

	link_type(
		ioready_dispatcher_poll * master,
		std::function<void(ioready_events)> fn,
		int fd,
		ioready_events event_mask) noexcept
		: detail::fd_handler_table::link_type(std::move(fn), fd, event_mask)
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

		ioready_dispatcher_poll * master = master_.load(std::memory_order_relaxed);

		if (master) {
			write_guard guard(*master);
			ioready_events old_mask, new_mask;
			master->fdtab_.remove(this, old_mask, new_mask);
			{
				std::lock_guard<std::mutex> pguard(master->polltab_mutex_);
				master->update_polltab_entry(fd(), new_mask);
				master->free_polltab_entry(fd());
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

		ioready_dispatcher_poll * master = master_.load(std::memory_order_relaxed);

		if (master) {
			write_guard guard(*master);
			ioready_events old_mask, new_mask;
			master->fdtab_.modify(this, new_event_mask, old_mask, new_mask);
			{
				std::lock_guard<std::mutex> pguard(master->polltab_mutex_);
				master->update_polltab_entry(fd(), new_mask);
				master->wakeup_flag_.set();
			}
		}
	}

private:
	std::mutex registration_mutex_;
	std::atomic<ioready_dispatcher_poll *> master_;
};

class ioready_dispatcher_poll::polltab_index_alloc_guard {
public:
	polltab_index_alloc_guard(
		ioready_dispatcher_poll & master,
		int fd)
		: master_(master), fd_(fd), guard_(master_.polltab_mutex_)
	{
		index_ = master_.allocate_polltab_entry(fd_);
	}

	~polltab_index_alloc_guard() noexcept
	{
		if (!committed_) {
			master_.free_polltab_entry(fd_);
		}
	}

	void
	commit(ioready_events new_event_mask)
	{
		master_.polltab_[index_].events = translate_tscb_to_os(new_event_mask);
		committed_ = true;
	}

private:
	ioready_dispatcher_poll & master_;
	int fd_;
	std::lock_guard<std::mutex> guard_;
	std::size_t index_;
	bool committed_ = true;
};

/** \endcond */

ioready_events
ioready_dispatcher_poll::translate_os_to_tscb(int ev) noexcept
{
	ioready_events e = ioready_none;
	if (ev & POLLIN) {
		e |= ioready_input;
	}
	if (ev & POLLOUT) {
		e |= ioready_output;
	}
	/* deliver hangup event to input and output handlers as well */
	if (ev & POLLHUP) {
		e |= (ioready_input|ioready_output|ioready_hangup|ioready_error);
	}
	if (ev & POLLERR) {
		e |= (ioready_input|ioready_output|ioready_error);
	}
	return e;
}

int
ioready_dispatcher_poll::translate_tscb_to_os(ioready_events ev) noexcept
{
	int e = 0;
	if (ev & ioready_input) {
		e |= POLLIN;
	}
	if (ev & ioready_output) {
		e |= POLLOUT;
	}
	return e;
}

/* ioready_dispatcher_poll */

ioready_dispatcher_poll::ioready_dispatcher_poll()
{
	watch(
		[this](ioready_events) {},
		wakeup_flag_.readfd(), ioready_input);
}

ioready_dispatcher_poll::~ioready_dispatcher_poll() noexcept
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

void
ioready_dispatcher_poll::wake_up() noexcept
{
	wakeup_flag_.set();
}

size_t
ioready_dispatcher_poll::dispatch(const std::chrono::steady_clock::duration * timeout, std::size_t limit)
{
	link_type::read_guard guard(*this);

	uint32_t cookie = fdtab_.cookie();
	auto ptab = get_polltab();

	/* need to round up timeout */
	int poll_timeout;
	if (timeout) {
		poll_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
			(*timeout) + std::chrono::milliseconds(1) - std::chrono::steady_clock::duration(1)).count();
	} else {
		poll_timeout = -1;
	}

	wakeup_flag_.start_waiting();

	if (wakeup_flag_.flagged()) {
		poll_timeout = 0;
	}

	ssize_t count = ::poll(ptab.first.get(), ptab.second, poll_timeout);

	wakeup_flag_.stop_waiting();

	std::size_t handled = 0;
	if (count > 0) {
		handled = handle_events(ptab.first.get(), ptab.second, std::min(limit, static_cast<std::size_t>(count)), cookie);
	}

	wakeup_flag_.clear();

	return handled;
}

size_t
ioready_dispatcher_poll::dispatch_pending(std::size_t limit)
{
	link_type::read_guard guard(*this);

	uint32_t cookie = fdtab_.cookie();
	auto ptab = get_polltab();

	ssize_t count = ::poll(ptab.first.get(), ptab.second, 0);

	std::size_t handled = 0;
	if (count > 0) {
		handled = handle_events(ptab.first.get(), ptab.second, std::min(limit, static_cast<std::size_t>(count)), cookie);
	}

	return handled;
}

ioready_connection
ioready_dispatcher_poll::watch(
	std::function<void(tscb::ioready_events)> function,
	int fd, tscb::ioready_events event_mask) /* throw(std::bad_alloc) */
{
	link_type::pointer link(new link_type(this, std::move(function), fd, event_mask));
	{
		link_type::write_guard wguard(*this);
		polltab_index_alloc_guard pguard(*this, fd);

		ioready_events old_mask, new_mask;
		fdtab_.insert(link.get(), old_mask, new_mask);

		pguard.commit(new_mask);
	}

	wakeup_flag_.set();

	return ioready_connection(std::move(link));
}

void
ioready_dispatcher_poll::synchronize() noexcept
{
	detail::fd_handler_table::delayed_handler_release rel = fdtab_.synchronize();

	lock_.sync_finished();

	rel.clear();
}

std::size_t
ioready_dispatcher_poll::allocate_polltab_entry(std::size_t fd) /* throw(std::bad_alloc) */
{
	if (fd >= polltab_index_.size()) {
		polltab_index_.resize(fd + 1);
	}

	if (polltab_index_[fd].use_count == 0) {
		std::size_t index = polltab_.size();
		struct pollfd pfd;
		pfd.fd = fd;
		pfd.events = 0;
		pfd.revents = 0;
		polltab_.push_back(pfd);
		polltab_index_[fd].index = index;
		polltab_index_[fd].use_count = 1;
		return index;
	} else {
		polltab_index_[fd].use_count += 1;
		return polltab_index_[fd].index;
	}
}

void
ioready_dispatcher_poll::update_polltab_entry(std::size_t fd, ioready_events mask) noexcept
{
	std::size_t index = polltab_index_[fd].index;
	polltab_[index].events = translate_tscb_to_os(mask);
}

void
ioready_dispatcher_poll::free_polltab_entry(std::size_t fd) noexcept
{
	polltab_index_[fd].use_count -= 1;
	if (polltab_index_[fd].use_count == 0) {
		std::size_t other_index = polltab_.size() - 1;
		std::size_t index = polltab_index_[fd].index;
		if (other_index != index) {
			std::size_t other_fd = polltab_[other_index].fd;

			polltab_[index] = polltab_[other_index];
			polltab_index_[other_fd].index = index;
		}
		polltab_.resize(polltab_.size() - 1);
	}
}

std::pair<std::unique_ptr<struct pollfd[]>, std::size_t>
ioready_dispatcher_poll::get_polltab()
{
	std::pair<std::unique_ptr<struct pollfd[]>, std::size_t> result;
	{
		std::lock_guard<std::mutex> guard(polltab_mutex_);
		result.second = polltab_.size();
		result.first.reset(new struct pollfd[result.second]);
		for (std::size_t n = 0; n < result.second; ++n) {
			result.first[n] = polltab_[n];
		}
	}

	return result;
}

std::size_t
ioready_dispatcher_poll::handle_events(struct pollfd * ptab, std::size_t ptab_size, std::size_t limit, uint32_t cookie)
{
	std::size_t count = 0;
	for (std::size_t n = 0; n < ptab_size; ++n) {
		if (count >= limit) {
			break;
		}
		if (ptab[n].revents) {
			int fd = ptab[n].fd;
			ioready_events ev = translate_os_to_tscb(ptab[n].revents);
			fdtab_.notify(fd, ev, cookie);
			++count;
		}
	}

	return count;
}

/** \cond false */

ioready_dispatcher *
create_ioready_dispatcher_poll() /* throw(std::bad_alloc, std::runtime_error) */
{
	return new ioready_dispatcher_poll();
}
/** \endcond */

}
