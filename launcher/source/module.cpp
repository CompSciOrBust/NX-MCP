#include "module.hpp"
#include "constants.hpp"

#include <cstdio>
#include <switch.h>

bool    g_module_running  = false;
Version g_running_version = {};

namespace {
StatusCallback status_cb  = nullptr;
VoidCallback   version_cb = nullptr;
}

void RegisterStatusCallback(StatusCallback cb)  { status_cb  = cb; }
void RegisterVersionCallback(VoidCallback cb)    { version_cb = cb; }

void SetStatus(const char* msg) {
  if (status_cb) status_cb(msg);
}
void SetStatus(const std::string& msg) { SetStatus(msg.c_str()); }

void RefreshVersionAfterRestart() {
  static constexpr SmServiceName kServiceName = smEncodeName("nx:mcp");

  for (int i = 0; i < 50; ++i) {
    svcSleepThread(100'000'000LL);
    bool has = false;
    tipcDispatchInOut(smGetServiceSessionTipc(), 65100, kServiceName, has);
    if (has) break;
  }

  Version v = {};
  g_module_running  = QueryModuleVersion(&v);
  g_running_version = v;
  if (version_cb) version_cb();
}

void ModuleRestart() {
  Result rc_term = 0, rc_launch = 0;
  Result rc = pmshellInitialize();
  if (R_SUCCEEDED(rc)) {
    rc_term = pmshellTerminateProgram(kModuleProgramId);
    NcmProgramLocation loc = {kModuleProgramId, NcmStorageId_None};
    rc_launch = pmshellLaunchProgram(0, &loc, nullptr);
    pmshellExit();
  } else {
    rc_term = rc_launch = rc;
  }

  char buf[128];
  snprintf(buf, sizeof(buf), "Kill: 0x%08X%s  Launch: 0x%08X%s",
    rc_term,   R_SUCCEEDED(rc_term)   ? " (ok)" : " (not running)",
    rc_launch, R_SUCCEEDED(rc_launch) ? " (ok)" : " (FAILED)");
  SetStatus(buf);

  if (R_SUCCEEDED(rc_launch))
    RefreshVersionAfterRestart();
}

void ModuleKill() {
  Result rc_term = 0;
  Result rc = pmshellInitialize();
  if (R_SUCCEEDED(rc)) {
    rc_term = pmshellTerminateProgram(kModuleProgramId);
    pmshellExit();
  } else {
    rc_term = rc;
  }

  char buf[64];
  snprintf(buf, sizeof(buf), "Kill: 0x%08X%s",
    rc_term, R_SUCCEEDED(rc_term) ? " (ok)" : " (not running)");
  SetStatus(buf);
}
