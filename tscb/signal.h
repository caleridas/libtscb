/* -*- C++ -*-
 * (c) 2009 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_SIGNAL_H
#define TSCB_SIGNAL_H

/**
	\page signal_descr Signals and slots

	\ref tscb::signal "signal" and related classes provide a
	thread-safe and highly efficient mechanism to implement the
	observer pattern: The "observer" wants to observe the state of
	another object, and for this purpose the object to be observed
	(the "provider") provides a number of "signals" that are
	activated on specific events (such as state changes) and to
	which the "observer" can listen by connecting a callback
	function to signals of interest.

	Complex multi-threaded applications pose a challenge to an
	implementation of this mechanism as callbacks may be
	registered to, deregistered from or activated through signals
	from many threads concurrently.

	<TT>tscb</TT> supports this through the following class
	templates:

	- \ref tscb::signal_proxy "signal_proxy" provides the interface
	used by observers for registering notifications

	- \ref tscb::signal "signal" derives from \ref tscb::signal_proxy
	and additionally provides a mechanism for notifying all
	callbacks registered with the signal

	- \ref tscb::connection "connection" objects represent
	a callback connected to a signal; its main purpose is
	to provide a handle through with this connection can later
	be broken

	The \ref tscb::signal "signal" ... template class provides
	a generic callback mechanism. It allows one object
	(the "sender") to notify an arbitray number of other objects
	(the "receivers") of events by calling specified functions.

	\section callback_declaration Declaration of signals

	Signals are declared as (global or member) variables and
	should generally be declared in the following way:

	\code
		class MyClass {
		public:
			tscb::signal_proxy<void (int,int)> &
			onValueChange() noexcept {return valueChange;}
			// will report old and new value to callbacks

			void setValue(int newval);
		private:
			tscb::signal<void (int, int)> valueChange;
			int value;
		}
	\endcode

	\section signal_emit Emitting signals

	\ref signal objects (as defined in the previous example) provide
	an overloaded () operator which will inform all callback functions
	registered with it:

	\code
		void MyClass::setValue(int newval)
		{
			int oldval=value;
			value=newval;
			// notify all registered callbacks
			valueChange(oldval, newval);
		}
	\endcode

	The overloaded () operator expects exactly the number and type
	of arguments as were used when the callback chain was declared.

	\section signal_register Registration

	All \ref tscb::signal "signals" derive from a
	\ref tscb::signal_proxy "signal_proxy" base clase that provide a
	\ref tscb::signal_proxy::connect "connect"
	member function to allow observers to add a new callback
	function. They can be used in the following fashion:

	\code
		class MyObserver {
		public:
			MyObserver(MyClass *c)
			{
				c->onValueChange().connect(boost::bind(&MyObserver::notify_change, this, _1, _2));
			}
		protected:
			void notify_change(int oldval, int newval)
			{
				printf("Value changed from %d to %d!\n", oldval, newval);
			}
		};
	\endcode

	In the previous example, the <TT>notify_change</TT> method of the
	corresponding <TT>MyObserver</TT> object would be called
	whenever the callback chain is being activated
	(see section \ref signal_emit above). Note the the
	function object created via <TT>boost::bind</TT> will be
	destroyed as soon as the callback is cancelled (see section \ref signal_connections below).

	"Free" functions can be bound in the same way:

	\code
		char *msg="Incredible!";

		void notify_change(char *ctx, int oldval, int newval)
		{
			printf("%s Value changed from %d to %d!\n", ctx, oldval, newval);
		}

		int main()
		{
			MyObject obj;
			obj.onValueChange.connect(boost::bind(notify_change, msg, _1, _2));
			obj.setValue(42);
		}
	\endcode

	Like in the previous example, the function <TT>notify_change</TT>
	will be called whenever the callback chain is being activated.

	\section signal_connections Connection management

	The
	\ref tscb::signal::connect "connect" method
	returns a connection object that represents the
	connection between the provider and the obverver. The
	return value can be stored by the caller:

	\code
		tscb::connection conn;
		conn=c->onValueChange.connect(boost::bind(&MyObserver::notify_change, this, _1, _2));
	\endcode

	The conection object can later be used to cancel the
	callback:

	\code
		conn.disconnect();
	\endcode

	The associated callback function will not be invoked subsequently,
	see section \ref design_concurrency_reentrancy for the precise guarantee.
	The data
	associated with the function object will be released as soon as it is guaranteed
	that the callback function cannot be called again (e.g. from other threads).
	This is useful e.g. if <TT>boost::shared_ptr</TT> or
	<TT>boost::intrusive_ptr</TT> is used to assist in
	tracking the lifetime of objects:

	\code
		tscb::connection conn;
		boost::shared_ptr<MyObserver> obj(new MyObserver);
		conn=c->onValueChange.connect(boost::bind(&MyObserver::notify_change)
			boost::shared_ptr(obj), _1, _2));
	\endcode

	In this case, a <TT>boost::shared_ptr</TT> instance is kept for as long as
	the callback can be activated -- thus the object will not be deallocated
	until it is guaranteed that the callback cannot be delivered any longer.

	\section signal_connections_single_threaded Connection management, single-threaded

	\warning The below is potentially not thread-safe and should
	be done with care.

	The return value of the \ref tscb::signal::connect "connect" method
	may be assigned to a \ref tscb::scoped_connection "scoped_connection"
	object (instead of a \ref tscb::connection "scoped_connection"):

	\code
		class MyObserver {
		public:
			MyObserver(MyClass *c)
			{
				conn = c->onValueChange().connect(boost::bind(&MyObserver::notify_change, this, _1, _2));
			}
		protected:
			void notify_change(int oldval, int newval)
			{
				printf("Value changed from %d to %d!\n", oldval, newval);
			}

			scoped_connection conn;
		};
	\endcode

	The impact of this is that the connection will be broken automatically
	when the \ref tscb::scoped_connection "scoped_connection" object
	goes out of scope or is destroyed otherwise. This provides a
	convenient mechanism for an object to track pending callbacks to
	itself, and have them broken automatically when the object
	is destroyed:

	\code
		MyObserver * observer = new MyObserver(c);
		...
		delete observer; // will implicitly perform conn.disconnect();
	\endcode

	\warning It is very important that you are on your own in ensuring that
	no other thread may race to perform a signal delivery while the object
	is being destroyed. You are safe if all signal delivery operations happen
	in the same thread as the deletion of the object (as is trivially the
	case when the program is single-threaded).

	\section signal_connections_multi_threaded Connection management, multi-threaded

	The recommended pattern for dynamically managed objects that need to receive
	callbacks from multiple threads:

	\code
		class MyObserver;
		static inline void intrusive_ptr_add_ref(const MyObserver *);
		static inline void intrusive_ptr_release(const MyObserver *);

		class MyObserver {
		public:
			typedef boost::intrusive_ptr<MyObserver> pointer;

			static pointer
			create(MyClass * c)
			{
				// constructor initializes refcount to 1, so no initial increment
				return pointer(new MyClass(c), false);
			}

			void
			disconnect()
			{
				conn.disconnect();
			}
		protected:
			MyObserver(MyClass *c)
				: refcount(1)
			{
				// use ref-counted pointer instead of naked pointer in
				// binding function object: causes functional to acquire
				// a reference to this object, thus this object will not
				// be deleted when there might be outstanding callbacks
				conn = c->onValueChange().connect(boost::bind(&MyObserver::notify_change, pointer(this), _1, _2));
				// it is important that the constructor itself holds a reference
				// before registering the callback: there could be a race
				// causing the connection to be broken from the other side
				// before the constructor finishes, which would cause the
				// function object to drop its reference. If the reference
				// count were dropped to zero, the object would be
				// destroyed before it was fully constructed!
			}

			void notify_change(int oldval, int newval)
			{
				std::cout << "Value changed from " << oldval << " to " << newval << "!\n";
			}

			connection conn;

			// atomic reference count implementation
			mutable atomic<size_t> refcount;
			friend void intrusive_ptr_add_ref(const MyObserver * obj)
			{
				obj->fetch_add(std::memory_order_relaxed, 1);
			}
			friend void intrusive_ptr_release(const MyObserver * obj)
			{
				if (obj->fetch_sub(std::memory_order_relaxed, 1) == 1) {
					atomic_thread_fence(std::memory_order_acquire);
					delete obj;
				}
			}
		};
	\endcode

*/

