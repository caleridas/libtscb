/*
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.
 * Refer to the file "COPYING" for details.
 */

#include <tscb/deferred>

namespace tscb {
	
	bool deferred_rwlock::read_lock_slow(void) throw()
	{
		writers.lock();
		if (read_acquire()) {
			writers.unlock();
			return false;
		}
		
		return true;
	}
		
	bool deferred_rwlock::read_unlock_slow(void) throw()
	{
		writers.lock();
		/* note: if another thread obsevers 1->0 transition, it will
		take the mutex afterwards (and thus serialize with us)
		
		conversely, a 0->1 transition can only happen with the
		mutex held; therefore, the acquire/release implicit in
		the mutex is sufficient to enforce memory ordering here */
		if (readers.load(memory_order_relaxed)!=0) {
			writers.unlock();
			return false;
		}
		
		return true;
	}
	
	bool deferrable_rwlock::read_lock_slow(void) throw()
	{
		writers.lock();
		while (waiting) {
			waiting=false;
			writers.unlock();
			waiting_writers.broadcast();
			writers.lock();
		}
		if (read_acquire()) {
			writers.unlock();
			return false;
		}
		
		return true;
	}
		
	bool deferrable_rwlock::read_unlock_slow(void) throw()
	{
		writers.lock();
		while (waiting) {
			waiting=false;
			writers.unlock();
			waiting_writers.broadcast();
			writers.lock();
		}
		/* note: if another thread obsevers 1->0 transition, it will
		take the mutex afterwards (and thus serialize with us)
		
		conversely, a 0->1 transition can only happen with the
		mutex held; therefore, the acquire/release implicit in
		the mutex is sufficient to enforce memory ordering here */
		if (readers.load(memory_order_relaxed)!=0) {
			writers.unlock();
			return false;
		}
		
		return true;
	}
	
}
