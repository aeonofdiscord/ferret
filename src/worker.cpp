#include <fstream>
#include <memory>
#include <mutex>
#include <vector>
#include <cstdio>
#include <cstring>
#include <iostream>
#include "net.h"
#include "worker.h"
#include <sys/socket.h>

const int DL_BUFFER_SIZE = 0x100000;
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
	enum State { START, DOWNLOADING, FINISHED, FAILED, } state;

	void startDownload();
	void update();
};
std::vector<Downloader*> downloaders;

void Downloader::startDownload()
{
	tmp_path = local_path+".part";
	std::cout << "Downloading " << remotePath << " to " << local_path << "\n";
	std::string remote = remotePath;
	if(remote.find("gopher://")==0) remote = remote.substr(9);
	file.reset(new std::ofstream(tmp_path.c_str(), std::ios::binary));
	if(!file->good())
	{
		state = FAILED;
		error = "could not open file for writing";
		return;
	}
	std::string host = remote.substr(0, remote.find("/"));
	host = host.substr(0, host.find(":"));
	std::string port = "70";
	if(remote.find(":") != std::string::npos)
	{
		port = remote.substr(remote.find(":")+1);
		port = port.substr(0, port.find("/"));
	}
	std::string file = remote.substr(remote.find("/")+1);
	file = file.substr(file.find("/"));
	Result r = opensocket(host.c_str(), port.c_str());
	if(r.result == -1)
	{
		error = r.error;
		state = FAILED;
		return;
	}
	socket = r.result;
	int e = send(socket, (file+"\r\n").c_str(), file.size()+2, 0);
	if(e == -1)
	{
		error = strerror(errno);
		state = FAILED;
		return;
	}
	state = DOWNLOADING;
}

void Downloader::update()
{
	if(state == START)
	{
		startDownload();
		return;
	}
	int r = recv(socket, downloadBuffer, DL_BUFFER_SIZE, 0);
	if(r == -1)
	{
		error = strerror(errno);
		state = FAILED;
		return;
	}
	else if(r == 0)
		state = FINISHED;
	else
	{
		file->write(downloadBuffer, r);
	}
}

void pollDownloaders()
{
	std::unique_lock<std::mutex> lock(mtx);
	fd_set set;
	FD_ZERO(&set);
	int max = 0;
	for(auto& d : downloaders)
	{
		FD_SET(d->socket, &set);
		if(max < d->socket)
			max = d->socket;
	}
	timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 50;
	select(max+1, &set, 0, 0, &tv);
	for(auto i = downloaders.begin(); i != downloaders.end();)
	{
		auto& d = *i;
		d->update();
		if(d->state == Downloader::FINISHED)
		{
			std::cout << "Download finished: " << d->local_path << "\n";
			if(rename(d->tmp_path.c_str(), d->local_path.c_str()))
				std::cout << "Failed to rename " << d->tmp_path << " to " << d->local_path << "\n";
			i = downloaders.erase(i);
			continue;
		}
		else if(d->state == Downloader::FAILED)
		{
			std::cout << "Downloading "<<d->local_path<<" failed: " << d->error << "\n";
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

void download(const std::string& remote_path, const std::string& local_path)
{
	std::unique_lock<std::mutex> lock(mtx);
	Downloader* d = new Downloader;
	d->remotePath = remote_path;
	d->local_path = local_path;
	d->state = Downloader::START;
	downloaders.push_back(d);
}
