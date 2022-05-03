/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_IOREADY_H
#define TSCB_IOREADY_H

/** \file ioready.h */

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

#include <tscb/connection.h>

namespace tscb {

class ioready_service;

class ioready_events {
public:
	/** \brief Initialize from bitmask */
	constexpr explicit inline ioready_events(int repr) : repr_(repr) {}

	constexpr inline ioready_events() noexcept : repr_(0) {}
	/** \brief Bitwise OR of event bitmask */
	inline ioready_events operator|(ioready_events other) const noexcept { return ioready_events(repr_ | other.repr_); }
	/** \brief Bitwise AND of event bitmask */
	inline ioready_events operator&(ioready_events other) const noexcept { return ioready_events(repr_ & other.repr_); }
	/** \brief Bitwise OR of event bitmask */
	inline ioready_events& operator|=(ioready_events other) noexcept { repr_ |= other.repr_; return *this; }
	/** \brief Bitwise AND of event bitmask */
	inline ioready_events& operator&=(ioready_events other) noexcept { repr_ &= other.repr_; return *this; }
	/** \brief Equality */
	inline bool operator==(ioready_events other) const noexcept { return repr_ == other.repr_; }
	/** \brief Inequality */
	inline bool operator!=(ioready_events other) const noexcept { return repr_ != other.repr_; }
	/** \brief Equivalent to *this != ioready_none */
	inline operator bool() const noexcept { return repr_; }
	/** \brief Bitwise negation */
	inline ioready_events operator~() const noexcept { return ioready_events(~repr_); }

private:
	int repr_;
};

constexpr ioready_events ioready_none{0x000};
constexpr ioready_events ioready_input{0x001};
constexpr ioready_events ioready_output{0x002};
constexpr ioready_events ioready_error{0x100};
constexpr ioready_events ioready_hangup{0x200};

class ioready_connection final {
public:
	class link_type : public connection::link_type {
	public:
		using pointer = detail::intrusive_ptr<link_type>;

		~link_type() noexcept override;

		void
		disconnect() noexcept override = 0;

		bool
		is_connected() const noexcept override = 0;

		virtual void
		modify(ioready_events new_event_mask) noexcept = 0;

		virtual ioready_events
		event_mask() const noexcept = 0;

	};

	inline
	ioready_connection() noexcept {}

	inline explicit
	ioready_connection(detail::intrusive_ptr<link_type> link) noexcept
		: link_(std::move(link))
	{
	}

	inline
	ioready_connection(const ioready_connection & other) noexcept
		: link_(other.link_)
	{
	}

	inline
	ioready_connection(ioready_connection && other) noexcept
		: link_(std::move(other.link_))
	{
	}

	inline ioready_connection &
	operator=(const ioready_connection & other) noexcept
	{
		link_ = other.link_;
		return *this;
	}

	inline ioready_connection &
	operator=(ioready_connection && other) noexcept
	{
		link_ = std::move(other.link_);
		return *this;
	}

	inline void
	swap(ioready_connection & other) noexcept
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
	modify(ioready_events events) noexcept
	{
		if (link_) {
			link_->modify(events);
		}
	}

	inline ioready_events
	event_mask() const noexcept
	{
		return link_ ? link_->event_mask() : ioready_none;
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

class scoped_ioready_connection final {
private:
	using link_type = ioready_connection::link_type;

public:
	inline ~scoped_ioready_connection () noexcept {
		disconnect();
	}

	inline scoped_ioready_connection() noexcept {}
	inline scoped_ioready_connection (const ioready_connection & c) noexcept : connection_(c) {}
	inline scoped_ioready_connection (ioready_connection && c) noexcept : connection_(std::move(c)) {}

	scoped_ioready_connection (const scoped_ioready_connection  & other) = delete;
	scoped_ioready_connection  & operator=(const scoped_ioready_connection  & other) = delete;

	scoped_ioready_connection (scoped_ioready_connection  && other) noexcept
	{
		swap(other);
	}
	scoped_ioready_connection  &
	operator=(scoped_ioready_connection  && other) noexcept
	{
		swap(other);
		return *this;
	}

	inline scoped_ioready_connection & operator=(const ioready_connection & c) noexcept
	{
		disconnect();
		connection_ = c;
		return *this;
	}

	inline void
	swap(scoped_ioready_connection & other) noexcept
	{
		connection_.swap(other.connection_);
	}

	inline bool
	is_connected() const noexcept
	{
		return connection_.is_connected();
	}

	inline void
	disconnect() noexcept
	{
		connection_.disconnect();
	}

	inline void
	modify(ioready_events events) noexcept
	{
		connection_.modify(events);
	}

	inline ioready_events
	event_mask() const noexcept
	{
		return connection_.event_mask();
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
	ioready_connection connection_;
};

class ioready_service {
public:
	virtual ~ioready_service() noexcept;

	virtual ioready_connection
	watch(std::function<void(tscb::ioready_events)> function, int fd, tscb::ioready_events event_mask) = 0;
};

class ioready_dispatcher : public ioready_service {
public:
	~ioready_dispatcher() noexcept override;

	virtual std::size_t
	dispatch(
		const std::chrono::steady_clock::duration * timeout,
		std::size_t limit = std::numeric_limits<std::size_t>::max()) = 0;

	virtual size_t
	dispatch_pending(
		std::size_t limit = std::numeric_limits<std::size_t>::max()) = 0;

	virtual void
	wake_up() noexcept = 0;

	static
	std::unique_ptr<ioready_dispatcher>
	create();
};

/**
	\brief Create dispatcher for ioready events

	XXX: to be removed
*/
ioready_dispatcher *
create_ioready_dispatcher();

}

#endif
