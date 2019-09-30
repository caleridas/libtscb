/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_DEFERRED_H
#define TSCB_DEFERRED_H

/**
	\page deferred_descr Deferred synchronization

	Deferred synchronization allows concurrency / reentrancy between
	operations that would normally allow strict serialization, e.g.
	traversal of a linked list vs. removal of an element. The key idea is
	to allow only "safe" mutations that preserve invariants essential for
	another operation (e.g. elements of a list are not deallocated, forward
	pointers retain their value). Destructive synchronization (e.g.
	deallocating objects) is deferred to points in time when no other
	operation is in progress. This is similar to RCU-style synchronization,
	but works without global tracking of thread states (at the expense
	of providing no obstruction-freedom guarantees).

	Two classes implement this kind of deferred synchronization:

	<UL>
		<LI>\ref tscb::deferred_rwlock allows only deferred synchronization</LI>
		<LI>\ref tscb::deferrable_rwlock allows both deferred and non-deferred synchronization</LI>
	</UL>

	They provide the following guarantees:

	<UL>
		<LI>
			acquiring multiple nested read-locks in arbitrary order is
			deadlock-free
		</LI>
		<LI>
			acquiring a single deferred write-lock while holding multiple
			read-locks is deadlock-free
		</LI>
		<LI>
			acquiring a single non-deferred write-lock while holding no
			other read-locks or write-locks is deadlock free
		</LI>
	</UL>

	All cases of lock nesting not covered above depend on external
	constraints wrt to lock acquisition to determine if they are
	deadlock-free or not.

	The implementation is optimized under the assumption that access for
	reading is significantly more commen than access for writing.

	\section deferred_sync_simple Using deferred updates

	Read access to a data structure protected by this kind of synchronization
	mechanism is granted by calling the <CODE>read_lock</CODE> and
	<CODE>read_unlock</CODE> methods around the relevant code blocks.
	Both methods return a boolean indicating whether synchronization
	is required:

	\code
		tscb::deferred_rwlock guard;
		...
		while (guard.read_lock()) {
			// synchronization should be done now, perform necessary
			// actions to apply queued up modifications
			guard.sync_finished();
		}
		...
		// now perform read access to protected data structure
		...
		if (guard.read_unlock()) {
			// synchronization should be done now, perform necessary
			// actions to apply queued up modifications
			guard.sync_finished();
		}
	\endcode

	Notice that <CODE>read_lock</CODE> has to be retried in a loop (but
	see below), while <CODE>read_unlock</CODE> will always "succeed" in a sense.
	It is important to note that <CODE>sync_finished</CODE> <B>must</B> be
	called after any of these methods returns true -- until
	<CODE>sync_finished</CODE> is called, both write and read access from
	any other container will block!

	On the write side, deferred synchronization is provided by calling
	<CODE>write_lock_async</CODE> and <CODE>write_unlock_async</CODE>:

	\code
		tscb::deferred_rwlock guard;
		...
		bool synchronous=guard.write_lock_async();
		...
		// it is guaranteed that no writers are active, but readers
		// may still be active
		// desired modifications should be queued up
		...
		if (synchronous) {
			// the lock was acquired synchronously -- no readers
			// were active when the lock was acquired, and it is
			// in this special case guaranteed that no readers
			// are active now
			// all queued up modifications can and must be applied now
			guard.sync_finished();
		} else {
			// cannot currently apply modifications, have to defer them
			guard.write_unlock_async();
			// a subsequent read_lock, read_unlock or write_lock_async
			// is promised to return "true", and synchronization must
			// be deferred until then
		}
	\endcode

	Again the return code of <CODE>write_lock_async</CODE> indicates whether
	synchronization is required and possible. Note that the easiest way
	is to proceed as in the code shown below: Always enqueue modifications,
	and only in the end decide to apply modifications immediately.

	Notice that the locking primitives are agnostic as to the structure
	of the data protected and also do not care how updates are being
	kept track of -- this is the responsibility of the caller! An
	example demonstrating the concept for a simple linked list might
	look as follows:

	\code
		class ListElement {
		public:
			ListElement *prev, *next;
			bool to_be_removed;
		};

		class List {
		public:
			void traverse_list()
			{
				while (guard.read_lock()) synchronize();

				ListElement *current=first;
				while (current) {
					... // do something useful
					current=current->next;
				}

				if (guard.read_unlock()) synchronize();
			}
			void erase_element(ListElement *element)
			{
				bool sync=guard.write_lock_async();

				element->to_be_removed=true;

				if (sync) synchronize();
				else guard.write_unlock_async();
			}
		private:
			void synchronize()
			{
				ListElement *current=first;
				while (current) {
					ListElement *tmp=current->next;
					if (current->to_be_removed) delete current;
					current=tmp;
				}
				guard.sync_finished();
			}
			ListElement *first, *last;
			tscb::deferred_rwlock guard;
		};
	\endcode

	(Note: in the example just above it might be a smart idea to mark
	<CODE>to_be_removed</CODE> "volatile" and check its value while traversing
	the	list).

	It is <I>possible</I> to use \ref tscb::deferred_rwlock::read_lock in
	a fashion similiar to \ref tscb::deferred_rwlock::write_lock_async --
	that is, not retry the locking operation and distinguish by the
	return value only on unlock. However this opens up all lock-nesting
	problems the implementation seeks to eliminate, so this is best
	avoided.

	\section deferrable_sync_simple Using deferrable updates

	Using \ref tscb::deferrable_rwlock instead of \ref tscb::deferred_rwlock
	allows to synchronously grab the write lock:

	\code
		tscb::deferrable_sync guard;
		...
		guard.write_lock_sync();
		// it is guaranteed that neither reader nor writer can hold
		// the lock now
		...
		// necessary operations to synchronize data structure must be
		// performed hede
		guard.write_unlock_sync();
	\endcode

	The call to <CODE>write_lock_sync</CODE> will <I>block</I> as long as
	there is at least one reader who is holding a read lock, instead
	of just returning "false" as <CODE>write_lock_async</CODE> does.

	The main purpose of the synchronous write lock versions is to allow
	the caller to force a defined synchronization point, mainly to avoid
	the starvation problem the fully asynchronous version has. It is
	possible, though discouraged, to "abuse" this lock as an ordinary
	(blocking) read-/write-lock.

	\section Performance

	The implementation has been optimized towards the read-path. For the
	case that there are no queued modifications to be processed, the
	overhead is as follows:

	<UL>
		<LI>
			<CODE>read_lock</CODE>: one successful atomic test-and-increment
			operation
		</LI>
		<LI>
			<CODE>read_unlock</CODE>: one atomic
			decrement-and-test operation where the counter does not return to
			zero
		</LI>
	</UL>

	For the write locks and for the read locks with contention (modifications
	have been queued up asynchronously) the additional overhead consists of
	one mutex acquisiton/release.

	For the \ref tscb::deferrable_rwlock case the additional cost in the
	contention cases is a broadcast operation on a condition variable,
	and waiting on the variable for the synchronous locks.

	Since the atomic variable is modified everytime a read lock is acquired,
	the implementation does <I>not</I> provide the best possible level
	of concurrency for multiple readers in different threads that frequently
	acquire and release this lock. For this reason, acquisition and release
	of the read lock is not significantly faster than an ordinary mutex, so
	it is advantageous <I>only</I> in cases multiple mutex lock/release
	cycles can be consolidated into a single locking call. For all
	other purposes, ordinary mutexes should be used as they are
	easier to understand.

*/

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace tscb {

/**
	\brief Deferred reader-/writer synchronization

	This class provides methods to implement deferred reader-/writer-
	synchronization. This means that both readers and writers
	are allowed (almost) unimpeded access, and synchronization is
	achieved by queueing up modifications that where made while
	readers were still active.

	Perhaps the most useful application of this kind of synchronization
	is that it is not required to worry about locking order; read and
	write accesses to different data structures protected by this
	mechanism can be acquired and released in arbitrary order. The
	downside is that there are no guarantees as to fairness, and
	write operations may potentially be starved indefinitely
	(even though writers are not blocked, technically).
*/
class deferred_rwlock {
public:
	inline deferred_rwlock()
		: readers_(1), queued_(false)
	{
	}

	/**
		\brief Try to acquire read lock

		Tries to acquire a read lock; read locks are "shared", that is
		multiple readers may hold a read lock at the same time. A
		read lock excludes synchronous writers.

		If this function returns "true", then the lock is now in
		"synchronizing" state: all queued up modifications must be
		applied, after that \ref sync_finished must be called and
		finally the \ref read_lock must be retried.

		If this function returns "false", then the caller may simply
		proceed.
	*/
	inline bool read_lock() noexcept
	{
		if (read_acquire()) {
			return false;
		} else {
			return read_lock_slow();
		}
	}

	/**
		\brief Release read lock

		Releases a previously held read lock acquired by \ref read_lock.

		If this function returns "true", then the lock is now in
		"synchronizing" state: all queued up modifications must
		be applied, after that \ref sync_finished must be called.

		If this function returns "false", then the caller may simply
		proceed.
	*/
	inline bool read_unlock() noexcept
	{
		if (read_release()) {
			return false;
		} else {
			return read_unlock_slow();
		}
	}

	/**
		\brief Try to acquire write lock

		Tries to acquire a write lock; write locks are "exclusive", that
		is they exclude both other readers and writers.

		If this function returns "true", then the lock is now in
		"synchronizing" state: no other reader or writer can hold
		the lock at this time, so it is permissible to do synchronous
		modification of the data structure. All queued up modifications
		must be applied, after that \ref sync_finished must be called
		<B>instead of</B> \ref write_unlock_async to release the lock.

		If this function returns false, then it was not possible
		to acquire an exclusive lock, intended modifications have
		to be queued up and cannot be applied directly. After
		the modifications have been noted, \ref write_unlock_async must
		be called. It is guaranteed that some subsequent call
		of \ref read_unlock will return true, so that modifications
		can be applied.
	*/
	inline bool write_lock_async()
	{
		writers_.lock();
		if (!queued_) {
			queued_ = true;
			return readers_.fetch_sub(1, std::memory_order_acquire) == 1;
		}
		return false;
	}

	/**
		\brief Release write lock

		Releases a write lock previously acquired through \ref write_lock_async.
		Note that this function <B>may not</B> be called if \ref write_lock_async
		has returned true; call \ref sync_finished instead.
	*/
	inline void write_unlock_async()
	{
		writers_.unlock();
	}

	/**
		\brief Synchronization completed

		Releases the lock out of "synchronizing" state, i.e. the state
		that has been entered when any of the following functions
		has returned true: \ref read_lock, \ref read_unlock, \ref write_lock_async.
	*/
	inline void sync_finished()
	{
		queued_ = false;
		readers_.fetch_add(1, std::memory_order_release);
		write_unlock_async();
	}

private:
	/* out of line slow-path functions */
	bool read_lock_slow() noexcept;
	bool read_unlock_slow() noexcept;

	inline bool read_acquire() noexcept
	{
		size_t expected;
		bool success;
		do {
			expected = readers_.load(std::memory_order_relaxed);
			if (expected == 0) {
				return false;
			}
			success = readers_.compare_exchange_strong(expected, expected + 1, std::memory_order_acquire);
		} while (!success);
		return true;
	}

	inline bool read_release() noexcept
	{
		return readers_.fetch_sub(1, std::memory_order_release) != 1;
	}

	std::atomic<size_t> readers_;
	std::mutex writers_;
	bool queued_;
};

/**
	\brief Deferrable reader-/writer synchronization

	This class provides methods to implement deferred reader-/writer-
	synchronization. This means that both readers and writers
	are allowed (almost) unimpeded access, and synchronization is
	achieved by queueing up modifications that where made while
	readers were still active.

	Perhaps the most useful application of this kind of synchronization
	is that it is not required to worry about locking order; read and
	write accesses to different data structures protected by this
	mechanism can be acquired and released in arbitrary order. The
	downside is that there are no guarantees as to fairness or
	starvation.

	In addition to the operations supported by \ref deferred_rwlock it is
	also possible to synchronously acquire a write lock. Though this is
	sometimes required, it is in this case necessary to carefully think
	about locking order.
*/
class deferrable_rwlock {
public:
	inline deferrable_rwlock()
		: readers_(1), queued_(false), waiting_(false)
	{
	}

	/**
		\brief Try to acquire read lock

		Tries to acquire a read lock; read locks are "shared", that is
		multiple readers may hold a read lock at the same time. A
		read lock excludes synchronous writers.

		If this function returns "true", then the lock is now in
		"synchronizing" state: all queued up modifications must be
		applied, after that \ref sync_finished must be called and
		finally the \ref read_lock must be retried.

		If this function returns "false", then the caller may simply
		proceed.
	*/
	inline bool read_lock() noexcept
	{
		if (read_acquire()) return false;
		return read_lock_slow();
	}

	/**
		\brief Release read lock

		Releases a previously held read lock acquired by \ref read_lock.

		If this function returns "true", then the lock is now in
		"synchronizing" state: all queued up modifications must
		be applied, after that \ref sync_finished must be called.

		If this function returns "false", then the caller may simply
		proceed.
	*/
	inline bool read_unlock() noexcept
	{
		if (read_release()) return false;
		return read_unlock_slow();
	}

	/**
		\brief Try to acquire write lock

		Tries to acquire a write lock; write locks are "exclusive", that
		is they exclude both other readers and writers.

		If this function returns "true", then the lock is now in
		"synchronizing" state: no other reader or writer can hold
		the lock at this time, so it is permissible to do synchronous
		modification of the data structure. All queued up modifications
		must be applied, after that \ref sync_finished must be called
		<B>instead of</B> \ref write_unlock_async	to release the lock.

		If this function returns false, then it was not possible
		to acquire an exclusive lock, intended modifications have
		to be queued up and cannot be applied directly. After
		the modifications have been noted, \ref write_unlock_async must
		be called. It is guaranteed that some subsequent call
		of \ref read_unlock will return true, so that modifications
		can be applied.
	*/
	inline bool write_lock_async()
	{
		writers_.lock();
		bool sync = false;

		if ((!queued_) && (!waiting_)) {
			sync = readers_.fetch_sub(1, std::memory_order_acquire) == 1;
		}
		queued_ = true;

		return sync;
	}

	/**
		\brief Release write lock

		Releases a write lock previously acquired through \ref write_lock_async.
		Note that this function <B>may not</B> be called if \ref write_lock_async
		has returned true; call \ref sync_finished instead.
	*/
	inline void write_unlock_async()
	{
		writers_.unlock();
	}

	/**
		\brief synchronously acquire write lock

		Acquires a write lock. This operation blocks until no other
		readers or writers are active. After the caller is finished
		he must call \ref write_unlock_sync.
	*/
	inline std::unique_lock<std::mutex> write_lock_sync()
	{
		std::unique_lock<std::mutex> guard(writers_);
		for(;;) {
			if ((!queued_) && (!waiting_)) {
				if (readers_.fetch_sub(1, std::memory_order_acquire) == 1) {
					return guard;
				}
			}
			waiting_ = true;
			waiting_writers_.wait(guard);
		}
		return guard;
	}

	/**
		\brief release write lock

		Requires a write lock previously acquired by \ref write_lock_sync.
		Note that this operation is completely equivalent to
		\ref sync_finished.
	*/
	inline void write_unlock_sync(std::unique_lock<std::mutex> guard)
	{
		queued_ = false;
		waiting_ = false;
		readers_.fetch_sub(1, std::memory_order_release);
		guard.unlock();
	}

	/**
		\brief Synchronization completed

		Releases the lock out of "synchronizing" state, i.e. the state
		that has been entered when any of the following functions
		has returned true: \ref read_lock, \ref read_unlock,
		\ref write_lock_async.
	*/
	inline void sync_finished()
	{
		queued_ = false;
		waiting_ = false;
		readers_.fetch_add(1, std::memory_order_release);
		write_unlock_async();
	}

private:
	/* out of line slow-path functions */
	bool read_lock_slow() noexcept;
	bool read_unlock_slow() noexcept;

	inline bool read_acquire() noexcept
	{
		size_t expected;
		bool success;
		do {
			expected = readers_.load(std::memory_order_relaxed);
			if (expected == 0) {
				return false;
			}
			success = readers_.compare_exchange_strong(expected, expected + 1, std::memory_order_acquire);
		} while (!success);
		return true;
	}

	inline bool read_release() noexcept
	{
		return readers_.fetch_sub(1, std::memory_order_release) != 1;
	}

	std::atomic<size_t> readers_;
	std::mutex writers_;
	std::condition_variable waiting_writers_;
	bool queued_, waiting_;
};

template<typename Container>
class read_guard {
public:
	read_guard(Container & container) : container_(container)
	{
		while (container_.lock_.read_lock()) {
			container.synchronize();
		}
	}

	~read_guard()
	{
		if (container_.lock_.read_unlock()) {
			container_.synchronize();
		}
	}

private:
	Container & container_;
};

template<typename Container>
class async_write_guard {
public:
	async_write_guard(Container & container) : container_(container)
	{
		sync_ = container_.lock_.write_lock_async();
	}

	~async_write_guard()
	{
		if (sync_) {
			container_.synchronize();
		} else {
			container_.lock_.write_unlock_async();
		}
	}

private:
	Container & container_;
	bool sync_;
};

#if 0
template <
	class T,
	class D=delete_functor<T>,
	T *(T::*async_remove_next)=&T::async_remove_next
>
class read_deferred_destroy {
public:
	inline read_deferred_destroy()
		: readers(1), async_remove(0)
	{
	}

	inline void read_lock()
	{
		while (1) {
			if (readers.inc_if_not_zero()))
				return;

			writers.lock();
			if (readers.inc_if_not_zero()) {
				writers.unlock();
				break;
			}

			_cleanup_and_unlock();
		}
	}

	inline void read_unlock()
	{
		if (--readers) return;

		writers.lock();
		if (readers!=0) {
			writers.unlock();
			return;
		}

		_cleanup_and_unlock();
	}

	inline void write_lock()
	{
		writers.lock();
	}

	inline void write_unlock()
	{
		writers.unlock();
	}

	inline void deferred_destroy(T *object)
	{
		write_lock();
		_deferred_destroy_unlock(object);
	}

protected:
	inline void _cleanup_and_unlock()
	{
		T *remove=async_remove;
		async_remove=0;
		readers++;
		write_unlock();

		while (remove) {
			T *next=((*remove).*async_remove_next);
			destroy(remove);
			remove=next;
		}
	}

	inline void _deferred_destroy_unlock(T *object)
	{
		bool sync_cleanup=false;
		if (!async_remove) sync_cleanup=(!--readers);
		(*object).*async_remove_next=async_remove;
		async_remove=object;

		if (!sync_cleanup) {
			writers.unlock();
			return;
		}

		_cleanup_and_unlock();
	}

	atomic readers;
	mutex writers;
	D destroy;
	T *async_remove;
};

