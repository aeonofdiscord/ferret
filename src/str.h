#pragma once

#include <string>
#include <vector>

std::string replaceAll(std::string& str, const std::string& f, const std::string& r);

void slice(std::string& str, size_t start, size_t end = std::string::npos);

std::string rstrip(std::string& s);

std::string lstrip(std::string& s);

std::string strip(std::string& s);

void splitLines(const std::string& str, std::vector<std::string>& lines);
