/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_REACTOR_H
#define TSCB_REACTOR_H

#include <list>
#include <mutex>

#include <tscb/reactor-service.h>


namespace tscb {

/**
	\brief Posix reactor service provider

	This class implements the \ref reactor_service interface
	and is capable of running stand-alone to provide the requested
	notifications.
*/
class reactor final : public reactor_service {
public:
	reactor();
	virtual ~reactor() noexcept;

	void dispatch();

	bool dispatch_pending();

	void dispatch_pending_all();

	/* workqueue_service */

	std::pair<connection, std::function<void()>>
	register_deferred_procedure(std::function<void()> function) override;

	std::pair<connection, std::function<void()>>
	register_async_deferred_procedure(std::function<void()> function) override;

	void
	queue_procedure(std::function<void()> function) override;

	/* timer_service */

	timer_connection
	timer(
		std::function<void(std::chrono::steady_clock::time_point)> function,
		std::chrono::steady_clock::time_point when)
		override;

	timer_connection
	one_shot_timer(
		std::function<void(std::chrono::steady_clock::time_point)> function,
		std::chrono::steady_clock::time_point when)
		override;

	timer_connection
	suspended_timer(
		std::function<void(std::chrono::steady_clock::time_point)> function)
		override;

	timer_connection
	one_shot_suspended_timer(
		std::function<void(std::chrono::steady_clock::time_point)> function)
		override;

	/* ioready_service */

	ioready_connection
	watch(
		std::function<void(tscb::ioready_events)> function,
		int fd, tscb::ioready_events event_mask) /* throw(std::bad_alloc) */
			override;

	void
	wake_up() noexcept;

private:
	std::unique_ptr<ioready_dispatcher> io_;
	timer_dispatcher timer_;
	workqueue workqueue_;
};

}

#endif
