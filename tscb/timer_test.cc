/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/timer.h>

#include <gtest/gtest.h>

namespace tscb {

class X {
public:
	X() : refcount_(0) {}
	void fn(long long t) {}

	int refcount_;
	friend inline void intrusive_ptr_add_ref(X *x) {++x->refcount_;}
	friend inline void intrusive_ptr_release(X *x) {--x->refcount_;}
};

class Y {
public:
	Y() : refcount_(0) {}
	void fn(long long t) {connection.disconnect(); EXPECT_EQ(1, refcount_); }

	basic_timer_connection<long long> connection;
	int refcount_;
	friend inline void intrusive_ptr_add_ref(Y * y) {++y->refcount_;}
	friend inline void intrusive_ptr_release(Y * y) {--y->refcount_;}
};

TEST(TimerTests, empty)
{
	basic_timer_dispatcher<long long> timers([](){});
	EXPECT_EQ(0, timers.run(0));
	EXPECT_FALSE(timers.next_timer().first);
}

TEST(TimerTests, simple)
{
	bool flagged = false;
	basic_timer_dispatcher<long long> tq([&flagged](){flagged = true;});

	int called = 0;

	basic_timer_connection<long long> connection;
	connection = tq.timer([&called, &connection](long long t) {++called; connection.set(t + 1);}, 0);

	EXPECT_TRUE(flagged);
	flagged = false;

	std::size_t count = tq.run(0);
	EXPECT_EQ(1 ,count);
	EXPECT_TRUE(tq.next_timer().first);
	EXPECT_EQ(1, called);
	EXPECT_EQ(1, connection.link()->when());
	EXPECT_FALSE(flagged);
	connection.disconnect();
	EXPECT_FALSE(flagged);
	count = tq.run(1);
	EXPECT_EQ(0, count);
	EXPECT_FALSE(tq.next_timer().first);
	EXPECT_EQ(1, called);
	EXPECT_FALSE(flagged);

	EXPECT_FALSE(connection.is_connected());
}

TEST(TimerTests, self_disconnect)
{
	basic_timer_dispatcher<long long> timers([](){});

	int called = 0;
	basic_timer_connection<long long> connection;
	connection = timers.timer([&connection, &called](long long t){++called; connection.disconnect(); connection.set(t + 1);}, 0);
	timers.run(0);
	EXPECT_EQ(1, called);
	EXPECT_FALSE(connection.is_connected());
}

TEST(TimerTests, member_fn)
{
	basic_timer_dispatcher<long long> timers([](){});

	X x;
	basic_timer_connection<long long> connection =
		timers.timer(std::bind(&X::fn, &x, std::placeholders::_1), 0);
	connection.disconnect();
	EXPECT_FALSE(connection.is_connected());
}

TEST(TimerTests, reference_count_disconnect)
{
	basic_timer_dispatcher<long long> timers([](){});

	X x;
	EXPECT_EQ(0, x.refcount_);
	basic_timer_connection<long long> connection =
		timers.timer(std::bind(&X::fn, detail::intrusive_ptr<X>(&x), std::placeholders::_1), 0);
	EXPECT_EQ(1, x.refcount_);
	connection.disconnect();
	EXPECT_EQ(0, x.refcount_);
	EXPECT_FALSE(connection.is_connected());
}

TEST(TimerTests, reference_count_expire)
{
	basic_timer_dispatcher<long long> timers([](){});

	Y y;
	EXPECT_EQ(0, y.refcount_);
	y.connection =
		timers.timer(std::bind(&Y::fn, detail::intrusive_ptr<Y>(&y), std::placeholders::_1), 0);
	EXPECT_EQ(1, y.refcount_);
	timers.run(0);
	EXPECT_EQ(0, y.refcount_);
	EXPECT_FALSE(y.connection.is_connected());
}

}
