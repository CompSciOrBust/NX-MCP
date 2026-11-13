#pragma once
#include <cstddef>
#include <switch.h>

constexpr u64    kModuleProgramId = 0x4200000000057331;
constexpr char   kModuleDir[]     = "sdmc:/atmosphere/contents/4200000000057331";
constexpr char   kNspPath[]       = "sdmc:/atmosphere/contents/4200000000057331/exefs.nsp";
constexpr char   kBoot2FlagsDir[] = "sdmc:/atmosphere/contents/4200000000057331/flags";
constexpr char   kBoot2FlagPath[] = "sdmc:/atmosphere/contents/4200000000057331/flags/boot2.flag";

// Up to 1.0.0 the module used an ID in the application range, which
// Atmosphere's loader only launches with sigpatches applied to ldr. At boot
// that loses the race against sys-patch, so the module never auto-started.
// Installs at the old location are migrated on launcher start.
constexpr u64    kLegacyProgramId       = 0x0100000000057331;
constexpr char   kLegacyModuleDir[]     = "sdmc:/atmosphere/contents/0100000000057331";
constexpr char   kLegacyNspPath[]       = "sdmc:/atmosphere/contents/0100000000057331/exefs.nsp";
constexpr char   kLegacyBoot2FlagsDir[] = "sdmc:/atmosphere/contents/0100000000057331/flags";
constexpr char   kLegacyBoot2FlagPath[] = "sdmc:/atmosphere/contents/0100000000057331/flags/boot2.flag";

constexpr u16    kUploadPort      = 7332;
constexpr u16    kServerPort      = 57331;
constexpr size_t kUploadChunk     = 65536;
