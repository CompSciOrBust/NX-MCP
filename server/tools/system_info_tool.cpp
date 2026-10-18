#include "system_info_tool.hpp"
#include "tool_registry.hpp"
#include "tool_util.hpp"
#include <switch.h>
#include <cstdio>
#include <string>

namespace tools {

static bool kRegistered =
    ToolRegistry::Instance().Register(std::make_unique<SystemInfoTool>());

std::string SystemInfoTool::Name() const { return "get_system_info"; }

std::string SystemInfoTool::Description() const {
  return "Returns Switch firmware version, Atmosphère version, emuMMC status, IP address, and system tick.";
}

nlohmann::json SystemInfoTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", nlohmann::json::object()},
    {"required", nlohmann::json::array()}
  };
}

// Atmosphere-specific SPL config items (stable Exosphere ABI).
static constexpr SplConfigItem kExosphereApiVersion = (SplConfigItem)65000;
static constexpr SplConfigItem kExosphereEmummcType = (SplConfigItem)65007;

ToolResult SystemInfoTool::Execute(const nlohmann::json&) {
  Result rc = setsysInitialize();
  if (R_FAILED(rc))
    return {"setsysInitialize failed: " + FmtRc(rc), true};

  u32 ver = hosversionGet();
  u64 tick = svcGetSystemTick();

  SetSysFirmwareVersion fw{};
  setsysGetFirmwareVersion(&fw);
  setsysExit();

  // Atmosphere version and emuMMC type via Exosphere config items.
  u64 ams_api = 0;
  u64 emummc_type = 0;
  rc = splInitialize();
  if (R_SUCCEEDED(rc)) {
    splGetConfig(kExosphereApiVersion, &ams_api);
    splGetConfig(kExosphereEmummcType, &emummc_type);
    splExit();
  }

  const u32 kAmsMajor = (ams_api >> 56) & 0xFF;
  const u32 kAmsMinor = (ams_api >> 48) & 0xFF;
  const u32 kAmsMicro = (ams_api >> 40) & 0xFF;

  const char* emummc_str =
      (emummc_type == 1) ? "partition" :
      (emummc_type == 2) ? "file"      : "none (sysnand)";

  char ip_str[32] = "unavailable";
  rc = nifmInitialize(NifmServiceType_User);
  if (R_SUCCEEDED(rc)) {
    u32 ip = 0;
    if (R_SUCCEEDED(nifmGetCurrentIpAddress(&ip))) {
      snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
          (ip >> 0) & 0xFF, (ip >> 8) & 0xFF,
          (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    }
    nifmExit();
  }

  char buf[384];
  snprintf(buf, sizeof(buf),
      "Firmware:   %s (%u.%u.%u)\n"
      "Atmosphere: %u.%u.%u\n"
      "emuMMC:     %s\n"
      "IP Address: %s\n"
      "Tick:       %llu",
      fw.display_version,
      HOSVER_MAJOR(ver), HOSVER_MINOR(ver), HOSVER_MICRO(ver),
      kAmsMajor, kAmsMinor, kAmsMicro,
      emummc_str,
      ip_str,
      (unsigned long long)tick);

  return {buf, false};
}

}  // namespace tools
