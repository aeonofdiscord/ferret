#pragma once

#include <string>
#include <vector>

struct Message
{
	int reqid;
	enum Type { DATA, FINISHED, ERROR } type;
	std::string data;
};

extern std::vector<Message> dataQueue;

void queueData(int reqid, Message::Type t, const std::string& d);
void queueData(int reqid, Message::Type t, const char* d, size_t sz);

