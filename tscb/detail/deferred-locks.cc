/*
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.
 * Refer to the file "COPYING" for details.
 */

#include <tscb/detail/deferred-locks.h>

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

	- \ref tscb::deferred_rwlock allows only deferred synchronization

	- \ref tscb::deferrable_rwlock allows both deferred and
	  non-deferred synchronization

	They provide the following guarantees:

	- acquiring multiple nested read-locks in arbitrary order is
	  deadlock-free

	- acquiring a single deferred write-lock while holding multiple
	  read-locks is deadlock-free

	- acquiring a single non-deferred write-lock while holding no other
	  read-locks or write-locks is deadlock free

	All cases of lock nesting not covered above depend on external
	constraints wrt to lock acquisition to determine if they are
	deadlock-free or not.

	The implementation is optimized under the assumption that access
	for reading is significantly more common than access for writing.

	\section deferred_sync_simple Using deferred updates

	Read access to a data structure protected by this kind of
	synchronization mechanism is granted by calling the \ref
	tscb::deferred_rwlock::read_lock "read_lock" and \ref
	tscb::deferred_rwlock::read_unlock "read_unlock" methods around the
	relevant code blocks. Both methods return a boolean indicating
	whether synchronization is required:

	\code
		tscb::deferred_rwlock lck;
		...
		while (lck.read_lock()) {
			// synchronization should be done now, perform necessary
			// actions to apply queued up modifications
			lck.sync_finished();
		}
		...
		// now perform read access to protected data structure
		...
		if (lck.read_unlock()) {
			// synchronization should be done now, perform necessary
			// actions to apply queued up modifications
			lck.sync_finished();
		}
	\endcode

	Notice that \ref tscb::deferred_rwlock::read_lock "read_lock" has
	to be retried in a loop (but see below), while \ref
	tscb::deferred_rwlock::read_unlock "read_unlock" will always
	"succeed" in a sense. It is important to note that \ref
	tscb::deferred_rwlock::sync_finished "sync_finished" <B>must</B> be
	called after any of these methods returns true -- until \ref
	tscb::deferred_rwlock::sync_finished "sync_finished" is called,
	both write and read access from any other container will block!

	On the write side, deferred synchronization is provided by calling
	\ref tscb::deferred_rwlock::write_lock_async "write_lock_async" and
	\ref tscb::deferred_rwlock::write_unlock_async
	"write_unlock_async":

	\code
		tscb::deferred_rwlock lck;
		...
		bool synchronous = lck.write_lock_async();
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
			lck.sync_finished();
		} else {
			// cannot currently apply modifications, have to defer them
			// code should perform "safe" modification of data structure
			// e.g. using atomic publish
			lck.write_unlock_async();
			// a subsequent read_lock, read_unlock or write_lock_async
			// is promised to return "true", and synchronization must
			// be deferred until then
		}
	\endcode

	Again the return code of \ref
	tscb::deferred_rwlock::write_unlock_async "write_unlock_async"
	indicates whether synchronization is required and possible. Note
	that the easiest way is to proceed as in the code shown below:
	Always enqueue modifications, and only in the end decide to apply
	modifications immediately.

	Notice that the locking primitives are agnostic as to the structure
	of the data protected and also do not care how updates are being
	kept track of -- this is the responsibility of the caller! An
	example demonstrating the concept for a simple linked list might
	look as follows:

	\code
		class ListElement {
		public:
			ListElement* prev;
			ListElement* next;
			bool to_be_removed;
		};

		class List {
		public:
			void traverse_list()
			{
				while (lck.read_lock()) {
					synchronize();
				}

				ListElement* current = first;
				while (current) {
					... // do something useful
					current = current->next;
				}

				if (lck.read_unlock()) {
					synchronize();
				}
			}
			void erase_element(ListElement *element)
			{
				bool sync = lck.write_lock_async();

				element->to_be_removed=true;

				if (sync) {
					synchronize();
				} else {
					lck.write_unlock_async();
				}
			}
		private:
			void synchronize()
			{
				ListElement* current = first;
				while (current) {
					ListElement* tmp = current->next;
					if (current->to_be_removed) {
						delete current;
					}
					current = tmp;
				}
				lck.sync_finished();
			}
			ListElement* first;
			ListElement* last;
			tscb::deferred_rwlock lck;
		};
	\endcode

	(Note: in the example just above it might be a smart idea to mark
	<CODE>to_be_removed</CODE> "atomic" and check its value while
	traversing the list to skip elements marked for deletion).

	It is <I>possible</I> to use \ref tscb::deferred_rwlock::read_lock
	in a fashion similiar to \ref
	tscb::deferred_rwlock::write_lock_async -- that is, not retry the
	locking operation and distinguish by the return value only on
	unlock. However this opens up all lock-nesting problems the
	implementation seeks to eliminate, so this is best avoided.

	\section deferrable_sync_simple Using deferrable updates

	Using \ref tscb::deferrable_rwlock instead of \ref
	tscb::deferred_rwlock allows to synchronously grab the write lock:

	\code
		tscb::deferrable_rwlock lck;
		...
		lck.write_lock_sync();
		// it is guaranteed that neither reader nor writer can hold
		// the lock now
		...
		// necessary operations to synchronize data structure must be
		// performed hede
		lck.write_unlock_sync();
	\endcode

	The call to \ref tscb::deferrable_rwlock::write_lock_sync
	"write_lock_sync" will <I>block</I> as long as there is at least
	one reader who is holding a read lock, instead of just returning
	"false" as \ref tscb::deferrable_rwlock::write_lock_async
	"write_lock_async" does.

	The main purpose of the synchronous write lock versions is to allow
	the caller to force a defined synchronization point, mainly to avoid
	the starvation problem the fully asynchronous version has. It is
	possible, though discouraged, to "abuse" this lock as an ordinary
	(blocking) read-/write-lock.

	\section Performance

	The implementation has been optimized towards the read-path. For the
	case that there are no queued modifications to be processed, the
	overhead is as follows:

	- \ref tscb::deferred_rwlock::read_lock "read_lock": one successful
	  atomic test-and-increment operation

	- \ref tscb::deferred_rwlock::read_unlock "read_unlock": one atomic
	  decrement-and-test operation where the counter does not return to
	  zero

	For the write locks and for the read locks with contention
	(modifications have been queued up asynchronously) the additional
	overhead consists of one mutex acquisiton/release.

	For the \ref tscb::deferrable_rwlock case the additional cost in
	the contention cases is a broadcast operation on a condition
	variable, and waiting on the variable for the synchronous locks.

	Since the atomic variable is modified everytime a read lock is
	acquired, the implementation does <I>not</I> provide the best
	possible level of concurrency for multiple readers in different
	threads that frequently acquire and release this lock. For this
	reason, acquisition and release of the read lock is not
	significantly faster than an ordinary mutex, so it is advantageous
	<I>only</I> in cases multiple mutex lock/release cycles can be
	consolidated into a single locking call. For all other purposes,
	ordinary mutexes should be used as they are easier to understand.
