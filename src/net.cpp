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
	if(!res)
	{
		return {-1, strerror(errno) };
	}

	int client = socket(AF_INET, SOCK_STREAM, 0);
	if(client == -1)
	{
		return {-1, strerror(errno) };
	}

	int flags = fcntl(client, F_GETFL);
	fcntl(client, F_SETFL, flags | O_NONBLOCK);
	connect(client, res->ai_addr, res->ai_addrlen);
	return {client, ""};
}
