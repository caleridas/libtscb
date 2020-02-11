#include <tscb/childproc-monitor.h>

namespace tscb {

/**
	\page childproc_monitor_descr Child process monitoring

	The class \ref tscb::childproc_monitor_service
	"childproc_monitor_service" defines the interface against which
	observers of child processes can use to register notification on
	child process exit. The class \ref tscb::childproc_monitor
	"childproc_monitor" allows to wait for child process events and
	deliver notifications.

	The client usage is rather straight-forward:

	\code
		tscb::childprocess_monitor_service* svc = ...;

		// Spawn a new process.
		pid_t child_pid;
		posix_spawn(&pid, ...);

		// Watch process termination.
		tscb::connection c = svc->watch_childproc(
			[](int pid, const rusage&) {
				std::cout << "Process " << pid << " exited.\n";
			}, pid);
	\endcode

	The concrete implementation, \ref tscb::childproc_monitor
	"childproc_monitor" provides the means to attach to OS interfaces
	(typically \p SIGCHLD signal handler as well as \p waitfor to
	synchronously reap child processes). To perform its task, it ties
	in with \ref tscb::workqueue::register_async_deferred_procedure to
	allow triggering on signal handler:

	\code
		// Note: this needs to be a global variable in order to be
		// accessible from signal handler.
		std::function<void()> workqueue_trigger;

		// Signal handler function.
		static void sigchld_handler(int signo)
		{
			workqueue_trigger();
		}

		// Perform setup in main function.
		int main()
		{
			// Main event handler.
			tscb::reactor reactor;
			// Downcast to narrower interface -- not technically
			// required, just for illustration.
			tscb::workqueue_service* wq_svc = &reactor;

			// Child process handler.
			tscb::childproc_monitor childprocs(false);
			// Bind function to trigger child process handling as
			// "deferred procedure".
			workqueue_trigger = register_async_deferred_procedure(
				[&childprocs]()
				{
					childprocs.dispatch();
				}).second;
			// Install as signal handler.
			signal(SIGCHLD, &sigchld_handler);

			// From now on, child process exit notifications will be
			// delivired to interested receivers.

			// Run actual program logic now...
		}
	\endcode

	Note that \ref tscb::childproc_monitor is instantiated slightly
	differently to other notification mechanisms -- it must be a
	process-wide singleton due to being bound to a signal handler
	(which is a process wide resource).

	Note that non-Posix mechanisms (e.g. \p CLONE_FD or \p kqueue)
	allow notification without signals and can therefore greatly
	simplify the setup. The \ref tscb::childproc_monitor_service
	"childproc_monitor_service" interface abstraction can also
	accomodate for such mechanisms.
*/

/**
	\class childproc_monitor_service
	\brief Service interface to monitor child processes
	\headerfile tscb/childproc-monitor.h <tscb/childproc-monitor.h>

	This interface allows to register callbacks to monitor child
	process exit events. It is targeted at systems with subprocesses
	that have posix semantics.


	\fn childproc_monitor_service::watch_childproc
	\brief Monitor single process for termination.
	\param function
		Callback to call on process exit.
	\param pid
		Process ID to monitor.
	\returns
		Connection handle to allow caller to disconnect on losing
		interest.

	Set up callback to be invoked when child processs identified by
	\p pid exits. The exit code and usual meta information provided by
	operating system is given to the callback.
*/

childproc_monitor_service::~childproc_monitor_service() noexcept
{
}

/* exclude private nested class from doxygen */
/** \cond false */

class childproc_monitor::link_type final : public connection::link_type {
public:
	using pointer = detail::intrusive_ptr<link_type>;

	~link_type() noexcept override
	{
	}

	void
	disconnect() noexcept override
	{
		registration_mutex_.lock();
		if (service_) {
			service_->remove(this);
		} else {
			registration_mutex_.unlock();
		}
	}

	bool
	is_connected() const noexcept override
	{
		std::unique_lock<std::mutex> guard(registration_mutex_);
		return service_ != nullptr;
	}

private:
	inline
	link_type(pid_t pid, const std::function<void(int, const rusage &)> function) noexcept
		: service_(nullptr)
		, prev_(nullptr)
		, next_(nullptr)
		, active_next_(nullptr)
		, deferred_destroy_next_(nullptr)
		, pid_(pid)
		, function_(std::move(function))
	{
	}

	mutable std::mutex registration_mutex_;
	childproc_monitor * service_;
	link_type * prev_, * next_;
	std::atomic<link_type *> active_next_;
	link_type * deferred_destroy_next_;

	pid_t pid_;
	std::function<void(int, const rusage &)> function_;

