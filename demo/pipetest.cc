/* -*- C++ -*-
 * (c) 2004 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

/*
  This is a small test program that exercises the event_dispatcher class
  by passing around a token in a circle of pipes.
 */

#include <vector>
#include <boost/bind.hpp>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <boost/date_time/posix_time/posix_time_types.hpp>

#include <tscb/thread>
#include <tscb/ioready>
#include <tscb/timer>

const int second_threshold=1;
const int max_threads=2;
const int num_reserved_fds=max_threads*4;

std::vector<int> read_fds, write_fds;

void create_pipes(void)
{
	int filedes[2], error, n=0;
	std::vector<int> reserved_fds;
	
	for(n=0; n<num_reserved_fds; n++) reserved_fds.push_back(open("/dev/null", O_RDONLY));
	
	n=0;
	while(1) {
		error=pipe(filedes);
		if (error) {
			if ((errno==EMFILE) || (errno==ENFILE)) break;
			perror("pipe");
			abort();
		}

		int flags=fcntl(filedes[0], F_GETFL);
		flags |= O_NONBLOCK;
		fcntl(filedes[0], F_SETFL, flags);

		read_fds.push_back(filedes[0]);
		write_fds.push_back(filedes[1]);
		n++;
	}
	
	fprintf(stderr, "created %d pipe pairs\n", n);
	
	for(n=0; n<num_reserved_fds; n++) close(reserved_fds[n]);
}

void cleanup_pipes(void)
{
	char buffer[16];
	size_t n;
	for(n=0; n<read_fds.size(); n++)
		while(read(read_fds[n], buffer, 16)!=-1);
}

class perfcounter {
public:
	perfcounter(void);
	
	void count(void);
	
	int counter, iterations;
	boost::posix_time::ptime begin;
	double loopspersecond;
	
	volatile bool finished;
};

perfcounter::perfcounter(void)
{
	counter=0;
	iterations=256;
	begin=boost::posix_time::microsec_clock::universal_time();
	loopspersecond=0;
	finished=false;
}
	
void perfcounter::count(void)
{
	if (!finished) {
		counter++;
		if (counter>=iterations) {
			boost::posix_time::ptime end;
			end=boost::posix_time::microsec_clock::universal_time();
			long long d=(end-begin).total_microseconds();
			if (d/1000000>=second_threshold) {
				loopspersecond=counter*1000000.0/d;
				finished=true;
				return;
			}
			iterations=iterations*2;
			counter=0;
			begin=end;
		}
	}
}


class receiver {
public:
	receiver(tscb::ioready_service *_io, int _from, int _to, perfcounter &counter);
	~receiver(void);
	void pass_token(int fd, int event);
	void release(void);
	
	tscb::ioready_connection link;
private:
	int from, to;
	perfcounter &counter;
};

std::vector<receiver *> receivers;

receiver::receiver(tscb::ioready_service *io, int _from, int _to, perfcounter &_counter)
	: counter(_counter)
{
	link=io->watch(boost::bind(&receiver::pass_token, this, _from, _1),
		_from, tscb::EVMASK_INPUT);
	//link=io->watch<receiver, &receiver::pass_token, &receiver::release>
	//	(_from, tscb::EVMASK_INPUT, this);
	from=_from;
	to=_to;
}

void receiver::release(void)
{
}

receiver::~receiver(void)
{
	link.disconnect();
}

void receiver::pass_token(int fd, int event)
{
	char buffer[1];
	read(from, buffer, 1);
	write(to, buffer, 1);
	counter.count();
}

void cleanup_receivers(void)
{
	for(size_t n=0; n<receivers.size(); n++) {
		delete receivers[n];
	}
	receivers.clear();
	cleanup_pipes();
}

tscb::ioready_dispatcher *prepare_ring(int start, int nelements,
	perfcounter &counter, int ninject=1)
{
	tscb::ioready_dispatcher *d=tscb::create_ioready_dispatcher();
	
	for(int n=0; n<nelements; n++)
		receivers.push_back(
			new receiver(d, read_fds[start+n], 
				write_fds[start+(n+1)%nelements],
				counter)
		);
	
	/* FIXME: inject n tokens */
	char buffer=0;
	write(write_fds[start], &buffer, 1);
	
	return d;
}

class dispatcher_thread : public tscb::thread {
public:
	dispatcher_thread(tscb::ioready_dispatcher *d) : dispatcher(d), cancelled(false)
	{
		flag=d->get_eventflag();
	}
	
	virtual void *thread_func(void) throw() {
		while(!cancelled) {
			dispatcher->dispatch(0);
		}
		return 0;
	}
	void cancel(void)
	{
		cancelled=true;
		flag->set();
	}
	
	tscb::ioready_dispatcher *dispatcher;
	volatile bool cancelled;
	tscb::eventflag *flag;
};

void run_independent(int nthreads, int nelements)
{
	int n;
	tscb::ioready_dispatcher *d[nthreads];
	perfcounter counter[nthreads];
	dispatcher_thread *thread[nthreads];

	for(n=0; n<nthreads; n++) d[n]=prepare_ring(nelements*n, nelements, counter[n]);
	
	for(n=0; n<nthreads; n++)
		thread[n]=new dispatcher_thread(d[n]);
	for(n=0; n<nthreads; n++)
		thread[n]->start();
	
	while(1) {
		wait_longer: sleep(1);
		for(n=0; n<nthreads; n++) if (!counter[n].finished) goto wait_longer;
		break;
	}

	
	for(n=0; n<nthreads; n++) {
		thread[n]->cancel();
		thread[n]->join(0);
	}
	
	double sum=0.0;
	for(n=0; n<nthreads; n++)
		sum+=counter[n].loopspersecond;
	printf("%g", sum);
	for(n=0; n<nthreads; n++)
		printf(" %g", counter[n].loopspersecond);
	printf("\n");
	
	cleanup_receivers();
	
	for(n=0; n<nthreads; n++) {
		delete thread[n];
		delete d[n];
	}
}

void run_independent(void)
{
	int nthreads=1;
	while(nthreads<=max_threads) {
		printf("%d thread(s)\n", nthreads);
		int nsockets=4;
		while(nsockets*nthreads<(int)read_fds.size()) {
			printf("%d ", nsockets);
			run_independent(nthreads, nsockets);
			nsockets=nsockets*2;
		}
		nthreads++;
	}
}

int main()
{
	create_pipes();
	
	run_independent();
}
