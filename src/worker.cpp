#include <fstream>
#include <memory>
#include <mutex>
#include <vector>
#include <cstdio>
#include <cstring>
#include <iostream>
#include "net.h"
#include "queue.h"
#include "worker.h"
#include <sys/socket.h>

const int TIMEOUT_LENGTH = 10;
char* downloadBuffer = new char[DL_BUFFER_SIZE];
std::mutex mtx;
bool running = true;

struct Downloader
{
	int socket;
	std::string remotePath;
	std::string local_path;
	std::string tmp_path;
	std::string error;
	std::unique_ptr<std::ostream> file;
	time_t startTime;
	enum State { START, CONNECTING, DOWNLOADING, FINISHED, FAILED, } state;
	enum Type { SAVE, QUEUE_DATA, } type;

	void startDownload();
	void update(fd_set* r, fd_set* w);
};
std::vector<Downloader*> downloaders;

void Downloader::startDownload()
{
	startTime = time(0);
	std::string remote = remotePath;
	if(remote.find("gopher://")==0) remote = remote.substr(9);
	if(type == SAVE)
	{
		tmp_path = local_path+".part";
		std::cout << "Downloading " << remotePath << " to " << local_path << "\n";

		file.reset(new std::ofstream(tmp_path.c_str(), std::ios::binary));
		if(!file->good())
		{
			state = FAILED;
			error = "could not open file for writing";
			return;
		}
	}
	std::string host = remote.substr(0, remote.find("/"));
	host = host.substr(0, host.find(":"));
	std::string port = "70";
	if(remote.find(":") != std::string::npos)
	{
		port = remote.substr(remote.find(":")+1);
		port = port.substr(0, port.find("/"));
	}

	Result r = opensocket(host.c_str(), port.c_str());
	if(r.result == -1)
	{
		error = r.error;
		state = FAILED;
		return;
	}
	socket = r.result;

	state = CONNECTING;
}

void Downloader::update(fd_set* rd, fd_set* wr)
{
	if(state == START)
	{
		startDownload();
		return;
	}
	else if(state == CONNECTING)
	{
		if(FD_ISSET(socket, wr))
		{
			std::string remote = remotePath;
			if(remote.find("gopher://")==0) remote = remote.substr(9);
			std::string file = remote.substr(remote.find("/")+1);
			auto s = file.find("/");
			if(s != std::string::npos)
			{
				file = file.substr(s);
			}
			else
				file = "";
			int e = send(socket, (file+"\r\n").c_str(), file.size()+2, 0);
			if(e == -1)
			{
				error = strerror(errno);
				state = FAILED;
			}
			else
				state = DOWNLOADING;
			return;
		}
		else
		{
			time_t now = time(0);
			if(now - startTime > TIMEOUT_LENGTH)
			{
				state = FAILED;
				error = "Timeout";
			}
		}
	}
	else if(FD_ISSET(socket, rd))
	{
		int r = recv(socket, downloadBuffer, DL_BUFFER_SIZE, 0);
		if(r == -1)
		{
			error = strerror(errno);
			state = FAILED;
		}
		else if(r == 0)
		{
			state = FINISHED;
		}
		else
		{
			if(type == SAVE)
				file->write(downloadBuffer, r);
			else
				queueData(Message::DATA, downloadBuffer, r);
		}
	}
}

void pollDownloaders()
{
	std::unique_lock<std::mutex> lock(mtx);
	if(!downloaders.size())
		return;
	fd_set r;
	fd_set w;
	FD_ZERO(&r);
	FD_ZERO(&w);
	int max = 0;
	for(auto& d : downloaders)
	{
		if(d->socket != -1)
		{
			FD_SET(d->socket, &r);
			FD_SET(d->socket, &w);
			if(max < d->socket)
				max = d->socket;
		}
	}
	timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 10;
	select(max+1, &r, &w, 0, &tv);
	for(auto i = downloaders.begin(); i != downloaders.end();)
	{
		auto& d = *i;
		d->update(&r, &w);
		if(d->state == Downloader::FINISHED)
		{
			if(d->type == Downloader::SAVE)
			{
				std::cout << "Download finished: " << d->local_path << "\n";
				if(rename(d->tmp_path.c_str(), d->local_path.c_str()))
					std::cout << "Failed to rename " << d->tmp_path << " to " << d->local_path << "\n";
			}
			i = downloaders.erase(i);
			continue;
		}
		else if(d->state == Downloader::FAILED)
		{
			if(d->type == Downloader::SAVE)
			{
				std::cout << "Downloading "<< d->local_path <<" failed: " << d->error << "\n";
			}
			else if(d->type == Downloader::QUEUE_DATA)
			{
				queueData(Message::ERROR, d->error);
			}
			i = downloaders.erase(i);
			continue;
		}
		else
			++i;
	}
}

void runWorker()
{
	running = true;
	while(running)
	{
		pollDownloaders();
	}
}

void endWorker()
{
	running = false;
}

void fetch(const std::string& remote_path, int type)
{
	Downloader* d = new Downloader;
	d->socket = -1;
	d->remotePath = remote_path;
	d->state = Downloader::START;
	d->type = Downloader::QUEUE_DATA;
	std::unique_lock<std::mutex> lock(mtx);
	downloaders.push_back(d);
}

void download(const std::string& remote_path, const std::string& local_path)
{
	Downloader* d = new Downloader;
	d->socket = -1;
	d->remotePath = remote_path;
	d->local_path = local_path;
	d->state = Downloader::START;
	d->type = Downloader::SAVE;
	std::unique_lock<std::mutex> lock(mtx);
	downloaders.push_back(d);
}
