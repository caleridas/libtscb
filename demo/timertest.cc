#include <stdio.h>
#include <stdlib.h>
#include <libtscb/event>

class TimerCallback {
public:
	tscb::abstime expected;
	int nanosec;
	bool timeout(tscb::event_dispatcher *d, tscb::abstime &now);
};

bool TimerCallback::timeout(tscb::event_dispatcher *d, tscb::abstime &now)
{
	if ((now-expected).milliseconds()>50) {
		printf("%lld\n", (now-expected).microseconds());
		abort();
	}
	nanosec=nanosec+325+50+12;
	expected=expected+(nanosec/1000);
	nanosec=nanosec%1000;
	now=expected;
	return true;
}

main()
{
	tscb::event_dispatcher *d=tscb::create_dispatcher();
	TimerCallback cb;
	
	cb.expected=tscb::current_time();
	cb.nanosec=0;
	d->timer_event(cb.expected, &cb, &TimerCallback::timeout);
	
	d->run();
}
