/* -*- C++ -*-
 * (c) 2010 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/config.h>
#include <tscb/fd-handler-table.h>

namespace tscb {

fd_handler_table::link_type::~link_type() noexcept
{
}

ioready_events
fd_handler_table::link_type::event_mask() const noexcept
{
	return event_mask_;
}


fd_handler_table::delayed_handler_release::~delayed_handler_release() noexcept
{
	clear();
}

fd_handler_table::delayed_handler_release::delayed_handler_release(link_type * link)
	: link_(link)
{
}

fd_handler_table::delayed_handler_release::delayed_handler_release() noexcept
	: link_(nullptr)
{
}

fd_handler_table::delayed_handler_release::delayed_handler_release(
	delayed_handler_release && other) noexcept
	: link_(other.link_)
{
	other.link_ = nullptr;
}

fd_handler_table::delayed_handler_release &
fd_handler_table::delayed_handler_release::operator=(delayed_handler_release && other) noexcept
{
	clear();
	link_ = other.link_;
	other.link_ = nullptr;
	return *this;
}

void
fd_handler_table::delayed_handler_release::clear() noexcept
{
	link_type * current = link_;
	while (current) {
		link_type * next = current->inactive_next_;
		current->fn_ = nullptr;
		intrusive_ptr_release(current);
		current = next;
	}
	link_ = nullptr;
}


void
fd_handler_table::deallocate_old_tables() noexcept
{
	table * tab = table_.load(std::memory_order_relaxed);
	table * old = tab->old_;
	tab->old_ = nullptr;
	while (old) {
		table * next = old->old_;
		delete old;
		old = next;
	}
}

ioready_events
fd_handler_table::chain::compute_event_mask() const noexcept
{
	ioready_events mask = ioready_none;

	link_type * link = active_.load(std::memory_order_relaxed);
	while (link) {
		mask |= link->event_mask();
		link = link->active_next_.load(std::memory_order_relaxed);
	}

	return mask;
}

fd_handler_table::table::table(size_t capacity) /* throw(std::bad_alloc) */
	: capacity_(capacity), entries_(new std::atomic<chain *>[capacity]), old_(nullptr)
{
	for (std::size_t n = 0; n < capacity; ++n) {
		entries_[n].store(nullptr, std::memory_order_relaxed);
	}
}

void
fd_handler_table::table::clear_entries() noexcept
{
	for (size_t n = 0; n < capacity_; ++n) {
		delete entries_[n].load(std::memory_order_relaxed);
	}
}

fd_handler_table::~fd_handler_table() noexcept
{
	table * tab = table_.load(std::memory_order_consume);
	tab->clear_entries();
	while (tab) {
		table * tmp = tab->old_;
		delete tab;
		tab = tmp;
	}
}

fd_handler_table::fd_handler_table(size_t initial)  /*throw(std::bad_alloc)*/
	: table_(new table(initial))
	, inactive_(nullptr)
	, cookie_(0)
	, need_cookie_sync_(false)
{
}

/* must be called under write lock */
void
fd_handler_table::insert(
	link_type * link,
	ioready_events & old_mask,
	ioready_events & new_mask) /*throw(std::bad_alloc)*/
{
	chain * ch = get_create_chain(link->fd_);

	/* Note: from this point onwards, no memory allocation is performed anymore,
	 * no exception can be thrown. */
	intrusive_ptr_add_ref(link);

	/* compute old/new event mask */
	old_mask = ioready_none;
	link_type * tmp = ch->active_.load(std::memory_order_relaxed);
	while (tmp) {
		old_mask |= tmp->event_mask_;
		tmp = tmp->active_next_.load(std::memory_order_relaxed);
	}
	new_mask = old_mask | link->event_mask_;

	/* prepare element */
	link->prev_ = ch->last_;
	link->next_ = nullptr;
	link->active_next_.store(nullptr, std::memory_order_relaxed);

	/* we are now going to "publish" this element; since we may be
	inserting multiple references, just issue a thread fence once
	and use relaxed memory order */
	atomic_thread_fence(std::memory_order_release);

	/* add element to active list; find all elements that have been removed
	from the full list and thus terminate the active list; point them to
	the newly-added element */

	tmp = ch->last_;

	for (;;) {
		if (!tmp) {
			if (ch->active_.load(std::memory_order_relaxed) == 0) {
				ch->active_.store(link, std::memory_order_relaxed);
			}
			break;
		}
		if (tmp->active_next_.load(std::memory_order_relaxed)) {
			break;
		}
		tmp->active_next_.store(link, std::memory_order_relaxed);
		tmp = tmp->prev_;
	}

	if (ch->last_) {
		ch->last_->next_ = link;
	} else {
		ch->first_ = link;
	}

	ch->last_ = link;
}