#include <mutex>
#include <functional>
#include <stdexcept>

#include <tscb/connection.h>
#include <tscb/deferred.h>

namespace tscb {

template<typename Signature> class signal;

/**
	\brief Generic notifier chain

	This object allows interested receivers to register themselves
	for notification, and it allows a sender to deliver notification
	to all registered receivers.

	See \ref signal_descr for usage.
*/
template<typename Signature>
class signal final {
private:
	class link_type : public connection::link_type {
	public:
		using pointer = boost::intrusive_ptr<link_type>;

		/** \internal \brief Instantiate callback link */
		link_type(signal * master, std::function<Signature> function) noexcept
			: function_(std::move(function)), active_next_(nullptr), prev_(nullptr), next_(nullptr), chain_(master)
		{
		}

		~link_type() noexcept override {}

		void
		disconnect() noexcept override
		{
			registration_mutex_.lock();
			signal * chain = chain_.load(std::memory_order_relaxed);
			if (chain) {
				chain->remove(this);
			} else {
				registration_mutex_.unlock();
			}
		}

		bool
		is_connected() const noexcept override
		{
			return chain_.load(std::memory_order_relaxed) != nullptr;
		}

	private:
		std::function<Signature> function_;
		std::atomic<link_type *> active_next_;
		link_type * prev_;
		link_type * next_;
		link_type * deferred_destroy_next_;
		std::atomic<signal<Signature> *> chain_;
		std::mutex registration_mutex_;

