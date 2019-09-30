/* -*- C++ -*-
 * (c) 2009 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/eventflag.h>

#include <gtest/gtest.h>

namespace tscb {

TEST(EventFlagTests, pipe_eventflag_ops)
{
	pipe_eventflag e;

	EXPECT_EQ(0, e.flagged_.load(std::memory_order_relaxed));
	e.set();
	EXPECT_EQ(1, e.flagged_.load(std::memory_order_relaxed));
	e.clear();
	EXPECT_EQ(0, e.flagged_.load(std::memory_order_relaxed));

	e.start_waiting();
	EXPECT_EQ(1, e.waiting_.load(std::memory_order_relaxed));
	e.stop_waiting();
	EXPECT_EQ(0, e.waiting_.load(std::memory_order_relaxed));

	e.set();
	EXPECT_EQ(1, e.flagged_.load(std::memory_order_relaxed));
	e.clear();
}

}