template <
	class T,
	T *T::*prev = &T::prev,
	T *T::*next = &T::next,
	class D=delete_functor<T>,
	T *(T::*async_remove_next)=&T::async_remove_next
>
class list_deferred_destroy :
	public read_deferred_destroy<T, D, async_remove_next>
{
public:
	class iterator {
	public:
		inline iterator() : ptr(0) {}
		inline const iterator &operator++() {ptr=(*ptr).*next; return *this;}
		inline iterator operator++(int) {iterator tmp=*this; ptr=(*ptr).*next; return tmp;}
		inline const iterator &operator--() {if (ptr) ptr=(*ptr).*prev; else ptr=l->last; return *this;}
		inline iterator operator--(int) {iterator tmp=*this; if (ptr) ptr=(*ptr).*prev; else ptr=l->last; return tmp;}
		inline operator T *() const {return ptr;}
		inline T* operator->() const {return ptr;}
		inline bool operator==(const iterator &i) const {return ptr==i.ptr;}
		inline bool operator!=(const iterator &i) const {return ptr!=i.ptr;}
	private:
		T *ptr;
		list_deferred_destroy *l;
		inline iterator(T *p, list_deferred_destroy *_l) : ptr(p) , l(_l) {}
		friend class list_deferred_destroy;
	};
	inline list_deferred_destroy() : first(0), last(0) {}
	inline void push_back(T *obj)
	{
		write_lock();
		if (last) (*last).*next=obj; else first=obj;
		(*obj).*prev=last;
		(*obj).*next=0;
		last=obj;
		write_unlock();
	}
	inline void push_front(T *obj)
	{
		write_lock();
		if (first) (*first).*prev=obj; else last=obj;
		(*obj).*prev=0;
		(*obj).*next=first;
		first=obj;
		write_unlock();
	}
	inline void remove(T *obj)
	{
		write_lock();
		if ((*obj).*prev) *((*obj).*prev).*next=(*obj).*next;
		else first=(*obj).*next;
		if ((*obj).*next) *((*obj).*next).*prev=(*obj).*prev;
		else last=(*obj).*prev;
		_deferred_destroy_unlock(obj);
	}
	inline iterator begin() {return iterator(first, this);}
	inline iterator end() {return iterator(0, this);}
private:
	T *first, *last;
	friend class iterator;
};

#endif

}

#endif
