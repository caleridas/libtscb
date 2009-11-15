/* -*- C++ -*-
 * (c) 2004 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <boost/bind.hpp>
#include <boost/intrusive_ptr.hpp>

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include <tscb/atomic>
#include <tscb/ioready>

class echo {
public:
	echo(tscb::ioready_service *service, int _fd);
	~echo(void);
	
	inline void pin(void) {refcount++;}
	inline void release(void) {if (!--refcount) delete this;}
	
	tscb::atomics::atomic_int refcount;
private:
	void data(int event);
	void destroy(void);
	
	int fd;
	tscb::ioready_connection link;
	tscb::ioready_service *service;
};

static inline void intrusive_ptr_add_ref(echo *e) throw()
{
	e->refcount++;
}
static inline void intrusive_ptr_release(echo *e) throw()
{
	if (!--e->refcount) delete e;
}

class acceptor {
public:
	acceptor(tscb::ioready_service *service, int _fd);
	
	tscb::atomics::atomic_int refcount;
private:
	void connection_request(int event);
	
	int fd;
	tscb::ioready_connection link;
	tscb::ioready_service *service;
};

static inline void intrusive_ptr_add_ref(acceptor *a) throw()
{
	a->refcount++;
}
static inline void intrusive_ptr_release(acceptor *a) throw()
{
	if (!--a->refcount) delete a;
}

echo::echo(tscb::ioready_service *_service, int _fd)
	: fd(_fd), service(_service)
{
	int flags=fcntl(fd, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
	
	link=service->watch(boost::bind(&echo::data, boost::intrusive_ptr<echo>(this), _1),
		fd, tscb::EVMASK_INPUT);
}

echo::~echo(void)
{
	printf("connection closed\n");
	close(fd);
}

void echo::data(int event)
{
	char buffer[16384];
	int n;
	do {
		n=read(fd, buffer, 16384);
		if (n<0) {
			if (errno==EAGAIN) break;
		}
		if (n<=0) {
			printf("connection closed by client\n");
			link->disconnect();
			break;
		}
		write(1, buffer, n);
		write(fd, buffer, n);
	} while(n==16384);
}

acceptor::acceptor(tscb::ioready_service *_service, int _fd)
	: fd(_fd), service(_service)
{
	int flags=fcntl(fd, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
	
	link=service->watch(boost::bind(&acceptor::connection_request,
		boost::intrusive_ptr<acceptor>(this), _1), fd, tscb::EVMASK_INPUT);
}

void acceptor::connection_request(int event)
{
	int s;
	s=accept(fd, 0, 0);
	while(s>=0) {
		new echo(service, s);
		s=accept(fd, 0, 0);
	}
}

int main(int argc, char **argv)
{
	int sock;
	int reuse_flag=1;
	struct sockaddr_in addr;
	
	sock=socket(PF_INET, SOCK_STREAM, 0);
	addr.sin_family=AF_INET;
	addr.sin_port=htons(1234);
	addr.sin_addr.s_addr=inet_addr("0.0.0.0");
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_flag, sizeof(reuse_flag));
	bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_flag, sizeof(reuse_flag));
	listen(sock, 25);
	
	tscb::ioready_dispatcher *dispatcher=tscb::create_ioready_dispatcher();
	
	new acceptor(dispatcher, sock);
	
	while(true) {
		dispatcher->dispatch(0);
	}
	
	return 0;
}
