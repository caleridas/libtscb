/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_DETAIL_DEFERRED_LOCKS_H
#define TSCB_DETAIL_DEFERRED_LOCKS_H

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace tscb {
namespace detail {

class deferred_rwlock {
public:
	inline deferred_rwlock()
		: readers_(1), queued_(false)
	{
	}

	inline bool read_lock() noexcept
	{
		if (read_acquire()) {
			return false;
		} else {
			return read_lock_slow();
		}
	}

	inline bool read_unlock() noexcept
	{
		if (read_release()) {
			return false;
		} else {
			return read_unlock_slow();
		}
	}

	inline bool write_lock_async()
	{
		writers_.lock();
		if (!queued_) {
			queued_ = true;
			return readers_.fetch_sub(1, std::memory_order_acquire) == 1;
		}
		return false;
	}

	inline void write_unlock_async()
	{
		writers_.unlock();
	}

	inline void sync_finished()
	{
		queued_ = false;
		readers_.fetch_add(1, std::memory_order_release);
		write_unlock_async();
	}

private:
	/* out of line slow-path functions */
	bool read_lock_slow() noexcept;
	bool read_unlock_slow() noexcept;

	inline bool read_acquire() noexcept
	{
		size_t expected;
		bool success;
		do {
			expected = readers_.load(std::memory_order_relaxed);
			if (expected == 0) {
				return false;
			}
			success = readers_.compare_exchange_strong(expected, expected + 1, std::memory_order_acquire);
		} while (!success);
		return true;
	}

	inline bool read_release() noexcept
	{
		return readers_.fetch_sub(1, std::memory_order_release) != 1;
	}

	std::atomic<size_t> readers_;
	std::mutex writers_;
	bool queued_;
};

class deferrable_rwlock {
public:
	inline deferrable_rwlock()
		: readers_(1), queued_(false), waiting_(false)
	{
	}

	inline bool read_lock() noexcept
	{
		if (read_acquire()) return false;
		return read_lock_slow();
	}

	inline bool read_unlock() noexcept
	{
		if (read_release()) return false;
		return read_unlock_slow();
	}

	inline bool write_lock_async()
	{
		writers_.lock();
		bool sync = false;

		if ((!queued_) && (!waiting_)) {
			sync = readers_.fetch_sub(1, std::memory_order_acquire) == 1;
		}
		queued_ = true;

		return sync;
	}

	inline void write_unlock_async()
	{
		writers_.unlock();
	}

	inline std::unique_lock<std::mutex> write_lock_sync()
	{
		std::unique_lock<std::mutex> guard(writers_);
		for(;;) {
			if ((!queued_) && (!waiting_)) {
				if (readers_.fetch_sub(1, std::memory_order_acquire) == 1) {
					return guard;
				}
			}
			waiting_ = true;
			waiting_writers_.wait(guard);
		}
		return guard;
	}

	inline void write_unlock_sync(std::unique_lock<std::mutex> guard)
	{
		queued_ = false;
		waiting_ = false;
		readers_.fetch_sub(1, std::memory_order_release);
		guard.unlock();
	}

	inline void sync_finished()
	{
		queued_ = false;
		waiting_ = false;
		readers_.fetch_add(1, std::memory_order_release);
		write_unlock_async();
	}

private:
	/* out of line slow-path functions */
	bool read_lock_slow() noexcept;
	bool read_unlock_slow() noexcept;

	inline bool read_acquire() noexcept
	{
		size_t expected;
		bool success;
		do {
			expected = readers_.load(std::memory_order_relaxed);
			if (expected == 0) {
				return false;
			}
			success = readers_.compare_exchange_strong(expected, expected + 1, std::memory_order_acquire);
		} while (!success);
		return true;
	}

	inline bool read_release() noexcept
	{
		return readers_.fetch_sub(1, std::memory_order_release) != 1;
	}

	std::atomic<size_t> readers_;
	std::mutex writers_;
	std::condition_variable waiting_writers_;
	bool queued_, waiting_;
};

template<
	typename Object,
	deferrable_rwlock Object::*lock_member,
	void (Object::*synchronize)() noexcept
>
class read_guard {
public:
	read_guard(Object& object) noexcept
		: object_(object)
	{
		while ((object_.*lock_member).read_lock()) {
			(object_.*synchronize)();
		}
	}

	~read_guard()
	{
		if ((object_.*lock_member).read_unlock()) {
			(object_.*synchronize)();
		}
	}

private:
	Object& object_;
};

template<
	typename Object,
	deferrable_rwlock Object::*lock_member,
	void (Object::*synchronize)() noexcept
>
class async_write_guard {
public:
	async_write_guard(Object& object) noexcept
		: object_(object)
	{
		exclusive_ = (object_.*lock_member).write_lock_async();
	}

	~async_write_guard()
	{
		if (exclusive_) {
			(object_.*synchronize)();
		} else {
			(object_.*lock_member).write_unlock_async();
		}
	}

	inline bool exclusive() const noexcept
	{
		return exclusive_;
	}

private:
	Object & object_;
	bool exclusive_;
};

}
}

#endif
