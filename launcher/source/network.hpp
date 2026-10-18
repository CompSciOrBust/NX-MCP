#pragma once
#include <string>

std::string GetLocalIp();
int SetupUploadServer();
bool HandleUpload(int fd);
bool InstallNsp();
