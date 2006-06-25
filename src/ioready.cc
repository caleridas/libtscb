/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/ioready>

namespace tscb {
	
	void ioready_callback_link::cancel(void) throw()
	{
		cancellation_mutex.lock();
		if (service) service->unregister_ioready_callback(this);
		else cancellation_mutex.unlock();
	}
	
	void ioready_callback_link::modify(int evmask) throw()
	{
		cancellation_mutex.lock();
		if (service) service->modify_ioready_callback(this, evmask);
		cancellation_mutex.unlock();
	}
	
	ioready_callback_link::~ioready_callback_link(void) throw()
	{
	}
	
	ioready_service::~ioready_service(void) throw()
	{
	}
	
	ioready_dispatcher::~ioready_dispatcher(void) throw()
	{
	}
	
	ioready_callback_table::chain_table::chain_table(size_t initial)
		throw(std::bad_alloc)
		: size(0), max(initial), old(0)
	{
		chains=new ioready_callback_chain[initial];
	}
	
	ioready_callback_table::chain_table::~chain_table(void) throw()
	{
		delete []chains;
	}
	
	ioready_callback_table::ioready_callback_table(size_t initial)
		throw(std::bad_alloc)
		: inactive(0)
	{
		table=new chain_table(initial);
	}
	
	ioready_callback_table::~ioready_callback_table(void) throw()
	{
		while(true) {
			if (table) break;
			chain_table *tmp=table->old;
			delete table;
			table=tmp;
		}
	}
	
	void ioready_callback_table::grow(size_t new_size) throw(std::bad_alloc)
	{
		chain_table *tmp=new chain_table(new_size);
		tmp->old=table;
		tmp->size=table->size;
		memcpy(tmp->chains, table->chains,
			sizeof(ioready_callback_chain)*table->size);
		memory_barrier();
		table=tmp;
	}
	
	void ioready_callback_table::ensure_size(size_t min_size) throw(std::bad_alloc)
	{
		if (table->max<min_size) {
			size_t target_size=table->max*2;
			if (target_size<min_size) target_size=min_size;
			grow(target_size);
		}
		if (table->size<min_size) {
			memset(table->chains+table->size, 0,
				sizeof(ioready_callback_chain)*(min_size-table->size));
		}
		table->size=min_size;
	}
	
	ioready_callback_link *ioready_callback_table::lookup_first_callback(int fd)
	{
		return table->chains[fd].active;
	}
	
	void ioready_callback_table::insert(ioready_callback_link *link) throw(std::bad_alloc)
	{
		ensure_size(link->fd+1);
		ioready_callback_chain &chain=table->chains[link->fd];
		
		/* insertion protocol */
		link->prev=chain.last;
		link->next=0;
		link->active_next=0;
		memory_barrier();
		
		/* add element to active list; find all elements that have been removed
		from the full list and thus terminate the active list; point them to
		the newly-added element */
		
		ioready_callback_link *tmp=chain.last;
		while(true) {
			if (!tmp) {
				if (!chain.active) chain.active=link;
				break;
			}
			if (tmp->active_next) break;
			tmp->active_next=link;
			tmp=tmp->prev;
		}
		
		if (chain.last) chain.last->next=link;
		else chain.first=link;
		chain.last=link;
	}
	
	void *ioready_callback_table::get_closure(int fd) throw()
	{
		return table->chains[fd].closure;
	}
	
	void ioready_callback_table::set_closure(int fd, void *closure) throw(std::bad_alloc)
	{
		ensure_size(fd+1);
		table->chains[fd].closure=closure;
	}
	
	void ioready_callback_table::remove(ioready_callback_link *link) throw()
	{
		ioready_callback_chain &chain=table->chains[link->fd];
		/* remove protocol */
		
		/* remove element from active list; we have to make
		sure that all elements that pointed to "us" within
		the active chain now point to the following element,
		so this element is skipped from within the active chain */
		ioready_callback_link *tmp=link->prev;
		while(true) {
			if (!tmp) {
				if (chain.active==link) chain.active=link->active_next;
				break;
			}
			if (tmp->active_next!=link) break;
			tmp->active_next=link->active_next;
			tmp=tmp->prev;
		}
		
		/* put on list of elements marked for deferred cancellation */
		link->inactive_next=inactive;
		inactive=link;
	}
	
	ioready_callback_link *ioready_callback_table::synchronize(void) throw()
	{
		chain_table *tab=table->old;
		/* deallocate old tables */
		table->old=0;
		while(tab) {
			chain_table *next=tab->old;
			delete tab;
			tab=next;
		}
		
		/* remove inactive callbacks */
		ioready_callback_link *link=inactive;
		while(link) {
			ioready_callback_chain &chain=table->chains[link->fd];
			if (link->prev) link->prev->next=link->next;
			else chain.first=link->next;
			if (link->next) link->next->prev=link->prev;
			else chain.last=link->prev;
			link=link->inactive_next;
		}
		
		/* return first inactive callback so they can be deallocated
		outside the lock */
		link=inactive;
		inactive=0;
		return link;
	}
	
}
