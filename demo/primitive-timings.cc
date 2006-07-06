#include <stdio.h>
#include <tscb/atomic>
#include <tscb/timer>
#include <tscb/thread>
#include <tscb/deferred>
#include <tscb/callback>

long long start, end;

volatile long *var;
tscb::atomic atomic_var;
tscb::mutex mutex;
tscb::deferred_rwlock deferred_rwlock;

void increment(int times)
{
	while(times--) var++;
}

void atomic_increment(int times)
{
	while(times--) atomic_var++;
}

void atomic_condincr(int times)
{
	atomic_var=1;
	while(times--) if (!atomic_var.inc_if_not_zero()) break;
}

void atomic_decandtest(int times)
{
	atomic_var=-1;
	while(times--) if (!--atomic_var) break;
}

void pthread_mutex_lockunlock(int times)
{
	while(times--) {
		mutex.lock();
		mutex.unlock();
	}
}

static inline void synchronize(void)
{
	deferred_rwlock.sync_finished();
}

void deferred_rwlock_lockunlock(int times)
{
	while(times--) {
		while (deferred_rwlock.read_lock()) synchronize();
		if (deferred_rwlock.read_unlock()) synchronize();
	}
}

void deferred_rwlock_write_lockunlock(int times)
{
	while(times--) {
		bool sync=deferred_rwlock.write_lock_async();
		if (sync) synchronize(); else deferred_rwlock.write_unlock_async();
	}
}

class CallbackReceiver {
public:
	void callback(int arg) {}
	inline void release(void) {}
};

CallbackReceiver receiver;

int ncallbacks;

void callback(int times)
{
	tscb::callback_chain<int> chain;
	
	int num=ncallbacks;
	
	while(num--)
		chain.connect<CallbackReceiver, &CallbackReceiver::callback, &CallbackReceiver::release>
			(&receiver);
	
	while(times--)
		chain(0);
}

inline void callback_fn(int dummy, int n)
{}

inline void release(int dummy)
{}

void callback_function(int times)
{
	tscb::callback_chain<int> chain;
	
	int num=ncallbacks;
	
	while(num--)
		chain.connect<int, &callback_fn, &release>(0);
	
	while(times--)
		chain(0);
}

struct simple_cb;

struct simple_cb {
	simple_cb *prev, *next;
	void (*fn)(void *closure, int arg);
	void *closure;
	
	/*
	int refcount;
	void *dummy1, *dummy2, *dummy3;
	tscb::mutex m;*/
};

void function(void *closure, int arg)
{
}

void simple_cb(int times)
{
	struct simple_cb *cb=0;
	
	for(int n=0; n<ncallbacks; n++) {
		struct simple_cb *tmp=new struct simple_cb;
		tmp->next=cb;
		cb=tmp;
		tmp->closure=0;
		tmp->fn=function;
	}
	while(--times) {
		struct simple_cb *tmp=cb;
		while(tmp) {
			(tmp->fn)(tmp->closure, 0);
			tmp=tmp->next;
		}
	}
}

void run(void (*function)(int times), const char *description)
{
	long times=128;
	
	while(1) {
		start=tscb::current_time();
		function(times);
		end=tscb::current_time();
		if ((end-start)>=500000) break;
		times=times*2;
	}
	
	double usec=end-start;
	double ops=times/usec*1000000;
	double nsec=usec/times*1000;
	
	printf("%30s: %12.1lf ops/sec %7.1lf nsecs/op\n", description, ops, nsec);
}

int main()
{
	run(increment, "Increment integer");
	run(atomic_increment, "Atomic increment integer");
	run(atomic_increment, "Atomic cond increment and test");
	run(atomic_decandtest, "Atomic decrement and test");
	run(pthread_mutex_lockunlock, "pthread_mutex lock+unlock");
	run(deferred_rwlock_lockunlock, "rwlock read_lock+read_unlock");
	run(deferred_rwlock_write_lockunlock, "rwlock write_lock+write_unlock");
	ncallbacks=0;
	printf("Empty chain\n");
	run(callback, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=1;
	printf("Single function\n");
	run(callback, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=2;
	printf("%d functions\n", ncallbacks);
	run(callback, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=3;
	printf("%d functions\n", ncallbacks);
	run(callback, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=4;
	printf("%d functions\n", ncallbacks);
	run(callback, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=5;
	printf("%d functions\n", ncallbacks);
	run(callback, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=10;
	printf("%d functions\n", ncallbacks);
	run(callback, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=15;
	printf("%d functions\n", ncallbacks);
	run(callback, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=20;
	printf("%d functions\n", ncallbacks);
	run(callback, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=30;
	printf("%d functions\n", ncallbacks);
	run(callback, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
	ncallbacks=40;
	printf("%d functions\n", ncallbacks);
	run(callback, "Callback chain, member fn");
	run(callback_function, "Callback chain, static fn");
	run(simple_cb, "Simple callback, static fn");
}
