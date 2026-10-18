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

}  // namespace mcp
