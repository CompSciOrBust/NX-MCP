#include "title_tools.hpp"
#include "tool_registry.hpp"
#include "tool_util.hpp"
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace tools {

static bool kInstalledGamesRegistered =
    ToolRegistry::Instance().Register(std::make_unique<InstalledGamesTool>());
static bool kMountSaveRegistered =
    ToolRegistry::Instance().Register(std::make_unique<MountSaveTool>());
static bool kUnmountSaveRegistered =
    ToolRegistry::Instance().Register(std::make_unique<UnmountSaveTool>());
static bool kListSaveDataRegistered =
    ToolRegistry::Instance().Register(std::make_unique<ListSaveDataTool>());

// ---- InstalledGamesTool -----------------------------------------------------

std::string InstalledGamesTool::Name() const { return "list_installed_games"; }

std::string InstalledGamesTool::Description() const {
  return "Lists all games shown on the homescreen with their title IDs and names. "
         "Includes installed titles and game cards (even if ejected).";
}

nlohmann::json InstalledGamesTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", nlohmann::json::object()},
    {"required", nlohmann::json::array()}
  };
}

ToolResult InstalledGamesTool::Execute(const nlohmann::json&) {
  Result rc = nsInitialize();
  if (R_FAILED(rc))
    return {"nsInitialize failed: " + FmtRc(rc), true};

  std::vector<NsApplicationRecord> records;
  s32 offset = 0;
  while (true) {
    NsApplicationRecord batch[32];
    s32 count = 0;
    rc = nsListApplicationRecord(batch, 32, offset, &count);
    if (R_FAILED(rc) || count == 0) break;
    for (s32 i = 0; i < count; i++)
      records.push_back(batch[i]);
    offset += count;
  }

  auto ctrl = std::make_unique<NsApplicationControlData>();
  std::string result;
  char line[320];

  snprintf(line, sizeof(line), "%-16s  %s\n", "Title ID", "Name");
  result += line;
  snprintf(line, sizeof(line), "%-16s  %s\n",
           "----------------", "------------------------------------------------");
  result += line;

  for (const auto& rec : records) {
    const char* name = "(unknown)";
    char name_buf[0x201] = {};
    u64 actual_size = 0;
    if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage,
                                                rec.application_id, ctrl.get(),
                                                sizeof(NsApplicationControlData),
                                                &actual_size))) {
      NacpLanguageEntry* lang = nullptr;
      if (R_SUCCEEDED(nsGetApplicationDesiredLanguage(&ctrl->nacp, &lang)) && lang && lang->name[0]) {
        memcpy(name_buf, lang->name, sizeof(name_buf) - 1);
        name = name_buf;
      }
    }
    snprintf(line, sizeof(line), "%016llX  %s\n",
             (unsigned long long)rec.application_id, name);
    result += line;
  }

  nsExit();
  return {result, false};
}

// ---- MountSaveTool ----------------------------------------------------------

std::string MountSaveTool::Name() const { return "mount_save"; }

std::string MountSaveTool::Description() const {
  return "Mounts a save data container so it can be accessed via fs_list_dir/fs_read_file/fs_write_file "
         "using the mount name as the path prefix (e.g. \"save:/\"). "
         "save_type: \"account\" (per-user, requires uid), \"device\" (shared across users), \"bcat\" (BCAT delivery cache).";
}

nlohmann::json MountSaveTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"title_id",    {{"type","string"},{"description","Title ID hex, e.g. \"01007EF00011E000\""}}},
      {"save_type",   {{"type","string"},{"enum", nlohmann::json::array({"account","device","bcat"})}}},
      {"uid",         {{"type","string"},{"description","Account UID (required for account saves), e.g. \"XXXXXXXXXXXXXXXX-XXXXXXXXXXXXXXXX\""}}},
      {"mount_name",  {{"type","string"},{"description","Mount point name (default: \"save\"). Use as \"<name>:/\" path prefix."}}}
    }},
    {"required", {"title_id", "save_type"}}
  };
}

ToolResult MountSaveTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("title_id") || !arguments["title_id"].is_string())
    return {"title_id required (string)", true};
  if (!arguments.contains("save_type") || !arguments["save_type"].is_string())
    return {"save_type required (\"account\", \"device\", or \"bcat\")", true};

  const u64 title_id = strtoull(
      arguments["title_id"].get<std::string>().c_str(), nullptr, 16);
  const std::string save_type = arguments["save_type"].get<std::string>();
  const std::string name = arguments.value("mount_name", "save");

  Result rc;
  if (save_type == "account") {
    if (!arguments.contains("uid") || !arguments["uid"].is_string())
      return {"uid required for account saves", true};

    const std::string uid_str = arguments["uid"].get<std::string>();
    AccountUid uid{};
    if (sscanf(uid_str.c_str(), "%016llx-%016llx",
               (unsigned long long*)&uid.uid[0],
               (unsigned long long*)&uid.uid[1]) != 2)
      return {"uid format invalid — expected XXXXXXXXXXXXXXXX-XXXXXXXXXXXXXXXX", true};

    rc = fsdevMountSaveData(name.c_str(), title_id, uid);
  } else if (save_type == "device") {
    rc = fsdevMountDeviceSaveData(name.c_str(), title_id);
  } else if (save_type == "bcat") {
    rc = fsdevMountBcatSaveData(name.c_str(), title_id);
  } else {
    return {"save_type must be \"account\", \"device\", or \"bcat\"", true};
  }

  if (R_FAILED(rc))
    return {"fsdevMount failed: " + FmtRc(rc), true};

  char buf[128];
  snprintf(buf, sizeof(buf), "Mounted as %s:/ — use fs_list_dir/fs_read_file with \"%s:/\" paths.",
           name.c_str(), name.c_str());
  return {buf, false};
}

