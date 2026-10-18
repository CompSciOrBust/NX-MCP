#pragma once
#include "version.hpp"
#include <string>

extern bool    g_module_running;
extern Version g_running_version;

using StatusCallback = void (*)(const char*);
using VoidCallback   = void (*)();

void RegisterStatusCallback(StatusCallback cb);
void RegisterVersionCallback(VoidCallback cb);

void SetStatus(const char* msg);
void SetStatus(const std::string& msg);

void RefreshVersionAfterRestart();
void ModuleRestart();
void ModuleKill();
