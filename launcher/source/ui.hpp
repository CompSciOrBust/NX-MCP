#pragma once
#include "version.hpp"
#include <string>

bool ShowInstallPrompt();
bool ShowUpdatePrompt(const Version& current);
void InitUi(int upload_fd, const std::string& ip, bool module_running, const Version& version);
bool HandleUiInput(int upload_fd);
