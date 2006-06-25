#include <libtscb/deferred>
#include <libtscb/thread>

tscb::deferred_rwlock a;
tscb::mutex b;

void synchronize(void)
{
	a.sync_finished();
}

main()
{
	int n;
	for(n=0; n<100000000; n++) {
		/*while (a.read_lock()) synchronize();
		a.read_unlock();*/
		b.lock();
		b.unlock();
	}
}
