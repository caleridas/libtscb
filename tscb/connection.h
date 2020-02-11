/* -*- C++ -*-
 * (c) 2009 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_CONNECTION_H
#define TSCB_CONNECTION_H

#include <atomic>
#include <cstddef>

#include <tscb/detail/intrusive-ptr.h>

namespace tscb {

class connection final {
public:
	class link_type {
	public:
		using pointer = detail::intrusive_ptr<link_type>;

		inline link_type() noexcept : refcount_(0) {}
		virtual ~link_type() noexcept;
		virtual void
		disconnect() noexcept = 0;
		virtual bool
		is_connected() const noexcept = 0;

		inline size_t
		reference_count() const noexcept
		{
			return refcount_.load(std::memory_order_relaxed);
		}

	private:
		std::atomic<size_t> refcount_;

		inline friend void intrusive_ptr_add_ref(link_type * link) noexcept
		{
			link->refcount_.fetch_add(1, std::memory_order_relaxed);
		}

		inline friend void intrusive_ptr_release(link_type * link) noexcept
		{
			if (link->refcount_.fetch_sub(1, std::memory_order_release) == 1) {
				atomic_thread_fence(std::memory_order_acquire);
				delete link;
			}
		}
	};

	inline
	connection() noexcept {}

	inline explicit
	connection(detail::intrusive_ptr<link_type> link) noexcept
		: link_(std::move(link))
	{
	}

	inline connection(const connection & other) noexcept
		: link_(other.link_)
	{
	}

	inline connection(connection && other) noexcept
		: link_(std::move(other.link_))
	{
	}

	inline connection & operator=(const connection & other) noexcept
	{
		link_ = other.link_;
		return *this;
	}

	inline connection & operator=(connection && other) noexcept
	{
		link_ = std::move(other.link_);
		return *this;
	}

	inline void
	swap(connection & other) noexcept
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

private:
	link_type::pointer link_;
};

class scoped_connection final {
public:
	using link_type = connection::link_type;

	inline ~scoped_connection() noexcept {
		disconnect();
	}

	inline scoped_connection() noexcept {}
	inline scoped_connection(const connection & c) noexcept : connection_(c) {}
	inline scoped_connection(connection && c) noexcept : connection_(std::move(c)) {}

	scoped_connection(const scoped_connection & other) = delete;
	scoped_connection & operator=(const scoped_connection & other) = delete;

	scoped_connection(scoped_connection && other) noexcept
	{
		swap(other);
	}
	scoped_connection &
	operator=(scoped_connection && other) noexcept
	{
		swap(other);
		return *this;
	}

	inline void
	swap(scoped_connection & other) noexcept
	{
		connection_.swap(other.connection_);
	}

	inline bool
	is_connected() const noexcept
	{
		return connection_.is_connected();
	}

	inline void disconnect() noexcept
	{
		connection_.disconnect();
	}

	inline scoped_connection & operator=(const connection & c) noexcept
	{
		disconnect();
		connection_ = c;
		return *this;
	}

	inline const link_type::pointer &
	link() const noexcept
	{
		return connection_.link();
	}

	inline link_type *
	get() const noexcept
	{
		return connection_.get();
	}

private:
	connection connection_;
};

}

#endif

