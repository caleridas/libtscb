/* -*- C++ -*-
 * (c) 2004 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/signal.h>

/** \file signal.cc */

/**
	\page signal_descr Signals and slots

	The \ref tscb::signal "signal" template class provides thread-safe
	and highly efficient mechanism to implement the observer pattern:
	The "observer" wants to observe the state of another object, and
	for this purpose the object to be observed (the "provider")
	provides a number of "signals" that are activated on specific
	events (such as state changes) and to which the "observer" can
	listen by connecting a callback function to signals of interest.

	Complex multi-threaded applications pose a challenge to an
	implementation of this mechanism as callbacks may be registered to,
	deregistered from or activated through signals from many threads
	concurrently.

	\section callback_declaration Declaration of signals

	Signals are declared as (global or member) variables in the
	following way:

	\code
		class observable {
		public:
			tscb::connection on_value_change(std::function<void(int old_value, int new_value)> fn)
			{
				return value_change_.connect(std::move(fn));
			}

			void set_value(int new_value);

		private:
			tscb::signal<void (int, int)> value_change_;
			int value_;
		}
	\endcode

	It is recommended to make signal variables private (as in this
	example) and expose an accessor to facilitate registration:
	Observers can subscribe to the signal, but not trigger it. The
	returned \ref tscb::connection "connection" object allows observers
	to later \ref tscb::connection::disconnect "disconnect" its
	subscription to the signal.

	\section signal_emit Emitting signals

	\ref tscb::signal "signal" objects provide an overloaded ()
	operator which will inform all callback functions registered with
	it:

	\code
		void observable::set_value(int new_value)
		{
			int old_value = value_;
			value_ = new_value;
			// notify all registered callbacks
			valueChange(old_value, new_value);
		}
	\endcode

	The overloaded () operator expects exactly the number and type
	of arguments as were used when the callback chain was declared.

	\section signal_register Registration

	Observers can register generic functionals such as lambdas as
	callbacks:

	\code
		class observer {
		public:
			observer(observable* o)
			{
				o->on_value_change(
					[this](int oldval, int newval)
					{
						std::cout
							<< "Value changed from " << oldval
							<< " to " << newval << "\n";
					});
			}
		};
	\endcode

	\section signal_connections Connection management

	The \ref tscb::signal::connect "connect" method returns a
	connection object that represents the connection between the
	provider and the obverver. The return value can be stored by the
	caller:

	\code
		tscb::connection conn;
		conn = c->on_value_change(...);
	\endcode

	The connection object can later be used to cancel the
	callback:

	\code
		conn.disconnect();
	\endcode

	The associated callback function will not be invoked subsequently,
	see section \ref page_concurrency for the precise guarantee. The
	data associated with the function object will be released as soon
	as it is guaranteed that the callback function cannot be called
	again (e.g. from other threads). See also section \ref
	signal_connections_multi_threaded.

	\section signal_connections_single_threaded Automatic connection management, single-threaded

	The return value of the \ref tscb::signal::connect "connect" method
	may be assigned to a \ref tscb::scoped_connection "scoped_connection"
	object (instead of a \ref tscb::connection "scoped_connection"):

	\code
		class observer {
		public:
			observer(observable* o)
			{
				conn = o->on_value_change(
					[this](int oldval, int newval)
					{
						handle_value_change(oldval, newval);
					});
			}
		private:
			void handle_value_change(int oldval, int newval);

			scoped_connection conn;
		};
	\endcode

	The connection is handled by \ref tscb::scoped_connection
	"scoped_connection" and will be implicitly disconnected when the observer instance is destroyed:

	\code
		std::unique_ptr<observer> observer = std::make_unique<observer>(c);
		...
		observer.reset(); // will implicitly perform conn.disconnect();
	\endcode

	\warning This pattern is only safe if all notifications of the
	signal and destruction of the observer are mutually serialized
	against each other. This is the case when both are guaranteed to
	always be run in the same thread.

	\section signal_connections_multi_threaded Connection management, multi-threaded

	In complex multi-threaded programs, registration, deregistration
	and signal notification run unsynchronized. To cope with this fact
	it is advised to bind resources to the callback for automatic
	cleanup after the callback can be disposed, for example in the
	following way:

	\code
		class observer : std::enable_shared_from_this<observer> {
		public:
			void watch(observable* o)
			{
				o->on_value_change(
					[t{shared_from_this()}](int oldval, int newval)
					{
						t->handle_value_change(oldval, newval);
					});
			}

		private:
			void handle_value_change(int oldval, int newval);
		};
	\endcode

	This ensures that the callback target is referencable for as long
	as the callback function is callable. This is safe in the event of
	races between notification and \ref tscb::connection::disconnect
	"disconnect" operations. Note that \ref tscb::connection::disconnect
	"disconnect" may be called concurrently to notification delivery,
	in this case the function object associated with the callback
	may not be destroyed immediately -- it will be slightly delayed
	to a safe point in time when the callback in question can never be
	visited anymore (which generally is as soon as callback processing
	for this signal is finished).

	Other suitable patterns involve explicit reference counting
	e.g. using detail::intrusive_ptr.
*/

namespace tscb {
/**
	\class signal
	\brief Generic notifier chain
	\headerfile tscb/signal.h <tscb/signal.h>

	This template class represents a signal to which interested
	receivers can subscribe to be notified via callbacks. The signature
	of the callbacks is given as function template parameter. The class
	allows receivers to \ref connect (register a callback), deliver
	notification to all registered callbacks and \ref disconnect_all
	"disconnect all" receivers. Individual receivers can disconnect
	themselves at any point in time using \ref connection::disconnect.

	See \ref signal_descr for usage.
*/

/**
	\fn signal::connect
	\brief Register newcallback
	\param function Callback to be registered

	Registers a new callback for this signal. The newly installed
	callback is put at the end of the chain: It will be called after
	all previously registered callbacks.
*/

/**
	\fn void signal::operator()(Args... args)
	\brief Call all callback functions registered with the chain
	\param args The call arguments passed to every registered callback

	Calls all callback functions registered trough \ref connect
	with the given arguments.
*/

/**
	\fn signal::disconnect_all
	\brief Disconnect all registered callbacks

	Disconnects all registered callbacks. The result is the
	same as if \ref connection::disconnect had been called on each
	\ref connection object returned by \ref connect.
*/

/* Provide explicit specialization for the common pattern of callback
 * without argument. */
template class signal<void()>;

}
