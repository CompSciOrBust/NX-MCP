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

// Moves an install from the pre-1.1.0 title ID (stops it, removes its files,
// carries over the boot2 flag). Returns whether a legacy install was found;
// the caller then installs the module under the current ID.
bool MigrateLegacyInstall();
