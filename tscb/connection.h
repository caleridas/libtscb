/* -*- C++ -*-
 * (c) 2009 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_CONNECTION_H
#define TSCB_CONNECTION_H


#include <mutex>
#include <stdexcept>
#include <functional>

#include <boost/intrusive_ptr.hpp>

#include <tscb/deferred.h>

namespace tscb {


/**
	\brief Connection between signal and receiver

	Every other connection implemented in this library can be
	downcast to this type.
*/
class connection {
public:
	/**
		\brief Abstract base of all callback objects

		This object represents the link between a sender/caller, from
		which notification is requested, to a reciever/callee, to which
		notification is to be delivered. It is an abstract base class
		for all different kinds of links established through the various
		notification interfaces (callback_chains, file or timer events).
	*/
	class link_type {
	public:
		using pointer = boost::intrusive_ptr<link_type>;

		inline link_type() noexcept : refcount_(0) {}
		virtual ~link_type() noexcept;
		/**
			\brief Break the connection

			Calling this function will break the notification connection. It will
			usually cease notifications to be delivered some time after this
			function has returned. The exact semantic guarantee is:

			<UL>
				<LI>
					no notification will be delivered within the same thread
					that has called \ref disconnect after \ref disconnect has
					returned (i.e. within the same thread, \ref disconnect
					is synchronous)
				</LI>
				<LI>
					notifications in other threads may be delivered after
					\ref disconnect has returned in one thread, but only
					for events that occured before \ref disconnect has returned
					(i.e. for other threads, cancellation is asynchronous).
				</LI>
			</UL>

			The weak synchronicity guarantee allows implementations that
			provide excellent concurrency. Furthermore it allows
			\ref disconnect to be called from arbitrary contexts: from within
			the callback to be cancelled, from different threads etc. It
			is guaranteed to be deadlock free.

		*/
		virtual void
		disconnect() noexcept = 0;
		/**
			\brief Test if connection is alive

			\return True if connected, false if disconnected
		*/
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
	connection(boost::intrusive_ptr<link_type> link) noexcept
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

/**
	\brief Scoped connection between signal and receiver

	Variant of \ref connection object that automatically
	breaks the connection when this object goes out of scope.

	\warning This class can be used by an object to track
	signal connections to itself, and have all connections
	broken automatically when the object is destroyed.
	Only do this when you know that all callback invocations
	as well as the destructor will always run from the
	same thread.
*/
class scoped_connection {
public:
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

protected:
	connection connection_;
};

}

#endif

