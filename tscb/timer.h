/* -*- C++ -*-
 * (c) 2019 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_TIMER_H
#define TSCB_TIMER_H

/**
\file timer.h
*/

#include <chrono>
#include <new>
#include <mutex>

#include <sys/time.h>
#include <time.h>

#include <tscb/detail/intrusive-list.h>
#include <tscb/signal.h>

namespace tscb {

template<typename TimePoint>
class basic_timer_connection {
public:
#if 0
	static_assert(
		noexcept(std::declval<TimePoint &>() < std::declval<TimePoint &>()),
		"require noexcept time point comparison");
#endif
	static_assert(
		noexcept(std::declval<TimePoint &>() = std::declval<TimePoint &>()),
		"require noexcept time point assignment");
	static_assert(
		noexcept(std::declval<TimePoint &>() = std::move(std::declval<TimePoint &>())),
		"require noexcept time point assignment");

	class link_type : public connection::link_type {
	public:
		using pointer = detail::intrusive_ptr<link_type>;

		~link_type() noexcept override
		{
		}

		void
		disconnect() noexcept override = 0;

		bool
		is_connected() const noexcept override = 0;

		virtual void
		set(TimePoint when) noexcept = 0;

		virtual const TimePoint &
		when() const noexcept = 0;

		virtual void
		suspend() noexcept = 0;

		virtual bool
		suspended() const noexcept = 0;
	};

	inline
	basic_timer_connection() noexcept {}

	inline explicit
	basic_timer_connection(detail::intrusive_ptr<link_type> link) noexcept
		: link_(std::move(link))
	{
	}

	inline
	basic_timer_connection(const basic_timer_connection & other) noexcept
		: link_(other.link_)
	{
	}

	inline
	basic_timer_connection(basic_timer_connection && other) noexcept
		: link_(std::move(other.link_))
	{
	}

	inline basic_timer_connection &
	operator=(const basic_timer_connection & other) noexcept
	{
		link_ = other.link_;
		return *this;
	}

	inline basic_timer_connection &
	operator=(basic_timer_connection && other) noexcept
	{
		link_ = std::move(other.link_);
		return *this;
	}

	inline void
	swap(basic_timer_connection & other) noexcept
	{
		link_.swap(other.link_);
	}

	inline void
	disconnect() noexcept
	{
		if (link_) {
			link_->disconnect();
			link_.reset();
		}
	}

	inline bool
	is_connected() const noexcept
	{
		return link_ && link_->is_connected();
	}

	inline void
	set(TimePoint when) noexcept
	{
		if (link_) {
			link_->set(when);
		}
	}

	inline void
	suspend() noexcept
	{
		if (link_) {
			link_->suspend();
		}
	}

	inline const typename link_type::pointer &
	link() const noexcept
	{
		return link_;
	}

	inline link_type *
	get() const noexcept
	{
		return link_.get();
	}

	inline bool
	suspended() const noexcept
	{
		if (link_) {
			return link_->suspended();
		} else {
			return true;
		}
	}

	inline TimePoint
	when() const noexcept
	{
		if (link_) {
			return link_->when();
		} else {
			return {};
		}
	}

	inline operator connection() const noexcept
	{
		return connection(link_);
	}

private:
	typename link_type::pointer link_;
};

template<typename TimePoint>
class scoped_basic_timer_connection {
private:
	using link_type = typename basic_timer_connection<TimePoint>::link_type;
public:
	inline ~scoped_basic_timer_connection() noexcept {
		disconnect();
	}

	inline scoped_basic_timer_connection() noexcept {}
	inline scoped_basic_timer_connection(const basic_timer_connection<TimePoint> & other) noexcept : connection_(other) {}
	inline scoped_basic_timer_connection(basic_timer_connection<TimePoint> && other) noexcept : connection_(std::move(other)) {}

	scoped_basic_timer_connection(const scoped_basic_timer_connection & other) = delete;
	scoped_basic_timer_connection & operator=(const scoped_basic_timer_connection & other) = delete;

	scoped_basic_timer_connection(scoped_basic_timer_connection && other) noexcept
	{
		swap(other);
	}
	scoped_basic_timer_connection &
	operator=(scoped_basic_timer_connection && other) noexcept
	{
		swap(other);
		return *this;
	}

