/* -*- C++ -*-
 * (c) 2022 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/inotify.h>

#include <limits.h>
#include <unistd.h>

#include <cstring>



namespace tscb {

/* exclude private nested class from doxygen */
/** \cond false */

class inotify_dispatcher::link_type final : public inotify_connection::link_type {
public:
	using pointer = detail::intrusive_ptr<link_type>;

	using read_guard = detail::read_guard<
		inotify_dispatcher,
		&inotify_dispatcher::lock_,
		&inotify_dispatcher::synchronize
	>;

	using write_guard = detail::async_write_guard<
		inotify_dispatcher,
		&inotify_dispatcher::lock_,
		&inotify_dispatcher::synchronize
	>;

	link_type(
		inotify_dispatcher* master,
		std::function<void(tscb::inotify_events, uint32_t, const char*)> fn,
		inotify_events event_mask) noexcept
		: fn_(std::move(fn)), wd_(-1), event_mask_(event_mask), master_(master)
	{
	}

	~link_type() noexcept override
	{
	}

	void
	disconnect() noexcept override
	{
		std::unique_lock<std::mutex> rguard(registration_mutex_);
		inotify_dispatcher* master = master_.load(std::memory_order_relaxed);

		if (master) {
			write_guard wguard(*master);
			bool remove_wd = master->remove(this);
			if (remove_wd) {
				::inotify_rm_watch(master->fd_, wd());
			}

			master_.store(nullptr, std::memory_order_relaxed);

			rguard.unlock();
		}
	}

	bool
	is_connected() const noexcept override
	{
		return master_.load(std::memory_order_relaxed) != nullptr;
	}

	inline int
	wd() const noexcept
	{
		return wd_;
	}

private:
	std::function<void(tscb::inotify_events, uint32_t, const char*)> fn_;

	std::atomic<link_type*> active_next_;
	link_type* prev_;
	link_type* next_;
	link_type* inactive_next_;

	int wd_;
	inotify_events event_mask_;

	std::mutex registration_mutex_;
	std::atomic<inotify_dispatcher*> master_;

	friend class inotify_dispatcher;
};

/** \endcond */

/**
	\class inotify_dispatcher
	\brief Dispatcher for inotify events.

	Dispatch inotify events to registered handlers.

	\fn inotify_dispatcher::dispatch
	\brief Dispatch up to specified number of events

	\param limit
		Maximum number of events to be processed.
	\return
		Number of events processed.

	Checks for pending inotify events and calls
	registered callback functions.

	All pending events are processed up to \p limit  number of events;
	unprocessed events will be processed in further calls to \ref
	inotify_dispatcher::dispatch "dispatch". The function may return
	0 if no further events are pending.

	Callers are advised to make sure all events are processed until
	this function eventually returns a number that is less than the
	one given by "limit".

	The function is generally reentrant, multiple threads can enter the
	dispatching loop simultaneously.


	\fn inotify_dispatcher::watch
	\brief Wake up event dispatcher prematurely.

	\param function
		Callback to invoke when event occurs.
	\param path
		File path to watch.
	\param event_mask
		Events to subscribe to.
	\return
		Connection object for this watch.

	Sets up to watch given events on inotify identified by path.

	\fn inotify_dispatcher::fd
	\brief Retrieve internal file descriptor

	\return
		File descriptor.

	Retrieves the internal file descriptor used by inotify. This can
	be registered with other services (e.g. reactor) for event-driven
	processing.
*/


inotify_dispatcher::~inotify_dispatcher() noexcept
{
	/* we can assume

	- no thread is actively dispatching at the moment
	- no user can register new callbacks at the moment

	if those conditions are not met, we are in big trouble anyway, and
	there is no point doing anything about it
	*/

	while(lock_.read_lock()) {
		synchronize();
	}
	bool any_disconnected = disconnect_all();
	if (lock_.read_unlock()) {
		/* the above cancel operations will cause synchronization
		to be performed at the next possible point in time; if
		there is no concurrent cancellation, this is now */
		synchronize();
	} else if (any_disconnected) {
		/* this can only happen if some callback link was
		cancelled while this object is being destroyed; in
		that case we have to suspend the thread that is destroying
		the object until we are certain that synchronization has
		been performed */

		lock_.write_lock_sync();
		synchronize();

		/* note that synchronize implicitly calls sync_finished,
		which is equivalent to write_unlock_sync for deferrable_rwlocks */
	}

	::close(fd_);
}

