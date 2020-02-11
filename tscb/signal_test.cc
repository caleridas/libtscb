/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/signal.h>

#include <gtest/gtest.h>

namespace tscb {

class SignalTests : public ::testing::Test {
public:
	class Receiver {
	public:
		Receiver() : result(0), called(0), refcount(1) {}
		void cbrecv1(int arg) {result = arg;}
		void cbrecv2(int arg) {
			result = arg;
			link1.disconnect();
			EXPECT_TRUE(refcount == 2);
			EXPECT_TRUE(!link1.is_connected());
			EXPECT_TRUE(refcount == 2);
		}
		void cbrecv3(int arg) {
			++called;
			result = arg;
			link1.disconnect();
			link2.disconnect();
			fflush(stdout);
		}

		int result;
		int called;
		int refcount;

		tscb::connection link1, link2;

		friend inline void
		intrusive_ptr_add_ref(Receiver * r) noexcept
		{
			r->refcount += 1;
		}

		friend inline void
		intrusive_ptr_release(Receiver * r) noexcept
		{
			r->refcount -= 1;
		}
	};

	tscb::signal<void(int)> chain;
};

TEST_F(SignalTests, simple)
{
	/* verify that callbacks are invoked correctly at all, that
	callbacks are cancellable and that references to target objects
	are handled correctly */
	Receiver r;

	r.link1 = chain.connect(
		std::bind(
			&Receiver::cbrecv1,
			detail::intrusive_ptr<Receiver>(&r),
			std::placeholders::_1));
	EXPECT_TRUE(r.refcount == 2);

	chain(1);
	EXPECT_TRUE(r.result == 1);

	r.link1.disconnect();
	EXPECT_TRUE(r.refcount == 1);

	chain(2);
	EXPECT_TRUE(r.result == 1);
}
TEST_F(SignalTests, self_cancel)
{
	/* verify that callbacks can cancel themselves and that the reference
	count to the target object is dropped after the callback has
	completed */
	Receiver r;
	r.link1 = chain.connect(
		std::bind(
			&Receiver::cbrecv2,
			detail::intrusive_ptr<Receiver>(&r),
			std::placeholders::_1));

	chain(3);
	EXPECT_TRUE(r.result == 3);
	chain(4);
	EXPECT_TRUE(r.result == 3);

	EXPECT_TRUE(r.refcount == 1);
}

TEST_F(SignalTests, mutual_cancel)
{
	/* verify that callbacks can cancel each other (out of two
	callbacks that mutually cancel each other, exactly one must be
	executed) and that reference counting still works as expected */

	Receiver r;
	r.link1 = chain.connect(std::bind(&Receiver::cbrecv3, detail::intrusive_ptr<Receiver>(&r), std::placeholders::_1));
	r.link2 = chain.connect(std::bind(&Receiver::cbrecv3, detail::intrusive_ptr<Receiver>(&r), std::placeholders::_1));

	chain(5);

	EXPECT_TRUE(r.result == 5);
	EXPECT_TRUE(r.called == 1);
	EXPECT_TRUE(r.refcount == 1);
}
TEST_F(SignalTests, cancel_refcount)
{
	/* verify that, upon destroying a callback chain, all corresponding
	callback links are removed from the chain and all references
	to target objects are dropped as well */
	Receiver r;
	{
		tscb::signal<void (int)> chain;
		r.link1 = chain.connect(std::bind(&Receiver::cbrecv1, detail::intrusive_ptr<Receiver>(&r), std::placeholders::_1));
		EXPECT_TRUE(r.refcount == 2);
	}
	EXPECT_TRUE(r.refcount == 1);
	r.link1.disconnect();
}
TEST_F(SignalTests, lambda_fn)
{
	int called = 0;
	tscb::connection l = chain.connect([&called](int arg) {called += arg;});

	chain(1);
	EXPECT_TRUE(called == 1);

	l.disconnect();
	chain(1);
	EXPECT_TRUE(called == 1);
}
TEST_F(SignalTests, cancel_first)
{
/* check cancellation of first element in list */
	int called = 0;
	tscb::connection link1, link2;
	link1 = chain.connect([&called](int arg) {called += arg;});
	link2 = chain.connect([&called](int arg) {called += arg;});

	chain(1);
	EXPECT_TRUE(called == 2);

	link1.disconnect();
	called = 0;
	chain(1);
	EXPECT_TRUE(called == 1);

	link2.disconnect();
}
TEST_F(SignalTests, cancel_second)
{
/* check cancellation of second element in list */
	int called = 0;
	tscb::connection link1, link2;
	link1 = chain.connect([&called](int arg) {called += arg;});
	link2 = chain.connect([&called](int arg) {called += arg;});

	chain(1);
	EXPECT_TRUE(called == 2);

	link2.disconnect();
	called = 0;
	chain(1);
	EXPECT_TRUE(called == 1);

	link1.disconnect();
}

}
