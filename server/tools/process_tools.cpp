#include "process_tools.hpp"
#include "tool_registry.hpp"
#include "tool_util.hpp"
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace tools {

static bool kListRegistered =
    ToolRegistry::Instance().Register(std::make_unique<PmListProcessesTool>());
static bool kInfoRegistered =
    ToolRegistry::Instance().Register(std::make_unique<PmGetProcessInfoTool>());
static bool kKillRegistered =
    ToolRegistry::Instance().Register(std::make_unique<PmKillTool>());
static bool kLaunchRegistered =
    ToolRegistry::Instance().Register(std::make_unique<PmLaunchTool>());

// ---- shared helpers --------------------------------------------------------

static std::string FmtMb(u64 bytes) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
  return buf;
}

static std::string Hex64(u64 v) {
  char buf[20];
  snprintf(buf, sizeof(buf), "0x%016llX", (unsigned long long)v);
  return buf;
}

static u64 ParseU64(const std::string& s) {
  return strtoull(s.c_str(), nullptr, 0);
}

// ---- PmListProcessesTool ---------------------------------------------------

std::string PmListProcessesTool::Name() const { return "pm_list_processes"; }

std::string PmListProcessesTool::Description() const {
  return "Lists all running processes with their PIDs and program IDs, plus system memory pool usage.";
}

nlohmann::json PmListProcessesTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", nlohmann::json::object()},
    {"required", nlohmann::json::array()}
  };
}

ToolResult PmListProcessesTool::Execute(const nlohmann::json&) {
  u64 total_app = 0, used_app = 0;
  u64 total_applet = 0, used_applet = 0;
  u64 total_sys = 0, used_sys = 0;
  svcGetSystemInfo(&total_app,    SystemInfoType_TotalPhysicalMemorySize, INVALID_HANDLE, PhysicalMemorySystemInfo_Application);
  svcGetSystemInfo(&used_app,     SystemInfoType_UsedPhysicalMemorySize,  INVALID_HANDLE, PhysicalMemorySystemInfo_Application);
  svcGetSystemInfo(&total_applet, SystemInfoType_TotalPhysicalMemorySize, INVALID_HANDLE, PhysicalMemorySystemInfo_Applet);
  svcGetSystemInfo(&used_applet,  SystemInfoType_UsedPhysicalMemorySize,  INVALID_HANDLE, PhysicalMemorySystemInfo_Applet);
  svcGetSystemInfo(&total_sys,    SystemInfoType_TotalPhysicalMemorySize, INVALID_HANDLE, PhysicalMemorySystemInfo_System);
  svcGetSystemInfo(&used_sys,     SystemInfoType_UsedPhysicalMemorySize,  INVALID_HANDLE, PhysicalMemorySystemInfo_System);

  static u64 pids[300];
  s32 count = 0;
  Result rc = svcGetProcessList(&count, pids, 300);
  if (R_FAILED(rc))
    return {"svcGetProcessList failed: " + FmtRc(rc), true};

  rc = pmdmntInitialize();
  if (R_FAILED(rc))
    return {"pmdmntInitialize failed: " + FmtRc(rc), true};

  u64 app_pid = 0;
  pmdmntGetApplicationProcessId(&app_pid);

  std::string out;
  char buf[128];

  snprintf(buf, sizeof(buf),
      "Memory pools:\n"
      "  Application  %s / %s\n"
      "  Applet       %s / %s\n"
      "  System       %s / %s\n\n",
      FmtMb(used_app).c_str(),    FmtMb(total_app).c_str(),
      FmtMb(used_applet).c_str(), FmtMb(total_applet).c_str(),
      FmtMb(used_sys).c_str(),    FmtMb(total_sys).c_str());
  out += buf;

  snprintf(buf, sizeof(buf), "Processes (%d):\n", count);
  out += buf;

  for (s32 i = 0; i < count; i++) {
    u64 program_id = 0;
    pmdmntGetProgramId(&program_id, pids[i]);

    const char* tag = (pids[i] == app_pid && app_pid != 0) ? "  [app]" : "";
    snprintf(buf, sizeof(buf), "  %-5llu  %s%s\n",
        (unsigned long long)pids[i],
        Hex64(program_id).c_str(),
        tag);
    out += buf;
  }

  pmdmntExit();
  return {out, false};
}

// ---- PmGetProcessInfoTool --------------------------------------------------

std::string PmGetProcessInfoTool::Name() const { return "pm_get_process_info"; }

std::string PmGetProcessInfoTool::Description() const {
  return "Gets the program ID and role for a specific process by PID.";
}

