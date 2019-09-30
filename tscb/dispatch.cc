/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#include <memory>

#include <tscb/dispatch.h>

namespace tscb {

void dispatch(
	tscb::timer_dispatcher * tq,
	tscb::ioready_dispatcher * io)
{
	bool timers_pending;
	std::chrono::steady_clock::time_point next_timer, now;
	std::tie(timers_pending, next_timer) = tq->next_timer();
	if (timers_pending) {
		now = std::chrono::steady_clock::now();
		while (timers_pending && next_timer <= now) {
			tq->run(now);
			now = std::chrono::steady_clock::now();
			std::tie(timers_pending, next_timer) = tq->next_timer();
		}
	}

	if (timers_pending) {
		std::chrono::steady_clock::duration timeout = next_timer - now;
		io->dispatch(&timeout);
	} else {
		io->dispatch(0);
	}
}

posix_reactor::posix_reactor()
	throw(std::bad_alloc, std::runtime_error)
	: io_(create_ioready_dispatcher()),
	trigger_(io_->get_eventtrigger()),
	timer_(trigger_),
	async_workqueue_(trigger_)
{
}

posix_reactor::~posix_reactor() noexcept
{
}

void
posix_reactor::post(std::function<void()> function) /*throw(std::bad_alloc)*/
{
	{
		std::unique_ptr<workitem> item(new workitem(std::move(function)));
		std::unique_lock<std::mutex> guard(workqueue_lock_);
		workqueue_.push_back(item.get());
		item.release();
	}
	trigger_.set();
}

timer_connection
posix_reactor::timer(
	std::function<void(std::chrono::steady_clock::time_point)> function,
	std::chrono::steady_clock::time_point when)
	/* throw(std::bad_alloc)*/
{
	return timer_.timer(std::move(function), std::move(when));
}

timer_connection
posix_reactor::one_shot_timer(
	std::function<void(std::chrono::steady_clock::time_point)> function,
	std::chrono::steady_clock::time_point when)
	/* throw(std::bad_alloc)*/
{
	return timer_.one_shot_timer(std::move(function), std::move(when));
}

timer_connection
posix_reactor::suspended_timer(
	std::function<void(std::chrono::steady_clock::time_point)> function)
	/* throw(std::bad_alloc)*/
{
	return timer_.suspended_timer(std::move(function));
}

timer_connection
posix_reactor::one_shot_suspended_timer(
	std::function<void(std::chrono::steady_clock::time_point)> function)
	/* throw(std::bad_alloc)*/
{
	return timer_.one_shot_suspended_timer(std::move(function));
}

ioready_connection
posix_reactor::watch(
	std::function<void(tscb::ioready_events)> function,
	int fd, tscb::ioready_events event_mask) /* throw(std::bad_alloc) */
{
	return io_->watch(std::move(function), fd, event_mask);
}

async_safe_connection
posix_reactor::async_procedure(std::function<void()> function)
{
	return async_workqueue_.async_procedure(std::move(function));
}

eventtrigger &
posix_reactor::get_eventtrigger() /*throw(std::bad_alloc)*/
{
	return trigger_;
}

void
posix_reactor::dispatch()
{
	if (!workqueue_.empty()) {
		std::unique_lock<std::mutex> guard(workqueue_lock_);
		std::unique_ptr<workitem> item(workqueue_.pop());
		guard.unlock();

		if (item.get()) {
			item->function_();
		}

		guard.lock();
		if (!workqueue_.empty()) {
			trigger_.set();
		}
	}
	async_workqueue_.dispatch();
	tscb::dispatch(&timer_, io_.get());
}

bool
posix_reactor::dispatch_pending()
{
	bool processed_events = false;

	if (!workqueue_.empty()) {
		std::unique_lock<std::mutex> guard(workqueue_lock_);
		std::unique_ptr<workitem> item(workqueue_.pop());
		guard.unlock();

		if (item.get()) {
			item->function_();
			processed_events = true;
		}

		guard.lock();
		if (!workqueue_.empty()) {
			trigger_.set();
		}
	}

	if (async_workqueue_.dispatch()) {
		processed_events = true;
	}

	bool timers_pending;
	std::chrono::steady_clock::time_point first_timer_due;
	std::tie(timers_pending, first_timer_due) = timer_.next_timer();
	if (timers_pending) {
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

		if (first_timer_due <= now) {
			processed_events = true;

			timer_.run(now);
		}
	}

	if (io_->dispatch_pending()) {
		processed_events = true;
	}

	return processed_events;
}

void posix_reactor::dispatch_pending_all()
{
	while (dispatch_pending()) {
		/* empty */
	}
}

};
