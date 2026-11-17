#pragma once
#include "version.hpp"
#include <string>

bool ShowInstallPrompt();
bool ShowUpdatePrompt(const Version& current);
void InitUi(int upload_fd, const std::string& ip, bool module_running, const Version& version);
void UpdateIpText(const std::string& ip);
void UpdateUploadText(int upload_fd);
bool HandleUiInput(int& upload_fd);