nlohmann::json PmGetProcessInfoTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"pid", {{"type","string"},{"description","Process ID (decimal or hex, e.g. '256' or '0x100')"}}}
    }},
    {"required", {"pid"}}
  };
}

ToolResult PmGetProcessInfoTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("pid") || !arguments["pid"].is_string())
    return {"pid required (string, decimal or hex)", true};

  u64 pid = ParseU64(arguments["pid"].get<std::string>());

  Result rc = pmdmntInitialize();
  if (R_FAILED(rc))
    return {"pmdmntInitialize failed: " + FmtRc(rc), true};

  u64 program_id = 0;
  rc = pmdmntGetProgramId(&program_id, pid);

  u64 app_pid = 0;
  pmdmntGetApplicationProcessId(&app_pid);

  pmdmntExit();

  if (R_FAILED(rc))
    return {"Process not found (rc=" + FmtRc(rc) + ")", true};

  const char* role = (pid == app_pid) ? "application" : "system process";

  char buf[256];
  snprintf(buf, sizeof(buf),
      "PID:        %llu\n"
      "Program ID: %s\n"
      "Role:       %s\n",
      (unsigned long long)pid,
      Hex64(program_id).c_str(),
      role);

  return {buf, false};
}

// ---- PmKillTool ------------------------------------------------------------

std::string PmKillTool::Name() const { return "pm_kill"; }

std::string PmKillTool::Description() const {
  return "Terminates a process by PID or program ID.";
}

nlohmann::json PmKillTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"pid",        {{"type","string"},{"description","Process ID (decimal or hex)"}}},
      {"program_id", {{"type","string"},{"description","Program ID (hex, e.g. '0x0100000000000123')"}}}
    }},
    {"required", nlohmann::json::array()}
  };
}

ToolResult PmKillTool::Execute(const nlohmann::json& arguments) {
  bool has_pid  = arguments.contains("pid")        && arguments["pid"].is_string();
  bool has_prog = arguments.contains("program_id") && arguments["program_id"].is_string();

  if (!has_pid && !has_prog)
    return {"provide pid or program_id", true};

  Result rc = pmshellInitialize();
  if (R_FAILED(rc))
    return {"pmshellInitialize failed: " + FmtRc(rc), true};

  Result kill_rc;
  std::string target;
  char buf[64];

  if (has_pid) {
    u64 pid = ParseU64(arguments["pid"].get<std::string>());
    kill_rc = pmshellTerminateProcess(pid);
    snprintf(buf, sizeof(buf), "PID %llu", (unsigned long long)pid);
    target = buf;
  } else {
    u64 program_id = ParseU64(arguments["program_id"].get<std::string>());
    kill_rc = pmshellTerminateProgram(program_id);
    target = Hex64(program_id);
  }

  pmshellExit();

  snprintf(buf, sizeof(buf), "%s %s (rc=%s)",
      target.c_str(),
      R_SUCCEEDED(kill_rc) ? "terminated" : "kill failed",
      FmtRc(kill_rc).c_str());
  return {buf, R_FAILED(kill_rc)};
}

// ---- PmLaunchTool ----------------------------------------------------------

std::string PmLaunchTool::Name() const { return "pm_launch"; }

std::string PmLaunchTool::Description() const {
  return "Launches a program by program ID.";
}

nlohmann::json PmLaunchTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"program_id", {{"type","string"},{"description","Program ID (hex, e.g. '0x0100000000000123')"}}}
    }},
    {"required", {"program_id"}}
  };
}

ToolResult PmLaunchTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("program_id") || !arguments["program_id"].is_string())
    return {"program_id required (string, hex)", true};

  u64 program_id = ParseU64(arguments["program_id"].get<std::string>());

  Result rc = pmshellInitialize();
  if (R_FAILED(rc))
    return {"pmshellInitialize failed: " + FmtRc(rc), true};

  NcmProgramLocation loc{};
  loc.program_id = program_id;
  loc.storageID  = NcmStorageId_None;

  u64 new_pid = 0;
  rc = pmshellLaunchProgram(0, &loc, &new_pid);

  pmshellExit();

  char buf[128];
  if (R_SUCCEEDED(rc))
    snprintf(buf, sizeof(buf), "Launched %s -> PID %llu",
        Hex64(program_id).c_str(), (unsigned long long)new_pid);
  else
    snprintf(buf, sizeof(buf), "Launch failed %s (rc=%s)",
        Hex64(program_id).c_str(), FmtRc(rc).c_str());

  return {buf, R_FAILED(rc)};
}

}  // namespace tools
