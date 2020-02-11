/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/ioready-kqueue.h>

#include <unistd.h>

namespace tscb {

/**
	\class ioready_dispatcher_kqueue
	\headerfile tscb/ioready-kqueue.h <tscb/ioready-kqueue.h>
	\brief Dispatcher for IO readiness events using the
	\p kqueue system call mechanism.

	This class supports collecting the IO readiness state of a set of
	file descriptors using the \p kevent system call, and dispatching
	callbacks to receivers that have registered themselves for events
	on specific file descriptors.

	The \p kevent system call provides the fastest possible way to
	observe the state of a set of file descriptors on BSD-derived
	systems; additionally other event notifications can be routed
	through \p kqueue< as well. Like \ref
	tscb::ioready_dispatcher_epoll "ioready_dispatcher_epoll" all
	relevant operations are O(1), that is: independent from the number
	of descriptors being watched.

	The \ref dispatch method can usefully be called by multiple
	threads: It will result in separate events to be dispatched in
	parallel.
*/

/* exclude private nested class from doxygen */
/** \cond false */

class ioready_dispatcher_kqueue::link_type final
	: public detail::fd_handler_table::link_type {
public:
	using pointer = detail::intrusive_ptr<link_type>;

	using read_guard = detail::read_guard<
		ioready_dispatcher_kqueue,
		&ioready_dispatcher_kqueue::lock_,
		&ioready_dispatcher_kqueue::synchronize
	>;

	using write_guard = detail::async_write_guard<
		ioready_dispatcher_kqueue,
		&ioready_dispatcher_kqueue::lock_,
		&ioready_dispatcher_kqueue::synchronize
	>;

	link_type(
		ioready_dispatcher_kqueue * master,
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

		ioready_dispatcher_kqueue * master = master_.load(std::memory_order_relaxed);

		if (master) {
			write_guard guard(*master);
			ioready_events old_mask, new_mask;
			master->fdtab_.remove(this, old_mask, new_mask);
			master->update_evmask(fd(), old_mask, new_mask);

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

		ioready_dispatcher_kqueue * master = master_.load(std::memory_order_relaxed);

		if (master) {
			write_guard guard(*master);
			ioready_events old_mask, new_mask;
			master->fdtab_.modify(this, new_event_mask, old_mask, new_mask);
			master->update_evmask(fd(), old_mask, new_mask);
		}
	}

private:
	std::mutex registration_mutex_;
	std::atomic<ioready_dispatcher_kqueue *> master_;
};

/** \endcond */

ioready_dispatcher_kqueue::~ioready_dispatcher_kqueue() noexcept
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

	::close(kqueue_fd_);
}

ioready_dispatcher_kqueue::ioready_dispatcher_kqueue()
{
	kqueue_fd_ = ::kqueue();
	if (kqueue_fd_ < 0) {
		throw std::runtime_error("Unable to create kqueue descriptor");
	}
	watch([this](ioready_events){}, wakeup_flag_.readfd(), ioready_input);
}

void ioready_dispatcher_kqueue::process_events(
	const struct kevent events[], size_t nevents,
	uint32_t cookie)
{
	link_type::read_guard guard(*this);

	for (std::size_t n = 0; n < nevents; ++n) {
		int fd = events[n].ident;
		ioready_events ev;
		if (events[n].filter == EVFILT_READ) {
			ev = ioready_input;
		} else if (events[n].filter == EVFILT_WRITE) {
			ev = ioready_output;
		} else {
			ev = ioready_none;
		}

		fdtab_.notify(fd, ev, cookie);
	}
}

size_t ioready_dispatcher_kqueue::dispatch(
	const std::chrono::steady_clock::duration * timeout, std::size_t limit)
{
	uint32_t cookie = fdtab_.cookie();

	struct timespec ts;
	struct timespec* kevent_timeout = nullptr;
	if (timeout) {
		uint64_t nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(
			(*timeout) + std::chrono::nanoseconds(1) - std::chrono::steady_clock::duration(1)).count();
		ts.tv_sec = nsecs / 1000000000;
		ts.tv_nsec = nsecs % 1000000000;
		kevent_timeout = &ts;
	}

	if (limit > 16) {
		limit = 16;
	}
	struct kevent events[16];

	wakeup_flag_.start_waiting();
	if (wakeup_flag_.flagged()) {
		kevent_timeout = nullptr;
	}
	ssize_t nevents = ::kevent(
		kqueue_fd_,
		/* modlist */ nullptr, /* modcount */ 0,
		events, limit,
		kevent_timeout);
	wakeup_flag_.stop_waiting();

	if (nevents > 0) {
		process_events(events, nevents, cookie);
	} else {
		nevents = 0;
	}

	wakeup_flag_.clear();

	return nevents;
}

size_t ioready_dispatcher_kqueue::dispatch_pending(std::size_t limit)
{
	uint32_t cookie = fdtab_.cookie();

	if (limit > 16) {
		limit = 16;
	}
	struct kevent events[16];

	struct timespec timeout;
	timeout.tv_sec = 0;
	timeout.tv_nsec = 0;

	ssize_t nevents = ::kevent(
		kqueue_fd_,
		/* modlist */ nullptr, /* modcount */ 0,
		events, limit,
		&timeout);

	if (nevents > 0) {
		process_events(events, nevents, cookie);
	} else {
		nevents = 0;
	}

	wakeup_flag_.clear();

	return nevents;
}

void
ioready_dispatcher_kqueue::wake_up() noexcept
{
	wakeup_flag_.set();
}

void
ioready_dispatcher_kqueue::synchronize() noexcept
{
	detail::fd_handler_table::delayed_handler_release rel = fdtab_.synchronize();

	lock_.sync_finished();

	rel.clear();
}

void
ioready_dispatcher_kqueue::update_evmask(
	int fd,
	ioready_events old_mask,
	ioready_events new_mask) const noexcept
{
	struct kevent modlist[2];
	std::size_t nmods = 0;
	if ((old_mask & ioready_output) != (new_mask & ioready_output)) {
		EV_SET(
			&modlist[nmods], fd, EVFILT_WRITE,
			(new_mask & ioready_output) ? EV_ADD : EV_DELETE,
			0, 0, (void *)EVFILT_WRITE);
		++nmods;
	}
	if ((old_mask & ioready_input) != (new_mask & ioready_input)) {
		EV_SET(&modlist[nmods], fd, EVFILT_READ,
			(new_mask & ioready_input) ? EV_ADD : EV_DELETE,
			0, 0, (void *)EVFILT_READ);
		++nmods;
	}
	if (nmods) {
		struct timespec timeout;
		timeout.tv_sec = 0;
		timeout.tv_nsec = 0;
		::kevent(kqueue_fd_,
			modlist, nmods,
			/* eventlist */ nullptr, /* eventcount */ 0,
			&timeout);
	}
}

ioready_connection
ioready_dispatcher_kqueue::watch(
	std::function<void(tscb::ioready_events)> function,
	int fd, tscb::ioready_events event_mask)
{
	link_type::pointer link(new link_type(this, std::move(function), fd, event_mask));
	ioready_events old_mask, new_mask;

	fdtab_.insert(link.get(), old_mask, new_mask);
	update_evmask(link->fd(), old_mask, new_mask);

	return ioready_connection(std::move(link));
}

/** \cond false */

ioready_dispatcher *
create_ioready_dispatcher_kqueue()
{
	return new ioready_dispatcher_kqueue();
}

/** \endcond */

}
