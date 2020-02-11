/* -*- C++ -*-
 * (c) 2009 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_SIGNAL_H
#define TSCB_SIGNAL_H

#include <mutex>
#include <functional>
#include <stdexcept>

#include <tscb/connection.h>
#include <tscb/detail/deferred-locks.h>

namespace tscb {

template<typename Signature> class signal;

template<typename Signature>
class signal final {
private:
	class link_type final : public connection::link_type {
	public:
		using pointer = detail::intrusive_ptr<link_type>;

		/** \internal \brief Instantiate callback link */
		link_type(signal * master, std::function<Signature> function) noexcept
			: function_(std::move(function)), active_next_(nullptr), prev_(nullptr), next_(nullptr), chain_(master)
		{
		}

		~link_type() noexcept override {}

		void
		disconnect() noexcept override
		{
			registration_mutex_.lock();
			signal * chain = chain_.load(std::memory_order_relaxed);
			if (chain) {
				chain->remove(this);
			} else {
				registration_mutex_.unlock();
			}
		}

		bool
		is_connected() const noexcept override
		{
			return chain_.load(std::memory_order_relaxed) != nullptr;
		}

	private:
		std::function<Signature> function_;
		std::atomic<link_type *> active_next_;
		link_type * prev_;
		link_type * next_;
		link_type * deferred_destroy_next_;
		std::atomic<signal<Signature> *> chain_;
		std::mutex registration_mutex_;

		friend class signal;
	};

public:
	signal() noexcept
		: active_(nullptr), first_(nullptr), last_(nullptr), deferred_destroy_(nullptr)
	{}

	connection
	connect(std::function<Signature> function) /* throw(std::bad_alloc) */
	{
		typename link_type::pointer link(new link_type(this, std::move(function)));
		push_back(link.get());
		return connection(std::move(link));
	}

	~signal() noexcept
	{
		while (lock_.read_lock()) {
			synchronize();
		}
		bool any_cancelled = false;
		for (;;) {
			link_type * tmp = active_.load(std::memory_order_relaxed);
			if (!tmp) {
				break;
			}
			any_cancelled = true;
			tmp->disconnect();
		}
		if (lock_.read_unlock()) {
			/* the above cancel operations will cause synchronization
			to be performed at the next possible point in time; if
			there is no concurrent cancellation, this is now */
			synchronize();
		} else if (any_cancelled) {
			/* this can only happen if some callback link was
			cancelled while this object is being destroyed; in
			that case we have to suspend the thread that is destroying
			the object until we are certain that synchronization has
			been performed */
			auto guard = lock_.write_lock_sync();
			link_type * to_destroy = synchronize_top();
			lock_.write_unlock_sync(std::move(guard));
			synchronize_bottom(to_destroy);
		}
	}

	template<typename... Args>
	inline void
	operator()(Args... args)
	{
		detail::read_guard<
			signal<Signature>,
			&signal<Signature>::lock_,
			&signal<Signature>::synchronize
		> guard(*this);
		link_type * l = active_.load(std::memory_order_consume);
		while(l) {
			l->function_(std::forward<Args>(args)...);
			l = l->active_next_.load(std::memory_order_consume);
		}
	}

	inline bool
	disconnect_all()
	{
		bool any_disconnected = false;
		detail::read_guard<
			signal<Signature>,
			&signal<Signature>::lock_,
			&signal<Signature>::synchronize
		> guard(*this);
		link_type * l = active_.load(std::memory_order_consume);
		while (l) {
			any_disconnected = true;
			l->disconnect();
			l = l->active_next_.load(std::memory_order_consume);
		}

		return any_disconnected;
	}