*/

namespace tscb {
namespace detail {

/**
	\class deferred_rwlock
	\brief Deferred reader-/writer synchronization
	\headerfile tscb/detail/deferred-locks.h <tscb/detail/deferred-locks.h>

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


	\fn deferred_rwlock::read_lock
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


	\fn deferred_rwlock::read_unlock
	\brief Release read lock

	Releases a previously held read lock acquired by \ref read_lock.

	If this function returns "true", then the lock is now in
	"synchronizing" state: all queued up modifications must
	be applied, after that \ref sync_finished must be called.

	If this function returns "false", then the caller may simply
	proceed.


	\fn deferred_rwlock::write_lock_async
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


	\fn deferred_rwlock::write_unlock_async
	\brief Release write lock

	Releases a write lock previously acquired through \ref write_lock_async.
	Note that this function <B>may not</B> be called if \ref write_lock_async
	has returned true; call \ref sync_finished instead.


	\fn deferred_rwlock::sync_finished
	\brief Synchronization completed

	Releases the lock out of "synchronizing" state, i.e. the state
	that has been entered when any of the following functions
	has returned true: \ref read_lock, \ref read_unlock, \ref write_lock_async.
*/

bool deferred_rwlock::read_lock_slow() noexcept
{
	writers_.lock();
	if (read_acquire()) {
		writers_.unlock();
		return false;
	}

	return true;
}

bool deferred_rwlock::read_unlock_slow() noexcept
{
	writers_.lock();
	/* note: if another thread obsevers 1->0 transition, it will
	take the mutex afterwards (and thus serialize with us)

	conversely, a 0->1 transition can only happen with the
	mutex held; therefore, the acquire/release implicit in
	the mutex is sufficient to enforce memory ordering here */
	if (readers_.load(std::memory_order_relaxed) != 0) {
		writers_.unlock();
		return false;
	}

	return true;
}

/**
	\class deferrable_rwlock
	\headerfile tscb/detail/deferred-locks.h <tscb/detail/deferred-locks.h>
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


	\fn deferrable_rwlock::read_lock
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


	\fn deferrable_rwlock::read_unlock
	\brief Release read lock

	Releases a previously held read lock acquired by \ref read_lock.

	If this function returns "true", then the lock is now in
	"synchronizing" state: all queued up modifications must
	be applied, after that \ref sync_finished must be called.

	If this function returns "false", then the caller may simply
	proceed.


	\fn deferrable_rwlock::write_lock_async
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


	\fn deferrable_rwlock::write_unlock_async
	\brief Release write lock

	Releases a write lock previously acquired through \ref write_lock_async.
	Note that this function <B>may not</B> be called if \ref write_lock_async
	has returned true; call \ref sync_finished instead.


	\fn deferrable_rwlock::write_lock_sync
	\brief synchronously acquire write lock

	Acquires a write lock. This operation blocks until no other
	readers or writers are active. After the caller is finished
	he must call \ref write_unlock_sync.


	\fn deferrable_rwlock::write_unlock_sync
	\brief release write lock

	Requires a write lock previously acquired by \ref write_lock_sync.
	Note that this operation is completely equivalent to
	\ref sync_finished.


	\fn deferrable_rwlock::sync_finished
	\brief Synchronization completed

	Releases the lock out of "synchronizing" state, i.e. the state
	that has been entered when any of the following functions
	has returned true: \ref read_lock, \ref read_unlock,
	\ref write_lock_async.
*/

bool deferrable_rwlock::read_lock_slow() noexcept
{
	writers_.lock();
	while (waiting_) {
		waiting_ = false;
		waiting_writers_.notify_all();
		writers_.unlock();
		writers_.lock();
	}
	if (read_acquire()) {
		writers_.unlock();
		return false;
	}

	return true;
}

bool deferrable_rwlock::read_unlock_slow() noexcept
{
	writers_.lock();
	while (waiting_) {
		waiting_ = false;
		writers_.unlock();
		waiting_writers_.notify_all();
		writers_.lock();
	}
	/* note: if another thread obsevers 1->0 transition, it will
	take the mutex afterwards (and thus serialize with us)

	conversely, a 0->1 transition can only happen with the
	mutex held; therefore, the acquire/release implicit in
	the mutex is sufficient to enforce memory ordering here */
	if (readers_.load(std::memory_order_relaxed)!=0) {
		writers_.unlock();
		return false;
	}

	return true;
}

/**
	\class read_guard
	\headerfile tscb/detail/deferred-locks.h <tscb/detail/deferred-locks.h>
	\brief Read guard helper.

	Helper class to manage read locks in \ref deferred_rwlock and
	\ref deferrable_rwlock using RAII.


	\fn read_guard::read_guard
	\brief Acquire read lock

	Acquire read lock. This may loop in synchronization until
	read lock is acquired successfully.


	\fn read_guard::~read_guard
	\brief Release read lock

	Release read lock, performing synchronization as required.
*/

/**
	\class async_write_guard
	\headerfile tscb/detail/deferred-locks.h <tscb/detail/deferred-locks.h>
	\brief Async write guard helper.

	Helper class to manage asynchronous write locks in \ref
	deferred_rwlock and \ref deferrable_rwlock using RAII.


	\fn async_write_guard::async_write_guard
	\brief Acquire asynchronous write lock.

	Acquire async write lock. It is possible that the lock is not
	"exclusive" because readers may still be active. See \ref exclusive.


	\fn async_write_guard::~async_write_guard
	\brief Release asynchronous lock

	Release asynchronous write lock, performing synchronization as
	required.


	\fn async_write_guard::exclusive
	\brief Check whether lock is held exclusively.
	\returns Whether lock is held exclusively.

	Determines whether the lock is held exclusively. If true, then no
	readers are active (or can become active) while this lock is being
	held. It is safe to modify data structures in a way that would
	destructively interfere with concurrent read access.

	If false, then some readers may be active. Modifications to data
	structure need to account for that and delay destructive
	modifications to synchronization time. See \ref deferred_descr
	section.
*/

}
}