inotify_dispatcher::inotify_dispatcher()
	: fd_(-1)
{
	wd_hash_buckets_.reset(new bucket_type[4]);
	wd_hash_bucket_mask_ = 3;
	fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd_ < 0) {
		throw std::runtime_error("Unable to create inotify descriptor");
	}
}

inotify_connection
inotify_dispatcher::inode_watch(
	std::function<void(tscb::inotify_events, uint32_t, const char*)> function,
	const char* path,
	tscb::inotify_events event_mask)
{
	std::string strpath(path);
	link_type::pointer link(new link_type(this, std::move(function), event_mask));

	link_type::write_guard wguard(*this);
	link->wd_ = ::inotify_add_watch(fd_, path, event_mask | IN_MASK_ADD);
	if (link->wd_ >= 0) {
		insert(link.get());
	} else {
		link.reset();
	}

	return inotify_connection(std::move(link));
}

std::size_t
inotify_dispatcher::dispatch(std::size_t limit)
{
	std::size_t processed = 0;

	while (limit) {
		char buffer[sizeof(struct inotify_event) + NAME_MAX + 1];
		ssize_t read_result = ::read(fd_, &buffer, sizeof(buffer));
		if (read_result <= 0) {
			break;
		}

		const char* pos = buffer;
		const char* end = buffer + read_result;

		while (pos < end) {
			struct inotify_event ev;
			std::memcpy(&ev, pos, sizeof(ev));

			link_type::read_guard rguard(*this);
			const bucket_type& bucket = wd_hash_buckets_[ev.wd & wd_hash_bucket_mask_];
			link_type * link = bucket.active.load(std::memory_order_consume);
			while (link) {
				if (link->wd() == ev.wd && ((link->event_mask_ & ev.mask) != 0)) {
					static const char empty[] = {'\0'};
					link->fn_(link->event_mask_ & ev.mask, ev.cookie, ev.len ? pos + sizeof(ev) : empty);
				}
				link = link->active_next_.load(std::memory_order_consume);
			}

			pos += sizeof(ev) + ev.len;

			++processed;
			if (limit > 0) {
				--limit;
			}
		}
	}

	return processed;
}

int
inotify_dispatcher::fd() const noexcept
{
	return fd_;
}

void
inotify_dispatcher::insert(link_type* link) noexcept
{
	bucket_type& bucket = wd_hash_buckets_[link->wd() & wd_hash_bucket_mask_];

	intrusive_ptr_add_ref(link);

	bool wd_exists = false;
	/* determine whether given descriptor is registered already */
	link_type * tmp = bucket.active.load(std::memory_order_relaxed);
	while (tmp) {
		if (tmp->wd() == link->wd()) {
			wd_exists = true;
			break;
		}
		tmp = tmp->active_next_.load(std::memory_order_relaxed);
	}

	/* prepare element */
	link->prev_ = bucket.last;
	link->next_ = nullptr;
	link->active_next_.store(nullptr, std::memory_order_relaxed);

	/* we are now going to "publish" this element; since we may be
	inserting multiple references, just issue a thread fence once
	and use relaxed memory order */
	atomic_thread_fence(std::memory_order_release);

	/* add element to active list; find all elements that have been removed
	from the full list and thus terminate the active list; point them to
	the newly-added element */
	tmp = bucket.last;

	for (;;) {
		if (!tmp) {
			if (bucket.active.load(std::memory_order_relaxed) == nullptr) {
				bucket.active.store(link, std::memory_order_relaxed);
			}
			break;
		}
		if (tmp->active_next_.load(std::memory_order_relaxed) != nullptr) {
			break;
		}
		tmp->active_next_.store(link, std::memory_order_relaxed);
		tmp = tmp->prev_;
	}

	if (bucket.last) {
		bucket.last->next_ = link;
	} else {
		bucket.first = link;
	}

	bucket.last = link;

	if (!wd_exists) {
		wd_entry_count_ += 1;
	}
}

