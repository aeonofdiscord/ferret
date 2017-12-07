#pragma once

#include <string>

const int DL_BUFFER_SIZE = 0x100000;

void fetch(int reqid, const std::string& remote_path, int type);
void download(const std::string& remote_path, const std::string& local_path);
void endWorker();
void runWorker();
