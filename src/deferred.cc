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
		if (readers.inc_if_not_zero()) {
			writers.unlock();
			return false;
		}
		
		return true;
	}
		
	bool deferred_rwlock::read_unlock_slow(void) throw()
	{
		writers.lock();
		if (readers!=0) {
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
		if (readers.inc_if_not_zero()) {
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
		if (readers!=0) {
			writers.unlock();
			return false;
		}
		
		return true;
	}
	
}
