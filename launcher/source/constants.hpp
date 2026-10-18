#pragma once
#include <cstddef>
#include <switch.h>

constexpr u64    kModuleProgramId = 0x0100000000057331;
constexpr char   kNspPath[]       = "sdmc:/atmosphere/contents/0100000000057331/exefs.nsp";
constexpr char   kBoot2FlagsDir[] = "sdmc:/atmosphere/contents/0100000000057331/flags";
constexpr char   kBoot2FlagPath[] = "sdmc:/atmosphere/contents/0100000000057331/flags/boot2.flag";
constexpr u16    kUploadPort      = 7332;
constexpr u16    kServerPort      = 57331;
constexpr size_t kUploadChunk     = 65536;
