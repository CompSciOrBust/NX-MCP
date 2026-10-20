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

inline constexpr Version kServerVersionInfo = {1, 1, 0, 0};

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
    "  sdmc:/atmosphere/crash_reports/   crash logs\n"
    "  sdmc:/atmosphere/contents/        sysmodule NSPs and LayeredFS patches\n"
    "  sdmc:/atmosphere/config/          Atmosphère config\n"
    "  sdmc:/switch/                     homebrew NRO applications\n"
    "\n"
    "Homebrew: to boot into hbmenu, press A to launch a game then immediately press and "
    "hold R throughout the entire launch sequence. Always use hid_sequence for this.\n"
    "\n"
    "Server health: curl http://<ip>:57331/ping — a live server returns \"pong\".\n"
    "\n"
    "Detailed documentation is available as MCP resources — fetch them as needed:\n"
    "  nxmcp://docs/file-transfer       HTTP endpoints for download, upload, and NSP install\n"
    "  nxmcp://docs/nsp-install         Streaming NSP installer details and error codes\n"
    "  nxmcp://docs/save-data           Rules for mounting and editing game save data\n"
    "  nxmcp://docs/process-model       Horizon OS process pools, program IDs, pm_ tools\n"
    "  nxmcp://docs/sysmodule-install   Atmosphère sysmodule and Tesla overlay layout\n";

}  // namespace mcp
