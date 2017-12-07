#include "str.h"

std::string lstrip(std::string& s)
{
	while(s.size() > 0 && isspace(s[0]))
		s.erase(0, 1);
	return s;
}

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

std::string rstrip(std::string& s)
{
	while(s.size() > 0 && isspace(s[s.size()-1]))
		s.erase(s.size()-1);
	return s;
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

void splitLines(const std::string& str, std::vector<std::string>& lines)
{
	size_t i = 0;
	while(i < str.size())
	{
		size_t l = str.find("\n", i);
		if(l != std::string::npos)
		{
			lines.push_back(str.substr(i, l-i));
			i = l+1;
		}
		else
		{
			lines.push_back(str.substr(i));
			break;
		}
	}
}

std::string strip(std::string& s)
{
	lstrip(s);
	rstrip(s);
	return s;
}