	inline void
	swap(scoped_basic_timer_connection & other) noexcept
	{
		connection_.swap(other.connection_);
	}

	inline bool
	is_connected() const noexcept
	{
		return connection_.is_connected();
	}

	inline void
	set(TimePoint when) noexcept
	{
		connection_.set(std::move(when));
	}

	inline TimePoint
	when() const noexcept
	{
		return connection_.when();
	}

	inline void
	suspend() noexcept
	{
		connection_.suspend();
	}

	inline bool
	suspended() const noexcept
	{
		return connection_.suspended();
	}

	inline void disconnect() noexcept
	{
		connection_.disconnect();
	}

	inline scoped_basic_timer_connection &
	operator=(const basic_timer_connection<TimePoint> & c) noexcept
	{
		disconnect();
		connection_ = c;
		return *this;
	}

	inline const typename link_type::pointer &
	link() const noexcept
	{
		return connection_.link();
	}

	inline link_type *
	get() const noexcept
	{
		return connection_.get();
	}

private:
	basic_timer_connection<TimePoint> connection_;
};

template<typename TimePoint>
class basic_timer_service {
public:
	virtual
	~basic_timer_service() noexcept
	{
	}

	virtual basic_timer_connection<TimePoint>
	timer(
		std::function<void(TimePoint)> function, TimePoint when) = 0;

	virtual basic_timer_connection<TimePoint>
	one_shot_timer(
		std::function<void(TimePoint)> function, TimePoint when) = 0;

	virtual basic_timer_connection<TimePoint>
	suspended_timer(
		std::function<void(TimePoint)> function) = 0;

	virtual basic_timer_connection<TimePoint>
	one_shot_suspended_timer(
		std::function<void(TimePoint)> function) = 0;
};

template<typename TimePoint>
class basic_timer_dispatcher final : public basic_timer_service<TimePoint> {
private:
	using connection = basic_timer_connection<TimePoint>;

	class link_type final : public connection::link_type {
	public:
		using pointer = detail::intrusive_ptr<link_type>;

		~link_type() noexcept override
		{
		}

		link_type(
			basic_timer_dispatcher * master,
			std::function<void(TimePoint when)> function,
			TimePoint when,
			bool one_shot) noexcept
			: function_(std::move(function)) , when_(std::move(when))
			, master_(master) , hold_count_(1), suspended_(false)
			, one_shot_(one_shot)
		{
		}

		void
		disconnect() noexcept override
		{
			std::unique_lock<std::mutex> guard(registry_mutex_);
			basic_timer_dispatcher * master = master_.load(std::memory_order_relaxed);
			if (master) {
				link_type::pointer ptr;
				{
					std::lock_guard<std::mutex> guard(master->mutex_);
					master->make_inactive(index_);
					ptr = master->unlink(index_);
					master_.store(nullptr, std::memory_order_relaxed);
				}
				guard.unlock();
				release_hold_count();
				ptr.reset();
			}
		}

		bool
		is_connected() const noexcept override
		{
			return master_.load(std::memory_order_relaxed) != nullptr;
		}

		void
		set(TimePoint when) noexcept override
		{
			std::lock_guard<std::mutex> guard(registry_mutex_);
			basic_timer_dispatcher * master = master_.load(std::memory_order_relaxed);
			if (master) {
				std::lock_guard<std::mutex> guard(master->mutex_);
				when_ = std::move(when);
				master->make_active(index_);
				suspended_.store(false, std::memory_order_relaxed);
				if (master->timers_[0].get() == this && hold_count_.load(std::memory_order_relaxed) < 2) {
					master->timer_added_();
				}
			}
		}

		const TimePoint &
		when() const noexcept override
		{
			return when_;
		}

		void
		suspend() noexcept override
		{
			std::lock_guard<std::mutex> guard(registry_mutex_);
			basic_timer_dispatcher * master = master_.load(std::memory_order_relaxed);
			if (master) {
				std::lock_guard<std::mutex> guard(master->mutex_);
				if (!suspended_.load(std::memory_order_relaxed)) {
					master->make_inactive(index_);
					suspended_.store(true, std::memory_order_relaxed);
				}
			}
		}

