#pragma once

struct Result
{
	int result;
	const char* error;
};

Result opensocket(const char* address, const char* port);
