/* -*- C++ -*-
 * (c) 2004 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include <tscb/ref>
#include <tscb/atomic>
#include <tscb/ioready>

class echo {
public:
	echo(tscb::ioready_service *service, int _fd);
	~echo(void);
private:
	void data(int fd, int event);
	void destroy(void);
	
	int fd;
	tscb::ioready_callback link;
	tscb::ioready_service *service;
};

class acceptor {
public:
	acceptor(tscb::ioready_service *service, int _fd);
	
private:
	void connection_request(int fd, int event);
	void destroy(void);
	
	int fd;
	tscb::ioready_callback link;
	tscb::ioready_service *service;
};

echo::echo(tscb::ioready_service *_service, int _fd)
	: fd(_fd), service(_service)
{
	link=service->watch<echo, &echo::data, &echo::destroy>
		(fd, tscb::EVMASK_INPUT, this);
	
	int flags=fcntl(fd, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
}

echo::~echo(void)
{
	printf("connection closed\n");
	close(fd);
}

void echo::data(int _fd, int event)
{
	char buffer[16384];
	int n;
	do {
		n=read(fd, buffer, 16384);
		if (n<0) {
			if ((errno=EAGAIN)) break;
		}
		if (n<=0) {
			printf("connection closed by client\n");
			link->cancel();
			break;
		}
		write(1, buffer, n);
		write(fd, buffer, n);
	} while(n==16384);
}

void echo::destroy(void)
{
	delete this;
}

acceptor::acceptor(tscb::ioready_service *_service, int _fd)
	: fd(_fd), service(_service)
{
	link=service->watch<acceptor, &acceptor::connection_request, &acceptor::destroy>
		(fd, tscb::EVMASK_INPUT, this);
	
	int flags=fcntl(fd, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
}

void acceptor::connection_request(int _fd, int event)
{
	int s;
	s=accept(fd, 0, 0);
	while(s>=0) {
		new echo(service, s);
		s=accept(fd, 0, 0);
	}
}

void acceptor::destroy(void)
{
	delete this;
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