/* must be called under write lock */
void
fd_handler_table::remove(
	link_type * link, ioready_events & old_mask, ioready_events & new_mask) noexcept
{
	chain * ch = get_chain(link->fd_);

	/* remove protocol: remove element from active list; we have to make
	sure that all elements that pointed to "us" within
	the active chain now point to the following element,
	so this element is skipped from within the active chain */
	link_type * tmp = link->prev_;
	link_type * next = link->active_next_.load(std::memory_order_relaxed);
	for (;;) {
		if (!tmp) {
			if (ch->active_.load(std::memory_order_relaxed) == link) {
				ch->active_.store(next, std::memory_order_release);
			}
			break;
		}
		if (tmp->active_next_.load(std::memory_order_relaxed) != link) {
			break;
		}
		tmp->active_next_.store(next, std::memory_order_release);
		tmp = tmp->prev_;
	}

	/* compute old/new event masks */
	new_mask = ioready_none;
	tmp = ch->active_.load(std::memory_order_relaxed);
	while (tmp) {
		new_mask |= tmp->event_mask_;
		tmp = tmp->active_next_.load(std::memory_order_relaxed);
	}
	old_mask = new_mask | link->event_mask_;

	/* If this is the last callback registered for this descriptor,
	 * then user program might close and reuse it immediately. This
	 * could lead to a pending event for the old descriptor to be
	 * delivered and interpreted as an event for the new chain. Guard
	 * against this by incrementing global cookie and assigning it to
	 * the chain: Any event with "lower" cookie value will be disregarded
	 * as belonging to the old descriptor */
	if (ch->active_.load(std::memory_order_relaxed) == 0) {
		uint32_t old_cookie = cookie_.fetch_add(1, std::memory_order_relaxed);
		uint32_t new_cookie = old_cookie + 1;
		ch->cookie_.store(new_cookie, std::memory_order_relaxed);
		if (((old_cookie ^ new_cookie) & (1<<16)) != 0) {
			need_cookie_sync_ = true;
		}
	}

	/* put into list of elements marked for deferred deletion */
	link->inactive_next_ = inactive_;
	inactive_ = link;
}

void
fd_handler_table::modify(
	link_type * link,
	ioready_events mask,
	ioready_events & old_mask,
	ioready_events & new_mask) noexcept
{
	chain * ch = get_chain(link->fd_);
	old_mask = ch->compute_event_mask();
	link->event_mask_ = mask;
	new_mask = ch->compute_event_mask();
}

bool
fd_handler_table::disconnect_all() noexcept
{
	bool any_disconnected = false;
	table * tab = table_.load(std::memory_order_consume);
	for (size_t n = 0; n < tab->capacity_; n++) {
		chain * ch = tab->entries_[n].load(std::memory_order_consume);
		if (!ch) {
			continue;
		}
		link_type * link = ch->active_.load(std::memory_order_consume);
		while (link) {
			any_disconnected = true;
			link->disconnect();
			link = ch->active_.load(std::memory_order_consume);
		}
	}

	return any_disconnected;
}

ioready_events
fd_handler_table::compute_event_mask(int fd) noexcept
{
	chain * ch = get_chain(fd);
	return ch ? ch->compute_event_mask() : ioready_none;
}

void
fd_handler_table::notify(int fd, ioready_events events, uint32_t call_cookie)
{
	std::size_t index = static_cast<std::size_t>(fd);
	table * tab = table_.load(std::memory_order_consume);
	if (index >= tab->capacity_) {
		return;
	}

	chain * ch = tab->entries_[index].load(std::memory_order_consume);
	if (!ch) {
		return;
	}

	int32_t delta = ch->cookie_.load(std::memory_order_relaxed) - call_cookie;
	if (delta > 0) {
		return;
	}

	link_type * link = ch->active_.load(std::memory_order_consume);
	while (link) {
		if ((events & link->event_mask_) != 0) {
			link->fn_(events & link->event_mask_);
		}
		link = link->active_next_.load(std::memory_order_consume);
	}
}

fd_handler_table::chain *
fd_handler_table::get_create_chain(int fd) /* throw(std::bad_alloc) */
{
	if (fd < 0) {
		throw std::bad_alloc();
	}
	std::size_t index = static_cast<std::size_t>(fd);

	table * tab = table_.load(std::memory_order_relaxed);
	if (index >= tab->capacity_) {
		tab = extend_table(tab, index + 1);
	}

	chain * ch = tab->entries_[index].load(std::memory_order_relaxed);
	if (!ch) {
		ch = new chain();
		tab->entries_[index].store(ch, std::memory_order_release);
	}

	return ch;
}

fd_handler_table::chain *
fd_handler_table::get_chain(int fd) noexcept
{
	std::size_t index = static_cast<std::size_t>(fd);
	table * tab = table_.load(std::memory_order_relaxed);
	return index < tab->capacity_ ? tab->entries_[index].load(std::memory_order_relaxed) : nullptr;
}

fd_handler_table::table *
fd_handler_table::extend_table(table * tab, std::size_t required_capacity) /*throw(std::bad_alloc)*/
{
	std::size_t new_capacity = std::max(tab->capacity_ * 2, required_capacity);
	table * newtab = new table(new_capacity);
	for (size_t n = 0; n < tab->capacity_; ++n) {
		newtab->entries_[n].store(tab->entries_[n].load(std::memory_order_relaxed), std::memory_order_relaxed);
	}

	newtab->old_ = tab;

	table_.store(newtab, std::memory_order_release);
	tab = newtab;

	return tab;
}

fd_handler_table::delayed_handler_release
fd_handler_table::synchronize() noexcept
{
	deallocate_old_tables();
	table * tab = table_.load(std::memory_order_relaxed);

	/* remove inactive callbacks */
	link_type * link = inactive_;
	while (link) {
		chain * ch = tab->entries_[link->fd_].load(std::memory_order_relaxed);
		if (link->prev_) {
			link->prev_->next_ = link->next_;
		} else {
			ch->first_ = link->next_;
		}
		if (link->next_) {
			link->next_->prev_ = link->prev_;
		} else {
			ch->last_ = link->prev_;
		}
		link = link->inactive_next_;
	}

	if (need_cookie_sync_) {
		need_cookie_sync_ = false;
		uint32_t current_cookie = cookie_.load(std::memory_order_relaxed);
		for (size_t n = 0; n < tab->capacity_; ++n) {
			chain * ch = tab->entries_[n];
			if (ch) {
				ch->cookie_.store(current_cookie, std::memory_order_relaxed);
			}
		}
	}

	/* return first inactive callback so they can be deallocated
	outside the lock */
	link = inactive_;
	inactive_ = nullptr;

	return delayed_handler_release(link);
}

}
