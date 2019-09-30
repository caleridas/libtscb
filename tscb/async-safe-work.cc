#include <signal.h>

#include <tscb/async-safe-work.h>

namespace tscb {

async_safe_connection::link_type::~link_type() noexcept
{
}

class async_safe_work_dispatcher::link_type final : public async_safe_connection::link_type {
public:
	using pointer = boost::intrusive_ptr<link_type>;

	~link_type() noexcept override
	{
	}

	inline
	link_type(std::function<void()> function, async_safe_work_dispatcher * service) noexcept
		: function_(std::move(function))
		, activation_flag_(false)
		, pending_next_(nullptr)
		, prev_(nullptr)
		, next_(nullptr)
		, service_(service)
		, disconnected_(false)
	{
	}

	void
	disconnect() noexcept override
	{
		registration_mutex_.lock();

		if (disconnected_) {
			registration_mutex_.unlock();
			return;
		}

		service_->list_mutex_.lock();

		disconnected_ = true;

		if (prev_) {
			prev_->next_ = next_;
		} else {
			service_->first_ = next_;
		}
		if (next_) {
			next_->prev_ = prev_;
		} else {
			service_->last_ = prev_;
		}

		if (activation_flag_.test_and_set(std::memory_order_release)) {
			/* if triggered already, it either has been or will subsequently
			be enqueued (this may race with the "trigger" method) */
			service_->async_cancel_count_.fetch_add(1, std::memory_order_relaxed);
			service_->list_mutex_.unlock();
			registration_mutex_.unlock();
		} else {
			service_->list_mutex_.unlock();
			registration_mutex_.unlock();
			intrusive_ptr_release(this);
		}
	}


	bool
	is_connected() const noexcept override
	{
		return !disconnected_;
	}

	void
	trigger() noexcept override
	{
		/* if triggered already, do nothing */
		if (activation_flag_.test_and_set(std::memory_order_acquire)) {
			return;
		}

		trigger_bottom();
	}

	void
	trigger_bottom() noexcept
	{
		link_type * tmp = service_->pending_.load(std::memory_order_relaxed);
		do {
			pending_next_ = tmp;
		} while (!service_->pending_.compare_exchange_weak(tmp, this, std::memory_order_release, std::memory_order_relaxed));
		/* small(ish) problem: trigger might race with final clean-up */
		service_->trigger_.set();
	}

private:
	std::function<void()> function_;

	std::atomic_flag activation_flag_;
	link_type * pending_next_;
	link_type * prev_, * next_;
	async_safe_work_dispatcher * service_;

	bool disconnected_;

	/** \internal \brief Serialize deregistration */
	std::mutex registration_mutex_;

	friend class async_safe_work_dispatcher;
	friend class async_safe_work_dispatcher::dequeue_helper;
};

async_safe_work_service::~async_safe_work_service() noexcept
{
}


async_safe_work_dispatcher::async_safe_work_dispatcher(eventtrigger & trigger)
	: pending_(nullptr), async_cancel_count_(0), first_(nullptr), last_(nullptr), trigger_(trigger)
{
}

/* temporarily and optimistically dequeue all items, but re-add them in case
not all were processed */
class async_safe_work_dispatcher::dequeue_helper {
public:
	inline
	dequeue_helper(
		std::atomic<link_type *> & pending_list,
		eventtrigger & trigger)
		: pending_list_(pending_list), trigger_(trigger)
	{
		head_ = pending_list_.exchange(nullptr, std::memory_order_acquire);
	}

	inline
	~dequeue_helper()
	{
		if (!head_) {
			return;
		}

		link_type * last = head_;
		while (last->pending_next_) {
			last = last->pending_next_;
		}

		link_type * tmp = pending_list_.load(std::memory_order_relaxed);
		do {
			last->pending_next_ = tmp;
		} while (!pending_list_.compare_exchange_weak(tmp, head_, std::memory_order_release, std::memory_order_relaxed));
		trigger_.set();
	}

	inline link_type *
	dequeue() noexcept
	{
		link_type * current = head_;
		head_ = head_->pending_next_;
		return current;
	}

	inline operator bool() const noexcept
	{
		return head_ != nullptr;
	}

	std::atomic<link_type *> & pending_list_;
	link_type * head_;
	eventtrigger & trigger_;
};

size_t
async_safe_work_dispatcher::dispatch()
{
	size_t handled = 0;
	/* fast-path check */
	if (pending_.load(std::memory_order_relaxed) == nullptr) {
		return 0;
	}

	dequeue_helper deq(pending_, trigger_);

	while (deq) {
		link_type * proc = deq.dequeue();

		list_mutex_.lock();
		proc->activation_flag_.clear();

		if (!proc->disconnected_) {
			list_mutex_.unlock();
			/* if this throws, the current proc will be considered "processed",
			while the remaining are re-added to the queue */
			proc->function_();
			handled ++;
		} else {
			list_mutex_.unlock();
			intrusive_ptr_release(proc);
			async_cancel_count_.fetch_sub(1, std::memory_order_relaxed);
		}
	}

	return handled;
}

async_safe_work_dispatcher::~async_safe_work_dispatcher() noexcept
{
	list_mutex_.lock();
	while (first_) {
		link_type::pointer tmp(first_);
		list_mutex_.unlock();
		tmp->disconnect();
		tmp.reset();
		list_mutex_.lock();
	}
	list_mutex_.unlock();

	while (async_cancel_count_.load(std::memory_order_relaxed)) {
		link_type * proc = pending_.exchange(nullptr, std::memory_order_acquire);

		while (proc) {
			link_type * next = proc->pending_next_;
			intrusive_ptr_release(proc);
			proc = next;
			async_cancel_count_.fetch_sub(1, std::memory_order_relaxed);
		}
	}
}

async_safe_connection
async_safe_work_dispatcher::async_procedure(std::function<void()> function)
{

	link_type::pointer cb(new link_type(std::move(function), this));

	intrusive_ptr_add_ref(cb.get());

	list_mutex_.lock();
	cb->prev_ = last_;
	cb->next_ = nullptr;
	if (last_) {
		last_->next_ = cb.get();
	} else {
		first_ = cb.get();
	}
	last_ = cb.get();
	list_mutex_.unlock();

	return async_safe_connection(std::move(cb));
}

}
