#include <tscb/childproc-monitor>

namespace tscb {
	
	childproc_monitor_service::~childproc_monitor_service(void) throw()
	{
	}
	
	childproc_callback::~childproc_callback(void) throw()
	{
	}
	
	void childproc_callback::disconnect(void) throw()
	{
		registration_mutex.lock();
		if (service) service->remove(this);
		else registration_mutex.unlock();
	}
	
	bool childproc_callback::connected(void) const throw()
	{
		mutex::guard guard(registration_mutex);
		return service != 0;
	}
	
	childproc_monitor::childproc_monitor(bool _reap_all_children)
		: reap_all_children(_reap_all_children),
		active(0), first(0), last(0), deferred_cancel(0)
	{
	}
	
	childproc_monitor::~childproc_monitor(void) throw()
	{
		while(lock.read_lock()) synchronize();
		for(;;) {
			childproc_callback * cb = active.load(memory_order_relaxed);
			if (!cb) break;
			cb->disconnect();
		}
		if (lock.read_unlock()) {
			synchronize();
		} else {
			lock.write_lock_sync();
			synchronize();
		}
	}
	
	connection
	childproc_monitor::watch_childproc(const boost::function<void(int, const rusage &)> & function, pid_t pid)
	{
		childproc_callback * cb = new childproc_callback(pid, function);
		
		cb->registration_mutex.lock();
		bool sync = lock.write_lock_async();
			
		cb->next = 0;
		cb->prev = last;
		
		cb->active_next.store(0, memory_order_relaxed);
		
		childproc_callback * tmp = last;
		for(;;) {
			if (!tmp) {
				if (!active.load(memory_order_relaxed)) active.store(cb, memory_order_release);
				break;
			}
			if (tmp->active_next.load(memory_order_relaxed)) break;
			tmp->active_next.store(cb, memory_order_release);
			tmp = tmp->prev;
		}
		
		/* insert into list of all elements*/
		if (last) last->next = cb;
		else first = cb;
		last = cb;
		
		cb->service = this;
		
		cb->registration_mutex.unlock();
		
		if (sync) synchronize();
		else lock.write_unlock_async();
		
		return cb;
	}
	
	void
	childproc_monitor::dispatch(void)
	{
		read_guard<childproc_monitor> guard(*this);
		
		childproc_callback * current = active.load(memory_order_consume);
		
		while(current) {
			int status;
			struct rusage res;
			pid_t pid = wait4(current->pid, &status, WNOHANG, &res);
			if (pid) {
				current->disconnect();
				current->function(status, res);
			}
			
			current = current->active_next.load(memory_order_consume);
		}
	}
	
	void childproc_monitor::remove(childproc_callback * cb) throw()
	{
		bool sync = lock.write_lock_async();
		if (cb->service == this) {
			/* remove element from active list; we have to make
			sure that all elements that pointed to "us" within
			the active chain now point to the following element,
			so this element is skipped from within the active chain */
			
			childproc_callback * tmp = cb->prev;
			childproc_callback * next = cb->active_next.load(memory_order_relaxed);
			while(true) {
				if (!tmp) {
					if (active.load(memory_order_relaxed) == cb)
						active.store(next, memory_order_release);
					break;
				}
				if (tmp->active_next.load(memory_order_relaxed) != cb) break;
				tmp->active_next.store(next, memory_order_release);
				tmp = tmp->prev;
			}
			
			/* put on list of elements marked for deferred cancellation */
			cb->deferred_cancel_next = deferred_cancel;
			deferred_cancel = cb;
			
			cb->service = 0;
		}
		
		cb->registration_mutex.unlock();
		
		if (sync) synchronize();
		else lock.write_unlock_async();
	}
	
	void childproc_monitor::synchronize(void)
	{
		childproc_callback * do_cancel = deferred_cancel;
		
		/* first, "repair" the list structure by "correcting" all prev
		pointers */
		while(do_cancel) {
			/* we can now safely remove the elements from the list */
			if (do_cancel->prev) do_cancel->prev->next = do_cancel->next;
			else first = do_cancel->next;
			if (do_cancel->next) do_cancel->next->prev = do_cancel->prev;
			else last = do_cancel->prev;
			
			do_cancel = do_cancel->deferred_cancel_next;
		}
		
		/* now swap pointers while still under the lock; this is
		necessary to make sure that the destructor for each
		callback link object is called exactly once */
		do_cancel = deferred_cancel;
		deferred_cancel = 0;
		lock.sync_finished();
		
		/* now we can release the callbacks, as we are sure that no one
		can "see" them anymore; the lock is dropped so side-effects
		of finalizing the links cannot cause deadlocks */
		while(do_cancel) {
			childproc_callback * tmp=do_cancel->deferred_cancel_next;
			do_cancel->cancelled();
			do_cancel->release();
			do_cancel = tmp;
		}
	}
	
}