private:
	/** \internal \brief Add link to end of chain */
	void
	push_back(link_type * l) noexcept
	{
		intrusive_ptr_add_ref(l);
		/* note: object has been fully constructed at this point,
		but the following lock acquisition only provides "acquire"
		semantics so that the memory references constructing
		this object are allowed to "leak" into the locked
		region. We therefore need an explicit fence here in
		order to avoid making an uninitialized element visible
		during traversal of the chain */
		std::atomic_thread_fence(std::memory_order_release);

		l->registration_mutex_.lock();
		bool sync = lock_.write_lock_async();

		l->next_ = nullptr;
		l->prev_ = last_;

		l->active_next_.store(nullptr, std::memory_order_relaxed);

		/* add element to active list; find all elements that have been removed
		from the full list and thus terminate the active list; point them to
		the newly-added element */

		link_type * tmp = last_;
		for (;;) {
			if (!tmp) {
				if (!active_.load(std::memory_order_relaxed)) {
					active_.store(l, std::memory_order_release);
				}
				break;
			}
			if (tmp->active_next_.load(std::memory_order_relaxed)) {
				break;
			}
			tmp->active_next_.store(l, std::memory_order_release);
			tmp = tmp->prev_;
		}

		/* insert into list of all elements*/
		if (last_) {
			last_->next_ = l;
		} else {
			first_ = l;
		}
		last_  =l;

		l->chain_.store(this, std::memory_order_relaxed);

		l->registration_mutex_.unlock();

		if (sync) {
			synchronize();
		} else {
			lock_.write_unlock_async();
		}
	}

	/** \internal \brief Remove link from chain */
	void
	remove(link_type * l) noexcept
	{
		bool sync = lock_.write_lock_async();
		if (l->chain_.load(std::memory_order_relaxed) == this) {
			/* remove element from active list; we have to make
			sure that all elements that pointed to "us" within
			the active chain now point to the following element,
			so this element is skipped from within the active chain */

			link_type * tmp = l->prev_;
			link_type * next = l->active_next_.load(std::memory_order_relaxed);
			for (;;) {
				if (!tmp) {
					if (active_.load(std::memory_order_relaxed) == l) {
						active_.store(next, std::memory_order_release);
					}
					break;
				}
				if (tmp->active_next_.load(std::memory_order_relaxed) != l) {
					break;
				}
				tmp->active_next_.store(next, std::memory_order_release);
				tmp = tmp->prev_;
			}

			/* put on list of elements marked to be destroyed at sync point */
			l->deferred_destroy_next_ = deferred_destroy_;
			deferred_destroy_ = l;

			/* remove pointer to chain, so a second call to ->cancel()
			will do nothing */
			l->chain_.store(nullptr, std::memory_order_relaxed);
		}

		l->registration_mutex_.unlock();

		if (sync) {
			synchronize();
		} else {
			lock_.write_unlock_async();
		}
	}

	link_type *
	synchronize_top() noexcept
	{
		link_type * to_destroy = deferred_destroy_;

		/* first, "repair" the list structure by "correcting" all prev
		pointers */
		while (to_destroy) {
			/* we can now safely remove the elements from the list */
			if (to_destroy->prev_) {
				to_destroy->prev_->next_ = to_destroy->next_;
			} else {
				first_ = to_destroy->next_;
			}
			if (to_destroy->next_) {
				to_destroy->next_->prev_ = to_destroy->prev_;
			} else {
				last_ = to_destroy->prev_;
			}

			to_destroy = to_destroy->deferred_destroy_next_;
		}

		/* now swap pointers while still under the lock; this is
		necessary to make sure that the destructor for each
		callback link object is called exactly once */
		to_destroy = deferred_destroy_;
		deferred_destroy_ = nullptr;

		return to_destroy;
	}

	void
	synchronize_bottom(link_type * to_destroy) noexcept
	{
		/* now we can release the callbacks, as we are sure that no one
		can "see" them anymore; the lock is dropped so side-effest
		of finalizing the links cannot cause deadlocks */

		while (to_destroy) {
			link_type * tmp = to_destroy->deferred_destroy_next_;
			to_destroy->function_ = nullptr;
			intrusive_ptr_release(to_destroy);
			to_destroy = tmp;
		}
	}

	/** \internal \brief Synchronize when reaching quiescent state */
	void
	synchronize() noexcept
	{
		link_type * to_destroy = synchronize_top();
		lock_.sync_finished();
		synchronize_bottom(to_destroy);
	}

	/** \internal \brief singly-linked list of active elements */
	std::atomic<link_type *> active_;

	/** \internal \brief thread synchronization */
	detail::deferrable_rwlock lock_;

	/** \internal \brief First element in the chain, whether active or not */
	link_type * first_;
	/** \internal \brief Last element in the chain, whether active or not */
	link_type * last_;

	/** \internal \brief List of elements to be cancelled

		singly-linked list of elements that have been removed from
		the active list, but are not yet removed from the full list
		and have not been discarded yet
	*/
	link_type * deferred_destroy_;
};

extern template class signal<void()>;

}

#endif
