#pragma once
#include <string>

constexpr char kUnavailableIp[] = "?.?.?.?";
std::string GetLocalIp();
int SetupUploadServer();
bool HandleUpload(int fd);
bool InstallNsp();
