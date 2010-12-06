/* -*- C++ -*-
 * (c) 2010 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <string.h>
#include <tscb/config>
#include <tscb/file-descriptor-table>

namespace tscb {
	
	/* must be called under read lock */
	void file_descriptor_table::cancel_all(void) throw()
	{
		volatile_table * tab = table.load(memory_order_consume);
		for(size_t n=0; n<tab->capacity; n++) {
			for(;;) {
				ioready_callback * cb = tab->entries[n].active.load(memory_order_relaxed);
				if (!cb) break;
				cb->disconnect();
			}
		}
	}
	
	/* must be called under write lock */
	void file_descriptor_table::insert(ioready_callback * cb, ioready_events & old_mask, ioready_events & new_mask) /*throw(std::bad_alloc)*/
	{
		volatile_table * tab = get_extend_table(cb->fd);
		file_descriptor_chain & entry = tab->entries[cb->fd];
		
		/* compute old event mask */
		old_mask = ioready_none;
		ioready_callback * tmp = entry.active.load(memory_order_relaxed);
		while(tmp) {
			old_mask |= tmp->event_mask;
			tmp = tmp->active_next.load(memory_order_relaxed);
		}
		new_mask = old_mask | cb->event_mask;
		
		/* prepare element */
		cb->prev = entry.last;
		cb->next = 0;
		cb->active_next.store(0, memory_order_relaxed);
		
		/* we are now going to "publish" this element; since we may be
		inserting multiple references, just issue a thread fence once
		and use relaxed memory order */
		atomic_thread_fence(memory_order_release);
	
		/* add element to active list; find all elements that have been removed
		from the full list and thus terminate the active list; point them to
		the newly-added element */
		
		tmp = entry.last;
		
		for(;;) {
			if (!tmp) {
				if (entry.active.load(memory_order_relaxed) == 0)
					entry.active.store(cb, memory_order_relaxed);
				break;
			}
			if (tmp->active_next.load(memory_order_relaxed)) break;
			tmp->active_next.store(cb, memory_order_relaxed);
			tmp=tmp->prev;
		}
		
		if (entry.last) entry.last->next = cb;
		else entry.first = cb;
		entry.last = cb;
	}
	
	/* must be called under write lock */
	void file_descriptor_table::remove(ioready_callback * cb, ioready_events & old_mask, ioready_events & new_mask) throw()
	{
		volatile_table * tab = table.load(memory_order_relaxed);
		file_descriptor_chain & entry = tab->entries[cb->fd];
		
		/* remove protocol: remove element from active list; we have to make
		sure that all elements that pointed to "us" within
		the active chain now point to the following element,
		so this element is skipped from within the active chain */
		ioready_callback * tmp = cb->prev;
		ioready_callback * next = cb->active_next.load(memory_order_relaxed);
		for(;;) {
			if (!tmp) {
				if (entry.active.load(memory_order_relaxed) == cb)
					entry.active.store(next, memory_order_release);
				break;
			}
			if (tmp->active_next.load(memory_order_relaxed) != cb) break;
			tmp->active_next.store(next, memory_order_release);
			tmp = tmp->prev;
		}
		
		/* compute old event mask */
		new_mask = ioready_none;
		tmp = entry.active.load(memory_order_relaxed);
		while(tmp) {
			old_mask |= tmp->event_mask;
			tmp = tmp->active_next.load(memory_order_relaxed);
		}
		old_mask = new_mask | cb->event_mask;
		
		/* put into list of elements marked for deferred cancellation */
		cb->inactive_next = inactive;
		inactive = cb;
	}
	
	/* must be called after read_unlock/write_lock indicates that synchronization
	is required */
	ioready_callback * file_descriptor_table::synchronize(void) throw()
	{
		volatile_table * tab = table.load(memory_order_relaxed);
		volatile_table * old = tab->old;
		/* deallocate old tables */
		tab->old=0;
		while(old) {
			volatile_table * next = old->old;
			delete old;
			old = next;
		}
		
		/* remove inactive callbacks */
		ioready_callback * link = inactive;
		while(link) {
			file_descriptor_chain & entry = tab->entries[link->fd];
			if (link->prev) link->prev->next = link->next;
			else entry.first = link->next;
			if (link->next) link->next->prev = link->prev;
			else entry.last = link->prev;
			link = link->inactive_next;
		}
		
		/* return first inactive callback so they can be deallocated
		outside the lock */
		link = inactive;
		inactive = 0;
		return link;
	}
		
	file_descriptor_table::volatile_table *
	file_descriptor_table::get_extend_table_slow(volatile_table * tab, int maxfd) /*throw(std::bad_alloc)*/
	{
		size_t new_capacity = tab->capacity * 2;
		if (new_capacity <= (size_t)maxfd) new_capacity = maxfd + 1;
		
		volatile_table * newtab = new volatile_table(new_capacity);
		for(size_t n = 0; n<tab->capacity; n++) newtab->entries[n] = tab->entries[n];
		newtab->old = tab;
		
		table.store(newtab, memory_order_release);
		tab = newtab;
		
		return tab;
	}
	
}
