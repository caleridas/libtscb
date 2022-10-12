/* -*- C++ -*-
 * (c) 2022 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_INOTIFY_SERVICE_H
#define TSCB_INOTIFY_SERVICE_H

#include <cstdint>
#include <functional>

#include <sys/inotify.h>

#include <tscb/connection.h>

namespace tscb {

class inotify_service;

using inotify_events = uint32_t;

class inotify_connection final {
public:
	class link_type : public connection::link_type {
	public:
		using pointer = detail::intrusive_ptr<link_type>;

		~link_type() noexcept override;

		void
		disconnect() noexcept override = 0;

		bool
		is_connected() const noexcept override = 0;
	};

	inline
	inotify_connection() noexcept {}

	inline explicit
	inotify_connection(detail::intrusive_ptr<link_type> link) noexcept
		: link_(std::move(link))
	{
	}

	inline
	inotify_connection(const inotify_connection & other) noexcept
		: link_(other.link_)
	{
	}

	inline
	inotify_connection(inotify_connection && other) noexcept
		: link_(std::move(other.link_))
	{
	}

	inline inotify_connection &
	operator=(const inotify_connection & other) noexcept
	{
		link_ = other.link_;
		return *this;
	}

	inline inotify_connection &
	operator=(inotify_connection && other) noexcept
	{
		link_ = std::move(other.link_);
		return *this;
	}

	inline void
	swap(inotify_connection & other) noexcept
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

	inline operator connection() const noexcept
	{
		return connection(link_);
	}

private:
	link_type::pointer link_;
};

class scoped_inotify_connection final {
private:
	using link_type = inotify_connection::link_type;

public:
	inline ~scoped_inotify_connection () noexcept {
		disconnect();
	}

	inline scoped_inotify_connection() noexcept {}
	inline scoped_inotify_connection (const inotify_connection & c) noexcept : connection_(c) {}
	inline scoped_inotify_connection (inotify_connection && c) noexcept : connection_(std::move(c)) {}

	scoped_inotify_connection (const scoped_inotify_connection  & other) = delete;
	scoped_inotify_connection  & operator=(const scoped_inotify_connection  & other) = delete;

	scoped_inotify_connection (scoped_inotify_connection  && other) noexcept
	{
		swap(other);
	}
	scoped_inotify_connection  &
	operator=(scoped_inotify_connection  && other) noexcept
	{
		swap(other);
		return *this;
	}

	inline scoped_inotify_connection & operator=(const inotify_connection & c) noexcept
	{
		disconnect();
		connection_ = c;
		return *this;
	}

	inline void
	swap(scoped_inotify_connection & other) noexcept
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
	inotify_connection connection_;
};

class inotify_service {
public:
	virtual ~inotify_service() noexcept;

	virtual inotify_connection
	inode_watch(
		std::function<void(tscb::inotify_events, uint32_t, const char*)> function,
		const char* path,
		inotify_events event_mask
	) = 0;
};

}

#endif
