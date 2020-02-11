/* -*- C++ -*-
 * (c) 2010 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_DETAIL_FD_HANDLER_TABLE_H
#define TSCB_DETAIL_FD_HANDLER_TABLE_H

#include <cstdint>

#include <tscb/ioready.h>

namespace tscb {
namespace detail {

class fd_handler_table {
public:
	class delayed_handler_release;

	class link_type : public ioready_connection::link_type {
	public:
		using pointer = detail::intrusive_ptr<link_type>;

		link_type(
			std::function<void(ioready_events)> fn,
			int fd,
			ioready_events event_mask) noexcept
			: fn_(std::move(fn)), fd_(fd), event_mask_(event_mask)
		{
		}

		~link_type() noexcept override;

		void
		disconnect() noexcept override = 0;

		bool
		is_connected() const noexcept override = 0;

		void
		modify(ioready_events new_event_mask) noexcept override = 0;

		ioready_events
		event_mask() const noexcept override;

		inline int
		fd() const noexcept
		{
			return fd_;
		}

	private:
		std::function<void(ioready_events)> fn_;
		std::atomic<link_type *> active_next_;
		link_type * prev_;
		link_type * next_;
		link_type * inactive_next_;
		int fd_;
		ioready_events event_mask_;

		friend class fd_handler_table;
		friend class delayed_handler_release;
	};

	class delayed_handler_release {
	public:
		~delayed_handler_release() noexcept;
		delayed_handler_release() noexcept;
		delayed_handler_release(const delayed_handler_release &) = delete;
		delayed_handler_release(delayed_handler_release && other) noexcept;

		delayed_handler_release &
		operator=(const delayed_handler_release &) = delete;
		delayed_handler_release &
		operator=(delayed_handler_release && other) noexcept;

		void
		clear() noexcept;

	private:
		delayed_handler_release(link_type * link);

		link_type * link_;

		friend class fd_handler_table;
	};

	~fd_handler_table() noexcept;

	explicit
	fd_handler_table(size_t initial = 32);

	void
	insert(
		link_type * link,
		ioready_events & old_mask,
		ioready_events & new_mask);

	void
	remove(
		link_type * link,
		ioready_events & old_mask,
		ioready_events & new_mask) noexcept;

	void
	modify(
		link_type * link,
		ioready_events mask,
		ioready_events & old_mask,
		ioready_events & new_mask) noexcept;

	bool
	disconnect_all() noexcept;

	inline uint32_t
	cookie() const noexcept;

	void
	notify(int fd, ioready_events events, uint32_t call_cookie);

	delayed_handler_release
	synchronize() noexcept;

private:
	struct chain {
		chain() noexcept : active_(nullptr), first_(nullptr), last_(nullptr), cookie_(0) {}

		std::atomic<link_type *> active_;
		link_type * first_;
		link_type * last_;
		std::atomic<uint32_t> cookie_;

		ioready_events
		compute_event_mask() const noexcept;
	};

	struct table {
		explicit
		table(size_t initial) /* throw(std::bad_alloc) */;

		void
		clear_entries() noexcept;

		size_t capacity_;
		std::unique_ptr<std::atomic<chain *>[]> entries_;
		table * old_;
	};

	/** \brief Compute event mask for descriptor

		Computes the effective event mask to check for on the given file
		descriptor.

		This operation must be called with write lock held.
	*/
	ioready_events
	compute_event_mask(int fd) noexcept;

	chain *
	get_create_chain(int fd) /* throw(std::bad_alloc) */;

	chain *
	get_chain(int fd) noexcept;

	table *
	extend_table(
		table * tab,
		std::size_t required_capacity) /*throw(std::bad_alloc)*/;

	void
	deallocate_old_tables() noexcept;

	std::atomic<table *> table_;
	link_type * inactive_;
	std::atomic<uint32_t> cookie_;
	bool need_cookie_sync_;
};

inline uint32_t
fd_handler_table::cookie() const noexcept
{
	return cookie_.load(std::memory_order_relaxed);
}

}
}

#endif
