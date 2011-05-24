#include <signal.h>

#include <tscb/async-safe-work>

namespace tscb {
	
	async_safe_callback::~async_safe_callback(void) throw()
	{
	}
	
	void
	async_safe_callback::disconnect(void) throw()
	{
		registration_mutex.lock();
		
		if (disconnected) {
			registration_mutex.unlock();
			return;
		}
		
		service->list_mutex.lock();
		
		disconnected = true;
		
		if (prev) prev->next = next;
		else service->first = next;
		if (next) next->prev = prev;
		else service->last = prev;
		
		if (activation_flag.test_and_set(memory_order_release)) {
			/* if triggered already, it either has been or will subsequently
			be enqueued (this may race with the "trigger" method) */
			service->async_cancel_count.fetch_add(1, memory_order_relaxed);
			service->list_mutex.unlock();
			registration_mutex.unlock();
		} else {
			service->list_mutex.unlock();
			registration_mutex.unlock();
			release();
		}
	}
	
	bool
	async_safe_callback::connected(void) const throw()
	{
		return !disconnected;
	}
	
	
	async_safe_connection::~async_safe_connection(void) throw()
	{
		if (callback) callback->release();
	}
	
	
	async_safe_work_service::~async_safe_work_service(void) throw()
	{
	}
	
	
	async_safe_work_dispatcher::async_safe_work_dispatcher(eventtrigger & _trigger)
		: pending(0), async_cancel_count(0), first(0), last(0), trigger(_trigger)
	{
	}
	
	/* temporarily and optimistically dequeue all items, but re-add them in case
	not all were processed */
	class async_pending_dequeue_helper {
	public:
		inline async_pending_dequeue_helper(atomic<async_safe_callback *> & _pending_list, eventtrigger & _trigger)
			: pending_list(_pending_list), trigger(_trigger)
		{
			head = pending_list.exchange(0, memory_order_consume);
		}
		
		inline ~async_pending_dequeue_helper(void)
		{
			if (!head)
				return;
			async_safe_callback * last = head;
			while(last->pending_next)
				last = last->pending_next;
			
			async_safe_callback * tmp = pending_list.load(memory_order_relaxed);
			do {
				last->pending_next = tmp;
			} while(!pending_list.compare_exchange_weak(tmp, head, memory_order_release, memory_order_relaxed));
			trigger.set();
		}
		
		inline async_safe_callback *
		dequeue(void) throw()
		{
			async_safe_callback * current = head;
			head = head->pending_next;
			return current;
		}
		
		inline operator bool(void) const throw()
		{
			return head != 0;
		}
		
		atomic<async_safe_callback *> & pending_list;
		async_safe_callback * head;
		eventtrigger & trigger;
	};
	
	void
	async_safe_work_dispatcher::dispatch(void)
	{
		/* fast-path check */
		if (pending.load(memory_order_relaxed) == 0) return;
		
		async_pending_dequeue_helper deq(pending, trigger);
		
		while(deq) {
			async_safe_callback * proc = deq.dequeue();
			
			list_mutex.lock();
			proc->activation_flag.clear();
			
			if (!proc->disconnected) {
				list_mutex.unlock();
				/* if this throws, the current proc will be considered "processed",
				while the remaining are re-added to the queue */
				proc->function();
			} else {
				list_mutex.unlock();
				proc->release();
				async_cancel_count.fetch_sub(1, memory_order_relaxed);
			}
		}
	}
	
	async_safe_work_dispatcher::~async_safe_work_dispatcher(void) throw()
	{
		list_mutex.lock();
		while(first) {
			async_safe_callback * tmp = first;
			tmp->pin();
			list_mutex.unlock();
			tmp->disconnect();
			tmp->release();
			list_mutex.lock();
		}
		list_mutex.unlock();
		
		while(async_cancel_count.load(memory_order_relaxed)) {
			async_safe_callback * proc = pending.exchange(0, memory_order_consume);
			
			while(proc) {
				async_safe_callback * next = proc->pending_next;
				proc->release();
				proc = next;
			}
		}
	}
	
	async_safe_connection
	async_safe_work_dispatcher::async_procedure(const boost::function<void(void)> & function)
	{
		async_safe_callback * cb = new async_safe_callback(function, this);
		
		list_mutex.lock();
		cb->prev = last;
		cb->next = 0;
		if (last) last->next = cb;
		else first = cb;
		last = cb;
		list_mutex.unlock();
		
		return cb;
	}
	
}
