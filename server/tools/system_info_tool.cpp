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
  return "Returns Switch firmware version, Atmosphère version, hardware type, emuMMC status, IP address, and system tick.";
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
  u64 hw_type = 0xFF;
  rc = splInitialize();
  if (R_SUCCEEDED(rc)) {
    splGetConfig(kExosphereApiVersion, &ams_api);
    splGetConfig(kExosphereEmummcType, &emummc_type);
    splGetConfig(SplConfigItem_HardwareType, &hw_type);
    splExit();
  }

  const u32 kAmsMajor = (ams_api >> 56) & 0xFF;
  const u32 kAmsMinor = (ams_api >> 48) & 0xFF;
  const u32 kAmsMicro = (ams_api >> 40) & 0xFF;

  const char* emummc_str =
      (emummc_type == 1) ? "partition" :
      (emummc_type == 2) ? "file"      : "none (sysnand)";

  const char* hw_str =
      (hw_type ==  0) ? "Icosa (Nintendo Switch (Erista))"      :
      (hw_type ==  1) ? "Copper (Nintendo Switch (Prototype?))" :
      (hw_type ==  2) ? "Hoag (Nintendo Switch Lite)"           :
      (hw_type ==  3) ? "Iowa (Nintendo Switch (Mariko))"       :
      (hw_type ==  4) ? "Calcio (Nintendo Switch (Prototype?))" :
      (hw_type ==  5) ? "Aula (Nintendo Switch – OLED Model)"   :
      (hw_type == 15) ? "Invalid"                               : "unknown";

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

  char buf[512];
  snprintf(buf, sizeof(buf),
      "Firmware:   %s (%u.%u.%u)\n"
      "Atmosphere: %u.%u.%u\n"
      "Hardware:   %s\n"
      "emuMMC:     %s\n"
      "IP Address: %s\n"
      "Tick:       %llu",
      fw.display_version,
      HOSVER_MAJOR(ver), HOSVER_MINOR(ver), HOSVER_MICRO(ver),
      kAmsMajor, kAmsMinor, kAmsMicro,
      hw_str,
      emummc_str,
      ip_str,
      (unsigned long long)tick);

  return {buf, false};
}

}  // namespace tools
