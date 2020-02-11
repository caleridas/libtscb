/* -*- C++ -*-
 * (c) 2019 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/detail/fd-handler-table.h>

#include <gtest/gtest.h>

namespace tscb {
namespace detail {

class FdHandlerTableTests : public ::testing::Test {
public:
	class link_type final : public fd_handler_table::link_type {
	public:
		using pointer = detail::intrusive_ptr<link_type>;

		link_type(
			std::function<void(ioready_events)> fn,
			int fd,
			ioready_events event_mask,
			fd_handler_table * table) noexcept
			: fd_handler_table::link_type(std::move(fn), fd, event_mask)
			, table_(table)
		{
		}

		~link_type() noexcept override
		{
		}

		void
		disconnect() noexcept override
		{
			if (table_) {
				ioready_events old_fd_mask, new_fd_mask;
				table_->remove(this, old_fd_mask, new_fd_mask);
			}
			table_ = nullptr;
		}

		bool
		is_connected() const noexcept override
		{
			return table_ != nullptr;
		}

		void
		modify(ioready_events new_event_mask) noexcept override
		{
			if (table_) {
				ioready_events old_fd_mask, new_fd_mask;
				table_->modify(this, new_event_mask, old_fd_mask, new_fd_mask);
			}
		}

	private:
		fd_handler_table * table_;
	};
};

TEST_F(FdHandlerTableTests, empty)
{
	fd_handler_table tab;
}

}
}
