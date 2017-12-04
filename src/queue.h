#pragma once

#include <string>
#include <vector>

struct Message
{
	enum Type { DATA, FINISHED, ERROR } type;
	std::string data;
};

extern std::vector<Message> dataQueue;

void queueData(Message::Type t, const std::string& d);
void queueData(Message::Type t, const char* d, size_t sz);