bool
inotify_dispatcher::remove(link_type* link) noexcept
{
	bucket_type& bucket = wd_hash_buckets_[link->wd() & wd_hash_bucket_mask_];

	link_type * tmp = link->prev_;
	link_type * next = link->active_next_.load(std::memory_order_relaxed);
	for (;;) {
		if (!tmp) {
			if (bucket.active.load(std::memory_order_relaxed) == link) {
				bucket.active.store(next, std::memory_order_release);
			}
			break;
		}
		if (tmp->active_next_.load(std::memory_order_relaxed) != link) {
			break;
		}
		tmp->active_next_.store(next, std::memory_order_release);
		tmp = tmp->prev_;
	}

	bool wd_exists = false;
	/* determine whether given descriptor is registered still */
	tmp = bucket.active.load(std::memory_order_relaxed);
	while (tmp) {
		if (tmp->wd() == link->wd()) {
			wd_exists = true;
			break;
		}
		tmp = tmp->active_next_.load(std::memory_order_relaxed);
	}

	link->inactive_next_ = inactive_;
	inactive_ = link;

	if (!wd_exists) {
		wd_entry_count_ -= 1;
	}

	return !wd_exists;
}

void
inotify_dispatcher::synchronize() noexcept
{
	link_type* to_release = inactive_;
	inactive_ = nullptr;

	link_type* tmp = to_release;
	while (tmp) {
		bucket_type& bucket = wd_hash_buckets_[tmp->wd() & wd_hash_bucket_mask_];
		if (tmp->prev_) {
			tmp->prev_->next_ = tmp->next_;
		} else {
			bucket.first = tmp->next_;
		}
		if (tmp->next_) {
			tmp->next_->prev_ = tmp->prev_;
		} else {
			bucket.last = tmp->prev_;
		}
		tmp = tmp->inactive_next_;
	}

	check_resize();

	lock_.sync_finished();

	while (to_release) {
		link_type* link = to_release;
		to_release = link->inactive_next_;

		link->fn_ = nullptr;
		intrusive_ptr_release(link);
	}
}

bool
inotify_dispatcher::disconnect_all()
{
	bool any_disconnected = false;
	std::size_t nbuckets = wd_hash_bucket_mask_ + 1;
	for (std::size_t n = 0; n < nbuckets; ++n) {
		bucket_type& bucket = wd_hash_buckets_[n];

		link_type* tmp = bucket.active.load(std::memory_order_consume);
		while (tmp) {
			any_disconnected = true;
			tmp->disconnect();
			tmp = bucket.active.load(std::memory_order_consume);
		}
	}

	return any_disconnected;
}

/*
	Check whether we need to resize hash table. Attempts to
	resize, but will just not do anything on allocation
	failure (hash will continue operating in degraded mode).
*/
void
inotify_dispatcher::check_resize() noexcept
{
	while (wd_entry_count_ > wd_hash_bucket_mask_) {
		try {
			rehash(wd_hash_bucket_mask_ * 2 + 1);
		}
		catch (std::bad_alloc& e) {
		}
	}
	while (wd_entry_count_ < wd_hash_bucket_mask_ / 4 && wd_hash_bucket_mask_ > 3) {
		try {
			rehash(wd_hash_bucket_mask_ / 2);
		}
		catch (std::bad_alloc& e) {
		}
	}
}

/*
	Re-arrange buckets for new hash. This is a potentially
	destructive operation and must be performed under write
	lock.
	It can also raise std::bad_alloc, so it may be necessary
	to catch that in noexcept contexts.
*/
void
inotify_dispatcher::rehash(std::size_t new_bucket_mask)
{
	std::size_t old_bucket_count = wd_hash_bucket_mask_ + 1;
	std::size_t new_bucket_count = new_bucket_mask + 1;
	std::unique_ptr<bucket_type[]> buckets(new bucket_type[new_bucket_count]);

	// Note: everything below is noexcept.
	for (std::size_t n = 0; n < old_bucket_count; ++n) {
		bucket_type& old_bucket = wd_hash_buckets_[n];
		while (old_bucket.first) {
			link_type* link = old_bucket.first;
			// Remove from old bucket.
			if (link->next_) {
				link->next_->prev_ = nullptr;
			} else {
				old_bucket.last = nullptr;
			}
			old_bucket.first = link->next_;

			std::size_t new_index = link->wd() & new_bucket_mask;
			bucket_type& new_bucket = buckets[new_index];

			// Insert into new bucket.
			link->next_ = nullptr;
			link->prev_ = new_bucket.last;
			link->active_next_.store(nullptr, std::memory_order_relaxed);
			if (new_bucket.last) {
				new_bucket.last->next_ = link;
				new_bucket.last->active_next_.store(link, std::memory_order_relaxed);
			} else {
				new_bucket.first = link;
				new_bucket.active.store(link, std::memory_order_relaxed);
			}
			new_bucket.last = link;
		}
	}

	wd_hash_buckets_.swap(buckets);
	wd_hash_bucket_mask_ = new_bucket_mask;
}

}
