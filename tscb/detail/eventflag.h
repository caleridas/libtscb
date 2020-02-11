/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_DETAIL_EVENTFLAG_H
#define TSCB_DETAIL_EVENTFLAG_H

#include <atomic>
#include <cstddef>

namespace tscb {
namespace detail {

class atomic_eventflag final {
public:
	inline atomic_eventflag() noexcept : state_(false) {}

	inline void
	set() noexcept
	{
		state_.store(true, std::memory_order_release);
	}
	inline void
	clear() noexcept
	{
		state_.store(false, std::memory_order_relaxed);
	}

	inline void
	wait() noexcept
	{
		while (!state_.load(std::memory_order_acquire)) {
		}
	}

private:
	std::atomic<bool> state_;
};

class pipe_eventflag final  {
public:
	~pipe_eventflag() noexcept;

	pipe_eventflag();

	inline void
	set() noexcept
	{
		/* fast path (to avoid write memory op) if flag is already set */
		if (flagged_.load(std::memory_order_relaxed) != 0) {
			return;
		}

		/* atomic exchange to ensure only one setter can "see" the
		0->1 transition; otherwise we could have spurious wakeups */
		int expected = 0;
		if (!flagged_.compare_exchange_strong(expected, 1, std::memory_order_release)) {
			return;
		}

		/* we are now certain that we have switched the flag from 0 to 1;
		if no one has been waiting before we switched the flag,
		there is no one to wakeup */

		if (waiting_.load(std::memory_order_relaxed) == 0) {
			return;
		}

		set_slow();
	}

	inline void
	wait() noexcept
	{
		/* fast path to avoid atomic op if flag is already set */
		if (flagged_.load(std::memory_order_acquire) != 0) {
			return;
		}

		wait_slow();
	}

	inline void
	clear() noexcept
	{
		int oldval;
		{
			oldval = flagged_.load(std::memory_order_relaxed);
			/* fast path (to avoid atomic op) if flag is already cleared */
			if (oldval==0) {
				return;
			}
			/* after clearing a flag, the application will test a
			condition in a data structure; make sure test of the
			condition and clearing of the flag are not reordered by
			changing the flag with "acquire" semantics */
		} while (!flagged_.compare_exchange_strong(oldval, 0, std::memory_order_acquire));
		if (oldval == 1) {
			return;
		}

		clear_slow();
	}


	inline void
	start_waiting() noexcept
	{
		waiting_.fetch_add(1, std::memory_order_relaxed);
	}

	inline void
	stop_waiting() noexcept
	{
		waiting_.fetch_sub(1, std::memory_order_relaxed);
	}

	inline bool flagged() const noexcept
	{
		return flagged_.load(std::memory_order_relaxed) != 0;
	}

	inline std::size_t waiting() const noexcept
	{
		return waiting_.load(std::memory_order_relaxed);
	}

	inline int readfd() const noexcept
	{
		return readfd_;
	}

private:
	void set_slow() noexcept;
	void wait_slow() noexcept;
	void clear_slow() noexcept;

	int readfd_;
	int writefd_;

	/** \internal
		\brief State of the event flag

		The event flag implementation uses the following internal states:
		- 0: not flagged
		- 1: flagged, but no wakeup via control pipe
		- 2: flagged, wakeup sent via control pipe
	*/
	std::atomic_int flagged_;
	/** \internal \brief Number of threads waiting */
	std::atomic<std::size_t> waiting_;
};

}
}

#endif
