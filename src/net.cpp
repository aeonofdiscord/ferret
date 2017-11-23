#include <libui/ui.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <string>
#include <iostream>
#include "net.h"

Result opensocket(const char* address, const char* port)
{
	addrinfo info;
	memset(&info, 0, sizeof(info));
	info.ai_family = AF_INET;
	info.ai_socktype = SOCK_STREAM;
	addrinfo* res;
	int e_addr = getaddrinfo(address, port, &info, &res);
	if(e_addr < 0)
	{
		return {-1, "Could not open address" };
	}

	int client = socket(AF_INET, SOCK_STREAM, 0);
	if(client == -1)
	{
		return {-1, strerror(errno) };
	}

	if(!res)
	{
		close(client);
		return {-1, strerror(errno) };
	}

	int flags = fcntl(client, F_GETFL);
	fcntl(client, F_SETFL, flags | O_NONBLOCK);
	connect(client, res->ai_addr, res->ai_addrlen);

	fd_set wr;
	float timeout = 10.0f * 1000000;
	while(timeout > 0)
	{
		FD_ZERO(&wr);
		FD_SET(client, &wr);

		timeval t;
		t.tv_sec = 0;
		t.tv_usec = 1;
		select(client+1, 0, &wr, 0, &t);
		int i = 0;
		if(FD_ISSET(client, &wr) || i)
		{
			flags = fcntl(client, F_GETFL);
			fcntl(client, F_SETFL, flags & ~O_NONBLOCK);
			return {client, ""};
		}
		usleep(10000);
		timeout -= 10000;
	}
	close(client);
	return {-1, "Timeout" };
}
