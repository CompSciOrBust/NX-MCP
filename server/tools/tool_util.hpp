#pragma once
#include <switch.h>
#include <cstdio>
#include <string>

namespace tools {

inline std::string FmtRc(Result rc) {
  char buf[12];
  snprintf(buf, sizeof(buf), "0x%08X", (unsigned)rc);
  return buf;
}

}  // namespace tools
