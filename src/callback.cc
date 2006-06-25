/* -*- C++ -*-
 * (c) 2004 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/callback>

#include <stdio.h>

namespace tscb {
	
	callback_link::~callback_link(void) throw()
	{
	}
	
	void callback_link::cancelled(void) throw()
	{
	}
	
	function_callback_link::function_callback_link(void) throw()
	{
		prev=next=active_next=deferred_cancel_next=0;
		_chain=0;
	}
	
	function_callback_link::~function_callback_link(void) throw()
	{
	}
	
	void function_callback_link::cancel(void) throw()
	{
		registration_mutex.lock();
		if (_chain) _chain->remove(this);
		else registration_mutex.unlock();
	}
	
	callback_chain::callback_chain(void) throw()
	{
		first=last=active=deferred_cancel=0;
	}
	
	callback_chain::~callback_chain(void) throw()
	{
		/* not sure there is any point in locking here... if the object
		is destroyed and anyone is trying to add or remove a callback
		at this moment we are hosed anyways */
		
		while(guard.read_lock()) synchronize();
		while (active) active->cancel();
		if (guard.read_unlock()) {
			/* the above cancel operations will cause synchronization
			to be performed at the next possible point in time; if
			there is no concurrent cancellation, this is now */
			synchronize();
		} else {
			/* this can only happen if some callback link was
			cancelled while this object is being destroyed; in
			that case we have to suspend the thread that is destroying
			the object until we are certain that synchronization has
			been performed */
			
			guard.write_lock_sync();
			synchronize();
			
			/* note that synchronize implicitly calls sync_finished,
			which is equivalent to write_unlock_sync for deferrable_rwlocks */
		}
	}
	
	void callback_chain::add(function_callback_link *l) throw()
	{
		l->registration_mutex.lock();
		l->pin();
		bool sync=guard.write_lock_async();
		
		l->next=0;
		l->prev=last;
		
		l->active_next=0;
		memory_barrier();
		
		/* add element to active list; find all elements that have been removed
		from the full list and thus terminate the active list; point them to
		the newly-added element */
		
		function_callback_link *tmp=last;
		while(true) {
			if (!tmp) {
				if (!active) active=l;
				break;
			}
			if (tmp->active_next) break;
			tmp->active_next=l;
			tmp=tmp->prev;
		}
		
		/* insert into list of all elements*/
		if (last) last->next=l;
		else first=l;
		last=l;
		
		l->_chain=this;
		
		l->registration_mutex.unlock();
		
		if (sync) synchronize();
		else guard.write_unlock_async();
	}
	
	void callback_chain::remove(function_callback_link *l) throw()
	{
		bool sync=guard.write_lock_async();
		if (l->_chain==this) {
			/* remove element from active list; we have to make
			sure that all elements that pointed to "us" within
			the active chain now point to the following element,
			so this element is skipped from within the active chain */
			
			function_callback_link *tmp=l->prev;
			while(true) {
				if (!tmp) {
					if (active==l) active=l->active_next;
					break;
				}
				if (tmp->active_next!=l) break;
				tmp->active_next=l->active_next;
				tmp=tmp->prev;
			}
			
			/* put on list of elements marked for deferred cancellation */
			l->deferred_cancel_next=deferred_cancel;
			deferred_cancel=l;
			
			/* remove pointer to chain, so a second call to ->cancel()
			will do nothing */
			l->_chain=0;
		}
		
		l->registration_mutex.unlock();
		
		if (sync) synchronize();
		else guard.write_unlock_async();
	}
	
	void callback_chain::synchronize(void) throw()
	{
		function_callback_link *do_cancel=deferred_cancel;
		
		/* first, "repair" the list structure by "correcting" all prev
		pointers */
		while(do_cancel) {
			/* we can now safely remove the elements from the list */
			if (do_cancel->prev) do_cancel->prev->next=do_cancel->next;
			else first=do_cancel->next;
			if (do_cancel->next) do_cancel->next->prev=do_cancel->prev;
			else last=do_cancel->prev;
			
			do_cancel=do_cancel->deferred_cancel_next;
		}
		
		/* now swap pointers while still under the lock; this is
		necessary to make sure that the destructor for each
		callback link object is called exactly once */
		do_cancel=deferred_cancel;
		deferred_cancel=0;
		guard.sync_finished();
		
		/* now we can release the callbacks, as we are sure that no one
		can "see" them anymore */
		
		while(do_cancel) {
			function_callback_link *tmp=do_cancel->deferred_cancel_next;
			do_cancel->cancelled();
			do_cancel->release();
			do_cancel=tmp;
		}
	}
	
}; /* namespace callback */
