/* -*- C++ -*-
 * (c) 2009 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/detail/eventflag.h>

#include <gtest/gtest.h>

namespace tscb {
namespace detail {

TEST(EventFlagTests, pipe_eventflag_ops)
{
	pipe_eventflag e;

	EXPECT_FALSE(e.flagged());
	e.set();
	EXPECT_TRUE(e.flagged());
	e.clear();
	EXPECT_FALSE(e.flagged());

	e.start_waiting();
	EXPECT_EQ(1, e.waiting());
	e.stop_waiting();
	EXPECT_EQ(0, e.waiting());

	e.set();
	EXPECT_TRUE(e.flagged());
	e.clear();
}

}
}