		friend class signal;
	};

public:
	signal() noexcept
		: active_(nullptr), first_(nullptr), last_(nullptr), deferred_destroy_(nullptr)
	{}

	connection
	connect(std::function<Signature> function) /* throw(std::bad_alloc) */
	{
		typename link_type::pointer link(new link_type(this, std::move(function)));
		push_back(link.get());
		return connection(std::move(link));
	}

	~signal() noexcept
	{
		while (lock_.read_lock()) {
			synchronize();
		}
		bool any_cancelled = false;
		for (;;) {
			link_type * tmp = active_.load(std::memory_order_relaxed);
			if (!tmp) {
				break;
			}
			any_cancelled = true;
			tmp->disconnect();
		}
		if (lock_.read_unlock()) {
			/* the above cancel operations will cause synchronization
			to be performed at the next possible point in time; if
			there is no concurrent cancellation, this is now */
			synchronize();
		} else if (any_cancelled) {
			/* this can only happen if some callback link was
			cancelled while this object is being destroyed; in
			that case we have to suspend the thread that is destroying
			the object until we are certain that synchronization has
			been performed */
			auto guard = lock_.write_lock_sync();
			link_type * to_destroy = synchronize_top();
			lock_.write_unlock_sync(std::move(guard));
			synchronize_bottom(to_destroy);
		}
	}

	/**
		\brief Call all callback functions registered with the chain

		Calls all callback functions registered trough \ref connect
		with the given arguments.
	*/
	template<typename... Args>
	inline void
	operator()(Args... args)
	{
		read_guard<signal<Signature>> guard(*this);
		link_type * l = active_.load(std::memory_order_consume);
		while(l) {
			l->function_(std::forward<Args>(args)...);
			l = l->active_next_.load(std::memory_order_consume);
		}
	}
	/**
		\brief Disconnect all registered callbacks

		Disconnects all registered callbacks. The result is the
		same as if \ref connection::disconnect had been called on
		\ref connection object returned by \ref connect.
	*/
	inline bool
	disconnect_all()
	{
		bool any_disconnected = false;
		read_guard<signal<Signature> > guard(*this);
		link_type * l = active_.load(std::memory_order_consume);
		while (l) {
			any_disconnected = true;
			l->disconnect();
			l = l->active_next_.load(std::memory_order_consume);
		}

		return any_disconnected;
	}

protected:
	/** \internal \brief Add link to end of chain */
	void
	push_back(link_type * l) noexcept
	{
		intrusive_ptr_add_ref(l);
		/* note: object has been fully constructed at this point,
		but the following lock acquisition only provides "acquire"
		semantics so that the memory references constructing
		this object are allowed to "leak" into the locked
		region. We therefore need an explicit fence here in
		order to avoid making an uninitialized element visible
		during traversal of the chain */
		std::atomic_thread_fence(std::memory_order_release);

		l->registration_mutex_.lock();
		bool sync = lock_.write_lock_async();

		l->next_ = nullptr;
		l->prev_ = last_;

		l->active_next_.store(nullptr, std::memory_order_relaxed);

		/* add element to active list; find all elements that have been removed
		from the full list and thus terminate the active list; point them to
		the newly-added element */

		link_type * tmp = last_;
		for (;;) {
			if (!tmp) {
				if (!active_.load(std::memory_order_relaxed)) {
					active_.store(l, std::memory_order_release);
				}
				break;
			}
			if (tmp->active_next_.load(std::memory_order_relaxed)) {
				break;
			}
			tmp->active_next_.store(l, std::memory_order_release);
			tmp = tmp->prev_;
		}

		/* insert into list of all elements*/
		if (last_) {
			last_->next_ = l;
		} else {
			first_ = l;
		}
		last_  =l;

		l->chain_.store(this, std::memory_order_relaxed);

		l->registration_mutex_.unlock();

		if (sync) {
			synchronize();
		} else {
			lock_.write_unlock_async();
		}
	}

