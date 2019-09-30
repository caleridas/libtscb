/* -*- C++ -*-
 * (c) 2010 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file_event "COPYING" for details.
 */

#ifndef TSCB_ASYNC_SAFE_WORK_H
#define TSCB_ASYNC_SAFE_WORK_H

/**
	\page async_safe_work_descr Async-safe triggered work interface

*/

#include <list>

#include <tscb/eventflag.h>
#include <tscb/signal.h>

namespace tscb {

/**
	\brief Async-safe callback triggering
*/
class async_safe_connection {
public:
	/**
		\brief Callback link for async safe triggering

		This class represents a link that has been established by registering
		a callback that can be triggered in an async-safe fashion.
	*/
	class link_type : public connection::link_type {
	public:
		using pointer = boost::intrusive_ptr<link_type>;

		~link_type() noexcept override;

		virtual void
		trigger() noexcept = 0;
	};

	inline
	async_safe_connection() noexcept {}

	inline explicit
	async_safe_connection(boost::intrusive_ptr<link_type> link) noexcept
		: link_(std::move(link))
	{
	}

	inline
	async_safe_connection(const async_safe_connection & other) noexcept
		: link_(other.link_)
	{
	}

	inline
	async_safe_connection(async_safe_connection && other) noexcept
		: link_(std::move(other.link_))
	{
	}

	inline async_safe_connection &
	operator=(const async_safe_connection & other) noexcept
	{
		link_ = other.link_;
		return *this;
	}

	inline async_safe_connection &
	operator=(async_safe_connection && other) noexcept
	{
		link_ = std::move(other.link_);
		return *this;
	}

	inline void
	swap(async_safe_connection & other) noexcept
	{
		link_.swap(other.link_);
	}

	inline void
	disconnect() noexcept
	{
		if (link_) {
			link_->disconnect();
			link_.reset();
		}
	}

	inline bool
	is_connected() const noexcept
	{
		return link_ && link_->is_connected();
	}

	inline void
	trigger() const noexcept
	{
		if (link_) {
			link_->trigger();
		}
	}

	inline std::function<void()>
	get_trigger_fn() const
	{
		if (link_) {
			return std::bind(&link_type::trigger, link_type::pointer(link_));
		} else {
			return [](){};
		}
	}

	inline const link_type::pointer &
	link() const noexcept
	{
		return link_;
	}

	inline link_type *
	get() const noexcept
	{
		return link_.get();
	}

	inline operator connection() const noexcept
	{
		return connection(link_);
	}

private:
	link_type::pointer link_;
};

/**
	\brief Async-triggered procedures interface

	Represents a registration interface for procedures that can be
	triggered in an async-safe way.
*/
class async_safe_work_service {
public:
	virtual ~async_safe_work_service() noexcept;

	/**
		\brief Register async-safe triggered work procedure

		\param function The procedure to be called
		\returns Connection object

		Registers a procedure and associates it with an eventflag.
		When the eventflag is set, the designated procedure will
		eventually be called at least once. The eventflag can
		safely be triggered from signal handler context.
	*/
	virtual async_safe_connection
	async_procedure(std::function<void()> function) = 0;
};

/**
	\brief Stand-alone dispatcher for async-triggered procedures
*/
class async_safe_work_dispatcher final : public async_safe_work_service {
public:
	async_safe_work_dispatcher(eventtrigger & trigger);

	~async_safe_work_dispatcher() noexcept override;

	async_safe_connection
	async_procedure(std::function<void()> function) override;

	/**
		\brief Dispatch pending events
		\return
			Number of pending async procedures processed
	*/
	size_t
	dispatch();

	inline bool
	pending() const noexcept
	{
		return pending_.load(std::memory_order_relaxed) != nullptr;
	}

protected:
	class link_type;
	class dequeue_helper;

	/** \internal \brief Singly-linked list of pending async procedures */
	std::atomic<link_type *> pending_;

	/** \internal \brief Count of procs to be cancelled asynchronously */
	std::atomic<size_t> async_cancel_count_;

	/** \internal \brief Doubly-linked list of available async procedures */
	link_type * first_;
	link_type * last_;

	std::mutex list_mutex_;

	eventtrigger & trigger_;
};

}

#endif
