#pragma once
#include <switch.h>

// Must match mcp::Version in server/mcp_versions.hpp
struct Version {
  u8 major_;
  u8 minor_;
  u8 patch_;
  u8 pad_;
};

// Must match mcp::kServerVersionInfo in server/mcp_versions.hpp
constexpr Version kExpectedVersion = {1, 1, 0, 0};

bool QueryModuleVersion(Version* out);
