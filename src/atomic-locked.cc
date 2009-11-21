/* -*- C++ -*-
 * (c) 2009 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <stdint.h>

#include <tscb/thread>
#include <tscb/atomic-locked>

#ifdef TSCB_FALLBACK_ATOMIC

namespace tscb {
	
	namespace atomics {
		
		static mutex atomic_locks[16];
		
		static inline mutex &get_atomic_lock(const volatile void *ptr, memory_order order) throw()
		{
			uint32_t v;
			if (order!=memory_order_seq_cst) {
				v=(intptr_t)const_cast<void *>(ptr);
				v=v^(v>>16);
				v=v^(v>>8);
				v=v^(v>>4);
			} else v=0;
			return atomic_locks[v&15];
		}
		
		class scoped_atomic_lock {
		public:
			scoped_atomic_lock(const volatile void *ptr, memory_order order) throw()
				: m(get_atomic_lock(ptr, order))
			{m.lock();}
			~scoped_atomic_lock(void) throw() {m.unlock();}
		private:
			mutex &m;
		};
		
		void atomic_int::store(int __i, memory_order order) volatile throw()
		{
			scoped_atomic_lock guard(&i, order);
			i=__i;
		}
			
		int
		atomic_int::load(memory_order order) const volatile throw()
		{
			scoped_atomic_lock guard(&i, order);
			return i;
		}
		
		bool atomic_int::compare_exchange_strong(int &expected, int desired,
			memory_order order) volatile throw()
		{
			scoped_atomic_lock guard(&i, order);
			if (i==expected) {
				i=desired;
				return true;
			} else {
				expected=i;
				return false;
			}
		}
		
		int atomic_int::fetch_add(int c, memory_order order) volatile throw()
		{
			scoped_atomic_lock guard(&i, order);
			int old=i;
			i=i+c;
			return old;
		}
		
		void fence(memory_order order)
		{
			atomic_locks[0].lock();
			atomic_locks[0].unlock();
		}
		
	}
	
}

#endif