	friend class childproc_monitor;
};

/** \endcond */

childproc_monitor::childproc_monitor()
	: childproc_monitor(false)
{
}

/**
	\fn childproc_monitor::childproc_monitor(bool reap_all_children)
	\brief Create child process monitor with options
	\param reap_all_children
		Exclusively manage all child processes of this process (more
		efficient).

	Instantiate child process monitor. If \p reap_all_children is set,
	then the monitor is going to assume that all subprocesses of this
	process are managed by it. This makes operation a lot more
	efficient in case there are many subprocesses, but it is generally
	highly problematic outside of very controlled environments: Random
	functions from other libraries (including libc) might spawn
	subprocesses of their own and expect to have control over them.

	See \ref childproc_monitor_descr how this interacts with other
	event notification.
*/
childproc_monitor::childproc_monitor(bool reap_all_children)
	: reap_all_children_(reap_all_children),
	active_(nullptr), first_(nullptr), last_(nullptr), deferred_destroy_(nullptr)
{
}

childproc_monitor::~childproc_monitor() noexcept
{
	while (lock_.read_lock()) {
		synchronize();
	}

	for (;;) {
		link_type * link = active_.load(std::memory_order_relaxed);
		if (!link) {
			break;
		}
		link->disconnect();
	}
	if (lock_.read_unlock()) {
		synchronize();
	} else {
		lock_.write_lock_sync();
		synchronize();
	}
}

/**
	\class childproc_monitor
	\brief Implementation to handle childprocess notification
	\headerfile tscb/childproc-monitor.h <tscb/childproc-monitor.h>

	Tracks registered child processes, provides the means to request
	process information from operating system and pass this to the
	correct callback.
*/


connection
childproc_monitor::watch_childproc(
	std::function<void(int, const rusage &)> function, pid_t pid)
{
	link_type::pointer link(new link_type(pid, std::move(function)));

	link->registration_mutex_.lock();
	bool sync = lock_.write_lock_async();

	link->next_ = nullptr;
	link->prev_ = last_;

	link->active_next_.store(nullptr, std::memory_order_relaxed);

	link_type * tmp = last_;
	for (;;) {
		if (!tmp) {
			if (!active_.load(std::memory_order_relaxed)) {
				active_.store(link.get(), std::memory_order_release);
			}
			break;
		}
		if (tmp->active_next_.load(std::memory_order_relaxed)) {
			break;
		}
		tmp->active_next_.store(link.get(), std::memory_order_release);
		tmp = tmp->prev_;
	}

	/* insert into list of all elements*/
	if (last_) {
		last_->next_ = link.get();
	} else {
		first_ = link.get();
	}
	intrusive_ptr_add_ref(link.get());
	last_ = link.get();

	link->service_ = this;

	link->registration_mutex_.unlock();

	if (sync) {
		synchronize();
	} else {
		lock_.write_unlock_async();
	}

	// XXX: need to check whether the specified child has exited already.
	return connection(std::move(link));
}

/**
	\brief Get exit state of subprocesses, invoke callbacks

	This checks whether subprocesses have exited and triggers
	corresponding callbacks.
*/
void
childproc_monitor::dispatch()
{
	using read_guard = detail::read_guard<
		childproc_monitor,
		&childproc_monitor::lock_,
		&childproc_monitor::synchronize
	>;
	read_guard guard(*this);

	link_type * current = active_.load(std::memory_order_consume);

	while (current) {
		int status;
		struct rusage res;
		pid_t pid = wait4(current->pid_, &status, WNOHANG, &res);
		if (pid) {
			current->disconnect();
			current->function_(status, res);
		}

		current = current->active_next_.load(std::memory_order_consume);
	}
}

void
childproc_monitor::remove(link_type * link) noexcept
{
	bool sync = lock_.write_lock_async();
	if (link->service_ == this) {
		/* remove element from active list; we have to make
		sure that all elements that pointed to "us" within
		the active chain now point to the following element,
		so this element is skipped from within the active chain */

		link_type * tmp = link->prev_;
		link_type * next = link->active_next_.load(std::memory_order_relaxed);
		for (;;) {
			if (!tmp) {
				if (active_.load(std::memory_order_relaxed) == link) {
					active_.store(next, std::memory_order_release);
				}
				break;
			}
			if (tmp->active_next_.load(std::memory_order_relaxed) != link) {
				break;
			}
			tmp->active_next_.store(next, std::memory_order_release);
			tmp = tmp->prev_;
		}

		/* put on list of elements marked for deferred cancellation */
		link->deferred_destroy_next_ = deferred_destroy_;
		deferred_destroy_ = link;

		link->service_ = nullptr;
	}

	link->registration_mutex_.unlock();

	if (sync) {
		synchronize();
	} else {
		lock_.write_unlock_async();
	}
}

void childproc_monitor::synchronize() noexcept
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
	lock_.sync_finished();

	/* now we can release the callbacks, as we are sure that no one
	can "see" them anymore; the lock is dropped so side-effects
	of finalizing the links cannot cause deadlocks */
	while (to_destroy) {
		link_type * tmp = to_destroy->deferred_destroy_next_;
		to_destroy->function_ = nullptr;
		intrusive_ptr_release(to_destroy);
		to_destroy = tmp;
	}
}

}
