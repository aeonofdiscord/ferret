#pragma once

#include <mutex>
#include <string>
#include <vector>

struct Message
{
	int reqid;
	enum Type { DATA, FINISHED, ERROR } type;
	std::string data;
};

struct MessageQueue
{
	std::vector<Message> queue;
	std::mutex queueMtx;

	Message pop()
	{
		std::unique_lock<std::mutex> lock(queueMtx);
		Message m = queue[0];
		queue.erase(queue.begin());
		return m;
	}

	void push(const Message& m)
	{
		std::unique_lock<std::mutex> lock(queueMtx);
		queue.push_back(m);
	}

	size_t size() const { return queue.size(); }
};

extern MessageQueue dataQueue;

void queueData(const Message& m);