		bool
		suspended() const noexcept override
		{
			return suspended_.load(std::memory_order_relaxed);
		}

	private:
		inline void
		acquire_hold_count() noexcept
		{
			hold_count_.fetch_add(1, std::memory_order_relaxed);
		}

		inline void
		release_hold_count() noexcept
		{
			if (hold_count_.fetch_sub(1, std::memory_order_release) == 1) {
				std::atomic_thread_fence(std::memory_order_acquire);
				function_ = nullptr;
			}
		}

		std::function<void(TimePoint when)> function_;
		TimePoint when_;

		std::mutex registry_mutex_;
		std::atomic<basic_timer_dispatcher *> master_;
		std::atomic<std::size_t> hold_count_;
		std::size_t index_;
		std::atomic<bool> suspended_;
		bool one_shot_;

		friend class basic_timer_dispatcher;
	};

	struct less_type {
		inline bool operator()(const TimePoint& x, const TimePoint& y) noexcept
		{
			return x < y;
		}
	};

public:
	~basic_timer_dispatcher() noexcept override
	{
		while (detach_registered()) {}
	}

	inline explicit
	basic_timer_dispatcher(std::function<void()> timer_added)
		: timer_added_(std::move(timer_added)), running_(0)
	{
	}

	std::pair<bool, TimePoint>
	next_timer() const noexcept
	{
		std::unique_lock<std::mutex> guard(mutex_);
		if (active_limit_ > 0) {
			return {true, timers_[0]->when()};
		} else {
			return {false, TimePoint()};
		}
	}

	std::size_t
	run(TimePoint now, std::size_t limit = std::numeric_limits<std::size_t>::max())
	{
		std::size_t count = 0;
		while (count < limit) {
			if (run_single(now)) {
				count += 1;
			} else {
				break;
			}
		}
		return count;
	}

	bool
	run_single(TimePoint now)
	{
		typename link_type::pointer link;
		{
			std::unique_lock<std::mutex> guard(mutex_);
			if (active_limit_ == 0) {
				return false;
			}
			if (less_(now, timers_[0]->when())) {
				return false;
			}
			link = timers_[0];
			make_inactive(link->index_);
			link->suspended_.store(true, std::memory_order_relaxed);
			link->acquire_hold_count();
		}

		if (link->one_shot_) {
			link->disconnect();
		}

		link->function_(now);

		// XXX: guard against exception thrown by function call
		link->release_hold_count();

		return true;
	}

	basic_timer_connection<TimePoint>
	timer(
		std::function<void (TimePoint)> function, TimePoint when)
		override
	{
		typename link_type::pointer link(new link_type(this, std::move(function), when, false));
		return register_timer(std::move(link), false);
	}

	basic_timer_connection<TimePoint>
	one_shot_timer(
		std::function<void(TimePoint)> function, TimePoint when)
		override
	{
		typename link_type::pointer link(new link_type(this, std::move(function), when, true));
		return register_timer(std::move(link), false);
	}

	basic_timer_connection<TimePoint>
	suspended_timer(
		std::function<void(TimePoint)> function)
		override
	{
		typename link_type::pointer link(new link_type(this, std::move(function), {}, false));
		return register_timer(std::move(link), true);
	}

	basic_timer_connection<TimePoint>
	one_shot_suspended_timer(
		std::function<void(TimePoint)> function)
		override
	{
		typename link_type::pointer link(new link_type(this, std::move(function), {}, true));
		return register_timer(std::move(link), true);
	}

private:
	inline connection
	register_timer(
		typename link_type::pointer link, bool suspended) noexcept
	{
		bool need_wakeup = false;

		{
			std::lock_guard<std::mutex> guard(mutex_);
			link->index_ = timers_.size();
			timers_.push_back(link);
			link->suspended_.store(suspended, std::memory_order_relaxed);
			if (!suspended) {
				make_active(link->index_);
				need_wakeup = link->index_ == 0;
			}
		}

		if (need_wakeup) {
			timer_added_();
		}

		return connection(std::move(link));
	}