// ---- UnmountSaveTool --------------------------------------------------------

std::string UnmountSaveTool::Name() const { return "unmount_save"; }

std::string UnmountSaveTool::Description() const {
  return "Unmounts a previously mounted save data container.";
}

nlohmann::json UnmountSaveTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"mount_name", {{"type","string"},{"description","Mount point name to unmount (default: \"save\")"}}}
    }},
    {"required", nlohmann::json::array()}
  };
}

ToolResult UnmountSaveTool::Execute(const nlohmann::json& arguments) {
  const std::string name = arguments.value("mount_name", "save");
  if (fsdevUnmountDevice(name.c_str()) < 0)
    return {"failed to unmount \"" + name + "\" — not mounted?", true};
  return {"Unmounted " + name + ":/", false};
}

// ---- ListSaveDataTool -------------------------------------------------------

std::string ListSaveDataTool::Name() const { return "list_save_data"; }

std::string ListSaveDataTool::Description() const {
  return "Lists save data containers present on the system. "
         "Optionally filter by save_type (\"account\", \"device\", or \"bcat\") and/or title_id.";
}

nlohmann::json ListSaveDataTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"save_type", {{"type","string"},{"enum", nlohmann::json::array({"account","device","bcat"})},
                    {"description","Filter by save type. Omit to list all types."}}},
      {"title_id",  {{"type","string"},{"description","Filter by title ID hex, e.g. \"01007EF00011E000\""}}}
    }},
    {"required", nlohmann::json::array()}
  };
}

static const char* SaveTypeName(u8 type) {
  switch (type) {
    case FsSaveDataType_Account:    return "Account";
    case FsSaveDataType_Bcat:       return "Bcat";
    case FsSaveDataType_Device:     return "Device";
    case FsSaveDataType_Temporary:  return "Temporary";
    case FsSaveDataType_Cache:      return "Cache";
    case FsSaveDataType_System:     return "System";
    case FsSaveDataType_SystemBcat: return "SystemBcat";
    default:                        return "Unknown";
  }
}

ToolResult ListSaveDataTool::Execute(const nlohmann::json& arguments) {
  bool filter_type = false;
  u8 type_val = 0;
  if (arguments.contains("save_type") && arguments["save_type"].is_string()) {
    const std::string t = arguments["save_type"].get<std::string>();
    filter_type = true;
    if      (t == "account") type_val = FsSaveDataType_Account;
    else if (t == "device")  type_val = FsSaveDataType_Device;
    else if (t == "bcat")    type_val = FsSaveDataType_Bcat;
    else return {"save_type must be \"account\", \"device\", or \"bcat\"", true};
  }

  bool filter_title = false;
  u64 title_id = 0;
  if (arguments.contains("title_id") && arguments["title_id"].is_string()) {
    title_id = strtoull(arguments["title_id"].get<std::string>().c_str(), nullptr, 16);
    filter_title = true;
  }

  FsSaveDataInfoReader reader{};
  Result rc;

  if (filter_type || filter_title) {
    FsSaveDataFilter filter = {};
    if (filter_type) {
      filter.filter_by_save_data_type = true;
      filter.attr.save_data_type = type_val;
    }
    if (filter_title) {
      filter.filter_by_application_id = true;
      filter.attr.application_id = title_id;
    }
    rc = fsOpenSaveDataInfoReaderWithFilter(&reader, FsSaveDataSpaceId_All, &filter);
  } else {
    rc = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_All);
  }

  if (R_FAILED(rc))
    return {"fsOpenSaveDataInfoReader failed: " + FmtRc(rc), true};

  std::string result;
  char line[128];

  snprintf(line, sizeof(line), "%-9s  %-16s  %-33s  %s\n",
           "Type", "Title ID", "UID", "Size");
  result += line;
  snprintf(line, sizeof(line), "%-9s  %-16s  %-33s  %s\n",
           "---------", "----------------", "---------------------------------", "----------");
  result += line;

  FsSaveDataInfo info;
  s64 total;
  while (true) {
    rc = fsSaveDataInfoReaderRead(&reader, &info, 1, &total);
    if (R_FAILED(rc) || total == 0) break;

    char uid_str[34] = "(none)";
    if (accountUidIsValid(&info.uid)) {
      snprintf(uid_str, sizeof(uid_str), "%016llX-%016llX",
               (unsigned long long)info.uid.uid[0],
               (unsigned long long)info.uid.uid[1]);
    }

    snprintf(line, sizeof(line), "%-9s  %016llX  %-33s  %llu\n",
             SaveTypeName(info.save_data_type),
             (unsigned long long)info.application_id,
             uid_str,
             (unsigned long long)info.size);
    result += line;
  }

  fsSaveDataInfoReaderClose(&reader);

  if (result.find('\n') == result.rfind('\n'))
    result += "(no save data found)\n";

  return {result, false};
}

}  // namespace tools
