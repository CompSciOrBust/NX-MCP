#pragma once
#include <cstdint>

namespace mcp {

inline constexpr const char* kVersion = "2026-07-28";
inline constexpr const char* kLegacyVersion = "2025-11-25";
inline constexpr const char* kServerName = "NX-MCP";

struct Version {
  uint8_t major_;
  uint8_t minor_;
  uint8_t patch_;
  uint8_t pad_;
};

inline constexpr Version kServerVersionInfo = {1, 0, 0, 0};

namespace detail {

struct VersionStr {
  char buf[12]{};
  constexpr VersionStr(uint8_t a, uint8_t b, uint8_t c) {
    int i = 0;
    auto write = [&](uint8_t n) {
      if (n >= 100) buf[i++] = char('0' + n / 100);
      if (n >= 10)  buf[i++] = char('0' + (n / 10) % 10);
      buf[i++] = char('0' + n % 10);
    };
    write(a); buf[i++] = '.';
    write(b); buf[i++] = '.';
    write(c);
  }
};

inline constexpr VersionStr kVersionStr{
  kServerVersionInfo.major_,
  kServerVersionInfo.minor_,
  kServerVersionInfo.patch_
};

}  // namespace detail

inline constexpr const char* kServerVersion = detail::kVersionStr.buf;

inline constexpr const char* kInstructions =
    "You are connected to a Nintendo Switch running Horizon OS via the NX-MCP sysmodule "
    "(title ID 0x0100000000057331), loaded by Atmosphère custom firmware.\n"
    "\n"
    "Filesystem — use the sdmc:/ prefix for all FS tool paths:\n"
    "  sdmc:/                            SD card root\n"
    "  sdmc:/atmosphere/crash_reports/   crash logs (.log text report + .bin minidump)\n"
    "  sdmc:/atmosphere/contents/        per-title LayeredFS patches and sysmodule NSPs\n"
    "  sdmc:/atmosphere/config/          Atmosphère config (system_settings.ini, etc.)\n"
    "  sdmc:/switch/                     homebrew NRO applications\n"
    "\n"
    "Auto-start: Atmosphère launches a sysmodule at boot when the file\n"
    "  sdmc:/atmosphere/contents/<title_id>/flags/boot2.flag exists (title IDs in paths\n"
    "  have no 0x prefix). NX-MCP boot2.flag path:\n"
    "  sdmc:/atmosphere/contents/0100000000057331/flags/boot2.flag\n"
    "\n"
    "Process model: Horizon has three memory pools — Application (foreground game),\n"
    "Applet (album/overlays), and System (sysmodules/services). pm_list_processes reports\n"
    "usage of all three. The foreground game is tagged [app].\n"
    "\n"
    "Program IDs are 64-bit hex values. Nintendo system titles occupy\n"
    "0x0100000000000000–0x01FFFFFFFFFFFFFF.\n"
    "\n"
    "Homebrew: to boot into hbmenu, press A to launch a game then immediately press and hold R throughout the entire launch sequence. "
    "Always use hid_sequence for this — separate tool calls will cause the game to launch in the gap between them while awaiting permission. "
    "Pressing Y in hbmenu launches nxlink, which enables remote deployment and "
    "debugging of NRO files over the network.\n";

}  // namespace mcp