	bool
	detach_registered() noexcept
	{
		typename link_type::pointer link;
		{
			std::unique_lock<std::mutex> guard(mutex_);
			if (timers_.empty()) {
				return false;
			}
			link = std::move(timers_[timers_.size() - 1]);
			timers_.resize(timers_.size() - 1);
			link->master_.store(nullptr, std::memory_order_relaxed);
		}
		if (link) {
			link->function_ = nullptr;
		}
		return link;
	}

	void make_active(std::size_t index)
	{
		if (index < active_limit_) {
			make_inactive(index);
			index = active_limit_;
		}
		if (index != active_limit_) {
			timers_[index].swap(timers_[active_limit_]);
			timers_[index]->index_ = index;
			timers_[active_limit_]->index_ = active_limit_;
		}
		index = active_limit_;
		++active_limit_;
		typename link_type::pointer element = std::move(timers_[index]);
		while (index) {
			std::size_t parent = (index - 1) >> 1;
			if (!less_(element->when(), timers_[parent]->when())) {
				break;
			}
			timers_[index] = std::move(timers_[parent]);
			timers_[index]->index_ = index;
			index = parent;
		}
		element->index_ = index;
		timers_[index] = std::move(element);
	}

	void make_inactive(std::size_t index)
	{
		if (index >= active_limit_) {
			return;
		}

		--active_limit_;
		if (index >= active_limit_) {
			return;
		}

		std::size_t orig_index = index;

		typename link_type::pointer tmp = std::move(timers_[active_limit_]);

		typename link_type::pointer element = std::move(timers_[index]);
		element->index_ = active_limit_;
		timers_[active_limit_] = std::move(element);
		while (index) {
			std::size_t parent = (index - 1) >> 1;
			if (less_(timers_[parent]->when(), tmp->when())) {
				break;
			}
			timers_[index] = std::move(timers_[parent]);
			timers_[index]->index_ = index;
			index = parent;
		}
		tmp->index_ = index;
		timers_[index] = std::move(tmp);

		rebalance(orig_index);
	}

	void rebalance(std::size_t index)
	{
		for (;;) {
			std::size_t lowest = index;
			std::size_t left = (index << 1) + 1;
			std::size_t right = left + 1;
			if (left < active_limit_) {
				if (less_(timers_[left]->when(), timers_[lowest]->when())) {
					lowest = left;
				}
				if (right < active_limit_) {
					if (less_(timers_[right]->when(), timers_[lowest]->when())) {
						lowest = right;
					}
				}
			}

			if (lowest == index) {
				break;
			}

			timers_[index].swap(timers_[lowest]);
			timers_[index]->index_ = index;
			timers_[lowest]->index_ = lowest;
			index = lowest;
		}
	}

	typename link_type::pointer unlink(std::size_t index)
	{
		if (index < active_limit_) {
			make_inactive(index);
			index = active_limit_;
		}
		if (index != timers_.size() - 1) {
			timers_[index].swap(timers_[timers_.size() - 1]);
			timers_[index]->index_ = index;
			index = timers_.size() - 1;
		}
		typename link_type::pointer ptr = std::move(timers_[timers_.size() - 1]);
		timers_.resize(timers_.size() - 1);
		return ptr;
	}

	mutable std::mutex mutex_;
	/* List of timers: elements in [0..active_limit_) form a priority
	 * heap of active timers. Elements in [active_limit..end) form
	 * a simple sequence of suspended timers. */
	std::vector<typename link_type::pointer> timers_;
	/* Invariant: active_limit_ <= timers_.size(). */
	std::size_t active_limit_ = 0;
	std::function<void()> timer_added_;
	std::size_t running_;
	less_type less_;
};

extern template class basic_timer_connection<std::chrono::steady_clock::time_point>;
extern template class scoped_basic_timer_connection<std::chrono::steady_clock::time_point>;
extern template class basic_timer_service<std::chrono::steady_clock::time_point>;
extern template class basic_timer_dispatcher<std::chrono::steady_clock::time_point>;

using timer_connection = basic_timer_connection<std::chrono::steady_clock::time_point>;
using scoped_timer_connection = scoped_basic_timer_connection<std::chrono::steady_clock::time_point>;
using timer_service = basic_timer_service<std::chrono::steady_clock::time_point>;
using timer_dispatcher = basic_timer_dispatcher<std::chrono::steady_clock::time_point>;

}

#endif
