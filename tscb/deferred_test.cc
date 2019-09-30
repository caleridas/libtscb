/* -*- C++ -*-
 * (c) 2011 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/deferred.h>

#include <condition_variable>
#include <thread>

#include <gtest/gtest.h>

namespace tscb {

TEST(DeferredLockTests, simple_read_locking)
{
	deferred_rwlock lock;

	EXPECT_FALSE(lock.read_lock());
	EXPECT_FALSE(lock.read_unlock());
}

TEST(DeferredLockTests, nested_read_locking)
{
	deferred_rwlock lock;

	EXPECT_FALSE(lock.read_lock());
	EXPECT_FALSE(lock.read_lock());
	EXPECT_FALSE(lock.read_unlock());
	EXPECT_FALSE(lock.read_unlock());
}

TEST(DeferredLockTests, simple_write_locking)
{
	deferred_rwlock lock;

	EXPECT_TRUE(lock.write_lock_async());
	lock.sync_finished();
}

TEST(DeferredLockTests, nested_read_write_locking)
{
	deferred_rwlock lock;

	// thread 1
	EXPECT_FALSE(lock.read_lock());

	// thread 2
	EXPECT_FALSE(lock.write_lock_async());
	lock.write_unlock_async();

	// thread 1
	EXPECT_TRUE(lock.read_unlock());
	lock.sync_finished();
}

TEST(DeferredLockTests, concurrent_read_write_locking)
{
	deferred_rwlock lock;

	struct stage {
		std::mutex m;
		std::condition_variable c;
		int value = 0;
		void set(int new_value)
		{
			std::unique_lock<std::mutex> guard(m);
			value = new_value;
			c.notify_all();
		}

		void await(int expect_value)
		{
			std::unique_lock<std::mutex> guard(m);
			while (value != expect_value) {
				c.wait(guard);
			}
		}
	} stage;

	std::thread t1([&lock, &stage]() {
		EXPECT_FALSE(lock.read_lock());
		stage.set(1);
		stage.await(2);
		EXPECT_TRUE(lock.read_unlock());
	});

	std::thread t2([&lock, &stage]() {
		stage.await(1);
		EXPECT_FALSE(lock.write_lock_async());
		stage.set(2);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		lock.write_unlock_async();
	});

	t1.join();
	t2.join();
}

}

#if 0
	// hm... need real threads here, otherwise it wil deadlock
	// I mean... it is *supposed* to deadlock :)
	// test "interleawed" read/write locking
	sync=lock.read_lock();
	ASSERT(sync==false);

			// think thread 2
			sync=lock.write_lock_async();
			ASSERT(sync==false);

	sync=lock.read_unlock();
	ASSERT(sync==true);
	lock.sync_finished();

			// think thread 2
			lock.write_unlock_async();
#endif
