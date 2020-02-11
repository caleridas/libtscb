/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/ioready-epoll.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/fcntl.h>
#include <sys/epoll.h>

#include <tscb/config.h>

namespace tscb {

/**
	\class ioready_dispatcher_epoll
	\headerfile tscb/ioready-epoll.h <tscb/ioready-epoll.h>
	\brief Dispatcher for IO readiness events using the
	\p epoll_* family of system calls

	This class supports collecting the IO readiness state of a set of
	file descriptors using the \p epoll_* family of system calls, and
	dispatching callbacks to receivers that have registered themselves
	for events on specific file descriptors.

	The \p epoll_* family of system calls provide the fastest possible
	way to observe the state of a set of file descriptors on Linux
	systems. Like \ref tscb::ioready_dispatcher_kqueue
	"ioready_dispatcher_kqueue" all relevant operations are O(1), i.e.
	independent from the number of descriptors being watched.

	The \ref dispatch method can usefully be called from multiple: It
	will result in separate events to be dispatched in parallel.
*/

namespace {

ioready_events
translate_os_to_tscb(int ev) noexcept
{
	ioready_events e = ioready_none;
	if ((ev & EPOLLIN) != 0) {
		e |= ioready_input;
	}
	if ((ev & EPOLLOUT) !=0) {
		e |= ioready_output;
	}
	/* deliver hangup event to input and output handlers as well */
	if ((ev & EPOLLHUP) != 0) {
		e |= ioready_input | ioready_output | ioready_hangup | ioready_error;
	}
	if ((ev & EPOLLERR) != 0) {
		e |= ioready_input | ioready_output | ioready_error;
	}
	return e;
}

int
translate_tscb_to_os(ioready_events ev) noexcept
{
	int e = 0;
	if ((ev & ioready_input) != 0) {
		e |= EPOLLIN;
	}
	if ((ev & ioready_output) != 0) {
		e |= EPOLLOUT;
	}
	return e;
}

}

/* exclude private nested class from doxygen */
/** \cond false */

class ioready_dispatcher_epoll::link_type final
	: public detail::fd_handler_table::link_type {
public:
	using pointer = detail::intrusive_ptr<link_type>;

	using read_guard = detail::read_guard<
		ioready_dispatcher_epoll,
		&ioready_dispatcher_epoll::lock_,
		&ioready_dispatcher_epoll::synchronize
	>;

	using write_guard = detail::async_write_guard<
		ioready_dispatcher_epoll,
		&ioready_dispatcher_epoll::lock_,
		&ioready_dispatcher_epoll::synchronize
	>;

	link_type(
		ioready_dispatcher_epoll * master,
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

		ioready_dispatcher_epoll * master = master_.load(std::memory_order_relaxed);

		if (master) {
			write_guard guard(*master);
			ioready_events old_mask, new_mask;
			master->fdtab_.remove(this, old_mask, new_mask);
			if (old_mask && old_mask != new_mask) {
				epoll_event event;
				event.data.u64 = 0;
				event.data.fd = fd();
				int op;
				if (new_mask) {
					event.events = translate_tscb_to_os(new_mask);
					op = EPOLL_CTL_MOD;
				} else {
					event.events = translate_tscb_to_os(old_mask);
					op = EPOLL_CTL_DEL;
				}
				::epoll_ctl(master->epoll_fd_, op, fd(), &event);
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

		ioready_dispatcher_epoll * master = master_.load(std::memory_order_relaxed);

		if (master) {
			write_guard guard(*master);
			ioready_events old_mask, new_mask;
			master->fdtab_.modify(this, new_event_mask, old_mask, new_mask);
			if (old_mask != new_mask) {
				epoll_event event;
				event.data.u64 = 0;
				event.data.fd = fd();
				int op;

				if (old_mask) {
					if (new_mask) {
						event.events = translate_tscb_to_os(new_mask);
						op = EPOLL_CTL_MOD;
					} else {
						event.events = translate_tscb_to_os(old_mask);
						op = EPOLL_CTL_DEL;
					}
					::epoll_ctl(master->epoll_fd_, op, fd(), &event);
				} else {
					if (new_mask) {
						event.events = translate_tscb_to_os(new_mask);
						op = EPOLL_CTL_ADD;
						::epoll_ctl(master->epoll_fd_, op, fd(), &event);
					}
				}
			}
		}
	}

private:
	std::mutex registration_mutex_;
	std::atomic<ioready_dispatcher_epoll *> master_;
};

/** \endcond */

ioready_dispatcher_epoll::~ioready_dispatcher_epoll() noexcept
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

	::close(epoll_fd_);
}

ioready_dispatcher_epoll::ioready_dispatcher_epoll()
{
#if defined(HAVE_EPOLL1) && defined(EPOLL_CLOEXEC)
	epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
#else
	epoll_fd_ = ::epoll_create(1024);
	::fcntl(epoll_fd_, F_SETFL, O_CLOEXEC);
#endif
	if (epoll_fd_ < 0) {
		throw std::runtime_error("Unable to create epoll descriptor");
	}

	watch([this](ioready_events){}, wakeup_flag_.readfd(), ioready_input);
}

void
ioready_dispatcher_epoll::process_events(const epoll_event events[], std::size_t nevents, uint32_t cookie)
{
	link_type::read_guard guard(*this);

	for (std::size_t n = 0; n < nevents; ++n) {
		int fd = events[n].data.fd;
		ioready_events ev = translate_os_to_tscb(events[n].events);

		fdtab_.notify(fd, ev, cookie);
	}
}

size_t
ioready_dispatcher_epoll::dispatch(const std::chrono::steady_clock::duration * timeout, std::size_t limit)
{
	uint32_t cookie = fdtab_.cookie();

	int poll_timeout;
	/* need to round up timeout; alas this is the only good way to do it in boost */
	if (timeout) {
		poll_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
			(*timeout) + std::chrono::milliseconds(1) - std::chrono::steady_clock::duration(1)).count();
	} else {
		poll_timeout = -1;
	}

	if (limit > 16) {
		limit = 16;
	}
	epoll_event events[16];

	ssize_t nevents;

	wakeup_flag_.start_waiting();
	if (wakeup_flag_.flagged()) {
		poll_timeout = 0;
	}
	nevents = ::epoll_wait(epoll_fd_, events, limit, poll_timeout);
	wakeup_flag_.stop_waiting();

	if (nevents > 0) {
		process_events(events, nevents, cookie);
	} else {
		nevents = 0;
	}

	wakeup_flag_.clear();

	return nevents;
}

std::size_t
ioready_dispatcher_epoll::dispatch_pending(std::size_t limit)
{
	uint32_t cookie = fdtab_.cookie();

	if (limit > 16) {
		limit = 16;
	}
	epoll_event events[16];

	ssize_t nevents = epoll_wait(epoll_fd_, events, limit, 0);

	if (nevents > 0) {
		process_events(events, nevents, cookie);
	} else {
		nevents = 0;
	}

	wakeup_flag_.clear();

	return nevents;
}

void
ioready_dispatcher_epoll::wake_up() noexcept
{
	wakeup_flag_.set();
}

void
ioready_dispatcher_epoll::synchronize() noexcept
{
	detail::fd_handler_table::delayed_handler_release rel = fdtab_.synchronize();

	lock_.sync_finished();

	rel.clear();
}

ioready_connection
ioready_dispatcher_epoll::watch(
	std::function<void(tscb::ioready_events)> function,
	int fd, tscb::ioready_events event_mask)
{
	link_type::pointer link(new link_type(this, std::move(function), fd, event_mask));
	ioready_events old_mask, new_mask;

	fdtab_.insert(link.get(), old_mask, new_mask);
	if (new_mask != ioready_none && old_mask != new_mask) {
		epoll_event event;
		event.events = translate_tscb_to_os(new_mask);
		event.data.u64 = 0;
		event.data.fd = link->fd();

		if (old_mask) {
			::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, link->fd(), &event);
		} else {
			::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, link->fd(), &event);
		}
	}

	return ioready_connection(std::move(link));
}

/** \cond false */

ioready_dispatcher *
create_ioready_dispatcher_epoll()
{
	return new ioready_dispatcher_epoll();
}

/** \endcond */

}
