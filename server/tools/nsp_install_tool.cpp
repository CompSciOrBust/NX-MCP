#include "nsp_install_tool.hpp"
#include "tool_registry.hpp"
#include "../services/fs_service.hpp"
#include "../services/nsp_install_service.hpp"
#include <switch.h>
#include <cstdio>

namespace tools {

static bool kNspInstallRegistered =
    ToolRegistry::Instance().Register(std::make_unique<NspInstallTool>());

static const char* StatusMessage(services::NspInstallStatus s) {
  switch (s) {
    case services::NspInstallStatus::kOk:               return nullptr;
    case services::NspInstallStatus::kInvalidPfs0:      return "invalid PFS0 header — not a valid NSP";
    case services::NspInstallStatus::kTooManyFiles:     return "NSP contains too many files";
    case services::NspInstallStatus::kNcaInstallFailed: return "failed to write NCA to content storage";
    case services::NspInstallStatus::kCnmtMissing:      return "NSP contains no CNMT NCA";
    case services::NspInstallStatus::kCnmtReadFailed:   return "failed to read CNMT (title-key encrypted NSPs are not supported)";
    case services::NspInstallStatus::kCnmtRegisterFailed: return "failed to register content meta";
    case services::NspInstallStatus::kRecordFailed:     return "failed to push NS application record";
    case services::NspInstallStatus::kNetworkError:     return "I/O error reading NSP file";
  }
  return "unknown error";
}

std::string NspInstallTool::Name() const { return "nsp_install"; }

std::string NspInstallTool::Description() const {
  return "Installs an NSP file from the SD card directly into NCM content storage. "
         "Supports sd (default) or nand storage targets. "
         "Title-key encrypted NSPs (requiring a ticket) are not supported.";
}

nlohmann::json NspInstallTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"path", {
        {"type", "string"},
        {"description", "Path to the NSP file on the SD card, e.g. \"sdmc:/games/title.nsp\""}
      }},
      {"storage", {
        {"type", "string"},
        {"enum", {"sd", "nand"}},
        {"description", "Install destination: \"sd\" (default) or \"nand\""}
      }}
    }},
    {"required", {"path"}}
  };
}

ToolResult NspInstallTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("path") || !arguments["path"].is_string())
    return {"path required (string)", true};

  const std::string path = arguments["path"].get<std::string>();

  NcmStorageId storage_id = NcmStorageId_SdCard;
  if (arguments.contains("storage") && arguments["storage"].is_string()) {
    const std::string storage = arguments["storage"].get<std::string>();
    if (storage == "nand")
      storage_id = NcmStorageId_BuiltInUser;
    else if (storage != "sd")
      return {"storage must be \"sd\" or \"nand\"", true};
  }

  int64_t size = services::FsService::Instance().FileSize(path);
  if (size < 0)
    return {"file not found: " + path, true};

  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp)
    return {"cannot open file: " + path, true};

  services::NspInstallService svc;
  services::NspInstallStatus status = svc.Install(
      static_cast<size_t>(size),
      static_cast<uint8_t>(storage_id),
      [fp](char* buf, size_t n) -> ssize_t {
        return static_cast<ssize_t>(fread(buf, 1, n, fp));
      });

  fclose(fp);

  const char* err = StatusMessage(status);
  if (err) return {err, true};

  char msg[256];
  snprintf(msg, sizeof(msg), "Installed %s to %s storage",
           path.c_str(), storage_id == NcmStorageId_BuiltInUser ? "NAND" : "SD");
  return {msg, false};
}

}  // namespace tools
