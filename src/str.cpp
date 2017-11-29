#include "str.h"

std::string replaceAll(std::string& str, const std::string& f, const std::string& r)
{
	auto i = str.find(f);
	while(str.find(f) != std::string::npos)
	{
		str.replace(i, f.size(), r);
		i = str.find(f);
	}
	return str;
}

void slice(std::string& str, size_t start, size_t end)
{
	if(start > str.size())
		start = 0;
	str.erase(0, start);
	if(end > str.size())
		end = str.size();
	str.erase(end, str.size());
}

std::string rstrip(std::string& s)
{
	while(s.size() > 0 && isspace(s[s.size()-1]))
		s.erase(s.size()-1);
	return s;
}

std::string lstrip(std::string& s)
{
	while(s.size() > 0 && isspace(s[0]))
		s.erase(0, 1);
	return s;
}

std::string strip(std::string& s)
{
	lstrip(s);
	rstrip(s);
	return s;
}