	/** \internal \brief Remove link from chain */
	void
	remove(link_type * l) noexcept
	{
		bool sync = lock_.write_lock_async();
		if (l->chain_.load(std::memory_order_relaxed) == this) {
			/* remove element from active list; we have to make
			sure that all elements that pointed to "us" within
			the active chain now point to the following element,
			so this element is skipped from within the active chain */

			link_type * tmp = l->prev_;
			link_type * next = l->active_next_.load(std::memory_order_relaxed);
			for (;;) {
				if (!tmp) {
					if (active_.load(std::memory_order_relaxed) == l) {
						active_.store(next, std::memory_order_release);
					}
					break;
				}
				if (tmp->active_next_.load(std::memory_order_relaxed) != l) {
					break;
				}
				tmp->active_next_.store(next, std::memory_order_release);
				tmp = tmp->prev_;
			}

			/* put on list of elements marked to be destroyed at sync point */
			l->deferred_destroy_next_ = deferred_destroy_;
			deferred_destroy_ = l;

			/* remove pointer to chain, so a second call to ->cancel()
			will do nothing */
			l->chain_.store(nullptr, std::memory_order_relaxed);
		}

		l->registration_mutex_.unlock();

		if (sync) {
			synchronize();
		} else {
			lock_.write_unlock_async();
		}
	}

	link_type *
	synchronize_top() noexcept
	{
		link_type * to_destroy = deferred_destroy_;

		/* first, "repair" the list structure by "correcting" all prev
		pointers */
		while (to_destroy) {
			/* we can now safely remove the elements from the list */
			if (to_destroy->prev_) {
				to_destroy->prev_->next_ = to_destroy->next_;
			} else {
				first_ = to_destroy->next_;
			}
			if (to_destroy->next_) {
				to_destroy->next_->prev_ = to_destroy->prev_;
			} else {
				last_ = to_destroy->prev_;
			}

			to_destroy = to_destroy->deferred_destroy_next_;
		}

		/* now swap pointers while still under the lock; this is
		necessary to make sure that the destructor for each
		callback link object is called exactly once */
		to_destroy = deferred_destroy_;
		deferred_destroy_ = nullptr;

		return to_destroy;
	}

	void
	synchronize_bottom(link_type * to_destroy) noexcept
	{
		/* now we can release the callbacks, as we are sure that no one
		can "see" them anymore; the lock is dropped so side-effest
		of finalizing the links cannot cause deadlocks */

		while (to_destroy) {
			link_type * tmp = to_destroy->deferred_destroy_next_;
			to_destroy->function_ = nullptr;
			intrusive_ptr_release(to_destroy);
			to_destroy = tmp;
		}
	}

	/** \internal \brief Synchronize when reaching quiescent state */
	void
	synchronize() noexcept
	{
		link_type * to_destroy = synchronize_top();
		lock_.sync_finished();
		synchronize_bottom(to_destroy);
	}

	/** \internal \brief singly-linked list of active elements */
	std::atomic<link_type *> active_;

	/** \internal \brief thread synchronization */
	deferrable_rwlock lock_;

	/** \internal \brief First element in the chain, whether active or not */
	link_type * first_;
	/** \internal \brief Last element in the chain, whether active or not */
	link_type * last_;

	/** \internal \brief List of elements to be cancelled

		singly-linked list of elements that have been removed from
		the active list, but are not yet removed from the full list
		and have not been discarded yet
	*/
	link_type * deferred_destroy_;

	friend class read_guard<signal<Signature>>;
};

}

#endif
