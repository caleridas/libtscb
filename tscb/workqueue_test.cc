/* -*- C++ -*-
 * (c) 2011 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/workqueue.h>

#include <thread>
#include <gtest/gtest.h>

#include <tscb/detail/eventflag.h>


namespace tscb {

class WorkqueueTest : public ::testing::Test {
public:
	WorkqueueTest() : wq_([this](){event_.set();}), called_count_(0) {}

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

	detail::pipe_eventflag event_;
	workqueue wq_;
	std::atomic<int> called_count_;
};

TEST_F(WorkqueueTest, basic_operation)
{
	connection conn;
	std::function<void()> trigger;
	std::tie(conn, trigger) = wq_.register_async_deferred_procedure([this](){call();});

	EXPECT_EQ(3, conn.get()->reference_count());

	std::thread t([&trigger]() {trigger();});

	while (called_count() == 0) {
		event_.wait();
		event_.clear();
		wq_.dispatch();
	}

	t.join();
}

TEST_F(WorkqueueTest, disconnect_triggered)
{
	connection conn;
	std::function<void()> trigger;
	std::tie(conn, trigger) = wq_.register_async_deferred_procedure([this](){call();});

	connection::link_type::pointer link(conn.get());
	/* one from dispatcher, one from connection, and one just acquired */
	EXPECT_EQ(4, link->reference_count());

	EXPECT_EQ(0, called_count());
	trigger();
	conn.disconnect();
	EXPECT_TRUE(wq_.pending());
	/* ref from connection object is dropped now */
	EXPECT_EQ(3, link->reference_count());

	wq_.dispatch();
	EXPECT_EQ(0, called_count());
	/* only our "private" ref as well as the trigger procedure remains
	 * now */
	EXPECT_EQ(2, link->reference_count());

	trigger = std::function<void()>();
	/* only our "private" ref remains now */
	EXPECT_EQ(1, link->reference_count());
}

TEST_F(WorkqueueTest, dispatch_throw)
{
	connection c1, c2;
	std::function<void()> trigger1, trigger2;
	int called_count = 0;
	std::tie(c1, trigger1) = wq_.register_async_deferred_procedure(
		[&called_count]()
		{
			++called_count;
			throw std::runtime_error("foo");
		});
	std::tie(c2, trigger2) = wq_.register_async_deferred_procedure(
		[&called_count]()
		{
			++called_count;
			throw std::runtime_error("foo");
		});

	trigger1();
	trigger2();
	EXPECT_TRUE(event_.flagged());

	/* dispatch pending events, will throw on first */
	event_.clear();
	try {
		wq_.dispatch();
		EXPECT_TRUE(false);
	}
	catch (std::runtime_error &) {
	}

	/* first must have been processed, other must remain pending;
	eventflag must have been reasserted */
	EXPECT_EQ(1, called_count);
	EXPECT_TRUE(event_.flagged());
	EXPECT_TRUE(wq_.pending());

	/* dispatch pending events, will throw on second */
	event_.clear();
	try {
		wq_.dispatch();
		EXPECT_TRUE(false);
	}
	catch(std::runtime_error &) {
	}

	/* second must have been processed */
	EXPECT_EQ(2, called_count);
}

}
