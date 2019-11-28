/* -*- C++ -*-
 * (c) 2011 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <thread>

#include "tscb/async-safe-work.h"

#include <gtest/gtest.h>

namespace tscb {

class AsyncWorkTest : public ::testing::Test {
public:
	AsyncWorkTest() : async_(event_), called_count_(0) {}

protected:
	void
	call() noexcept
	{
		called_count_.fetch_add(1, std::memory_order_relaxed);
	}

	void
	call_throw()
	{
		called_count_.fetch_add(1, std::memory_order_relaxed);
		throw std::runtime_error("foo");
	}

	inline int
	called_count() const noexcept
	{
		return called_count_.load(std::memory_order_relaxed);
	}

	pipe_eventflag event_;
	async_safe_work_dispatcher async_;
	std::atomic<int> called_count_;
};

TEST_F(AsyncWorkTest, basic_operation)
{
	async_safe_connection connection = async_.async_procedure([this](){call();});

	EXPECT_EQ(2, connection.get()->reference_count());

	std::thread t([&connection]() {connection.trigger();});

	while (called_count() == 0) {
		event_.wait();
		event_.clear();
		async_.dispatch();
	}

	t.join();
}

TEST_F(AsyncWorkTest, disconnect_triggered)
{
	async_safe_connection connection = async_.async_procedure([this](){call();});

	async_safe_connection::link_type::pointer link = connection.get();
	/* one from dispatcher, one from connection, and one just acquired */
	EXPECT_EQ(3, link->reference_count());

	EXPECT_EQ(0, called_count());
	connection.trigger();
	connection.disconnect();
	EXPECT_TRUE(async_.pending());
	/* ref from connection object is dropped now */
	EXPECT_EQ(2, link->reference_count());

	async_.dispatch();
	EXPECT_EQ(0, called_count());
	/* only our "private" ref remains now */
	EXPECT_EQ(1, link->reference_count());
}

TEST_F(AsyncWorkTest, dispatch_throw)
{
	async_safe_connection c1 = async_.async_procedure([this](){call_throw();});
	async_safe_connection c2 = async_.async_procedure([this](){call_throw();});

	c1.trigger();
	c2.trigger();
	assert(event_.flagged_.load(std::memory_order_relaxed) != 0);

	/* dispatch pending events, will throw on first */
	event_.clear();
	try {
		async_.dispatch();
		EXPECT_TRUE(false);
	}
	catch (std::runtime_error &) {
	}

	/* first must have been processed, other must remain pending;
	eventflag must have been reasserted */
	EXPECT_EQ(1, called_count());
	EXPECT_NE(0, event_.flagged_.load(std::memory_order_relaxed));
	EXPECT_TRUE(async_.pending());

	/* dispatch pending events, will throw on second */
	event_.clear();
	try {
		async_.dispatch();
		EXPECT_TRUE(false);
	}
	catch(std::runtime_error &) {
	}

	/* second must have been processed; nothing pending anymore */
	EXPECT_EQ(2, called_count());
	EXPECT_EQ(0, event_.flagged_.load(std::memory_order_relaxed));
}

}
