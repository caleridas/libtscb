/* -*- C++ -*-
 * (c) 2010 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_WORKQUEUE_H
#define TSCB_WORKQUEUE_H

#include <functional>
#include <list>
#include <mutex>

#include <tscb/connection.h>

namespace tscb {

class workqueue_service {
public:
	virtual ~workqueue_service() noexcept;

	virtual std::pair<connection, std::function<void()>>
	register_deferred_procedure(std::function<void()> function) = 0;

	virtual std::pair<connection, std::function<void()>>
	register_async_deferred_procedure(std::function<void()> function) = 0;

	virtual void
	queue_procedure(std::function<void()> function) = 0;
};

class workqueue final : public workqueue_service {
public:
	~workqueue() noexcept override;

	explicit
	workqueue(std::function<void()> trigger);

	std::pair<connection, std::function<void()>>
	register_deferred_procedure(std::function<void()> function) override;

	std::pair<connection, std::function<void()>>
	register_async_deferred_procedure(std::function<void()> function) override;

	void
	queue_procedure(std::function<void()> function) override;

	std::size_t
	dispatch();

	inline bool
	pending() const noexcept
	{
		return pending_.load(std::memory_order_relaxed);
	}

private:
	class link_type;
	class retrigger_guard;

	struct list_type {
		link_type* first;
		link_type* last;
	};

	void
	transfer_async_triggered() noexcept;

	void
	trigger() noexcept;

	detail::intrusive_ptr<link_type>
	get_registered() noexcept;

	static void
	list_erase(list_type& list, link_type* element) noexcept;

	static void
	list_insert(list_type& list, link_type* pos, link_type* element) noexcept;

	std::atomic<link_type*> async_triggered_;
	list_type active_;
	list_type inactive_;
	std::list<std::function<void()>> work_;
	std::atomic<bool> pending_;
	std::function<void()> trigger_;
	std::mutex lock_;
};

};

#endif
