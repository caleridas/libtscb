#include <tscb/childproc-monitor.h>

namespace tscb {

childproc_monitor_service::~childproc_monitor_service() noexcept
{
}


class childproc_monitor::link_type final : public connection::link_type {
public:
	using pointer = boost::intrusive_ptr<link_type>;

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

void
childproc_monitor::dispatch()
{
	read_guard<childproc_monitor> guard(*this);

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

void childproc_monitor::synchronize()
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
