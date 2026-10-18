#include "fs_tools.hpp"
#include "tool_registry.hpp"
#include "tool_util.hpp"
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <dirent.h>
#include <sys/stat.h>

namespace tools {

static bool kListDirRegistered =
    ToolRegistry::Instance().Register(std::make_unique<FsListDirTool>());
static bool kReadFileRegistered =
    ToolRegistry::Instance().Register(std::make_unique<FsReadFileTool>());
static bool kWriteFileRegistered =
    ToolRegistry::Instance().Register(std::make_unique<FsWriteFileTool>());
static bool kDeleteRegistered =
    ToolRegistry::Instance().Register(std::make_unique<FsDeleteTool>());
static bool kMkdirRegistered =
    ToolRegistry::Instance().Register(std::make_unique<FsMkdirTool>());
static bool kCrc32Registered =
    ToolRegistry::Instance().Register(std::make_unique<FsCrc32Tool>());

// ---- shared helpers --------------------------------------------------------

static Result FsInit() { return 0; }
static void FsDeinit() {}

// ---- FsListDirTool ---------------------------------------------------------

std::string FsListDirTool::Name() const { return "fs_list_dir"; }

std::string FsListDirTool::Description() const {
  return "Lists entries in a directory on the SD card. Each entry is prefixed with [DIR] or [FILE] and files show their size in bytes.";
}

nlohmann::json FsListDirTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"path", {{"type","string"},{"description","Directory path, e.g. \"sdmc:/\" or \"sdmc:/switch\""}}}
    }},
    {"required", {"path"}}
  };
}

ToolResult FsListDirTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("path") || !arguments["path"].is_string())
    return {"path required (string, e.g. \"sdmc:/\")", true};

  Result rc = FsInit();
  if (R_FAILED(rc)) return {"fs init failed: " + FmtRc(rc), true};

  std::string path = arguments["path"].get<std::string>();
  std::string dir_path = path;
  if (!dir_path.empty() && dir_path.back() != '/')
    dir_path += '/';

  DIR* dir = opendir(path.c_str());
  if (!dir) {
    FsDeinit();
    return {"cannot open directory: " + path, true};
  }

  std::string out;
  struct dirent* ent;
  while ((ent = readdir(dir)) != nullptr) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;

    std::string full = dir_path + ent->d_name;
    struct stat st{};
    bool is_dir = false;
    if (stat(full.c_str(), &st) == 0)
      is_dir = S_ISDIR(st.st_mode);

    char line[512];
    if (is_dir)
      snprintf(line, sizeof(line), "[DIR]  %s\n", ent->d_name);
    else
      snprintf(line, sizeof(line), "[FILE] %s  (%lld bytes)\n",
          ent->d_name, (long long)st.st_size);
    out += line;
  }
  closedir(dir);
  FsDeinit();

  if (out.empty()) out = "(empty directory)";
  return {out, false};
}

// ---- FsReadFileTool --------------------------------------------------------

std::string FsReadFileTool::Name() const { return "fs_read_file"; }

std::string FsReadFileTool::Description() const {
  return "Reads text content from a file on the SD card. Capped at 64 KB per call.";
}

nlohmann::json FsReadFileTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"path",   {{"type","string"},  {"description","File path, e.g. \"sdmc:/atmosphere/config/system_settings.ini\""}}},
      {"offset", {{"type","integer"}, {"description","Byte offset to start reading from (default: 0)"}}},
      {"length", {{"type","integer"}, {"description","Number of bytes to read (default: 4096, max: 65536)"}}}
    }},
    {"required", {"path"}}
  };
}

ToolResult FsReadFileTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("path") || !arguments["path"].is_string())
    return {"path required (string)", true};

  const std::string path = arguments["path"].get<std::string>();

  int64_t offset = 0;
  if (arguments.contains("offset") && arguments["offset"].is_number())
    offset = arguments["offset"].get<int64_t>();

  int64_t length = 4096;
  if (arguments.contains("length") && arguments["length"].is_number())
    length = arguments["length"].get<int64_t>();

  const int64_t kMaxLen = 65536;
  if (length > kMaxLen) length = kMaxLen;
  if (length < 0)       length = 0;

  Result rc = FsInit();
  if (R_FAILED(rc)) return {"fs init failed: " + FmtRc(rc), true};

  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) {
    FsDeinit();
    return {"cannot open file: " + path, true};
  }

  if (offset > 0)
    fseek(fp, (long)offset, SEEK_SET);

  std::string buf(length, '\0');
  size_t bytes_read = fread(&buf[0], 1, (size_t)length, fp);
  fclose(fp);
  FsDeinit();

  buf.resize(bytes_read);

  char header[128];
  snprintf(header, sizeof(header), "Read %zu bytes from offset %lld:\n",
      bytes_read, (long long)offset);

  return {std::string(header) + buf, false};
}

// ---- FsWriteFileTool -------------------------------------------------------

std::string FsWriteFileTool::Name() const { return "fs_write_file"; }

std::string FsWriteFileTool::Description() const {
  return "Writes UTF-8 text content to a file on the SD card, overwriting any existing content.";
}

nlohmann::json FsWriteFileTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"path", {{"type","string"},{"description","File path to write, e.g. \"sdmc:/test.txt\""}}},
      {"data", {{"type","string"},{"description","UTF-8 text content to write"}}}
    }},
    {"required", {"path", "data"}}
  };
}

ToolResult FsWriteFileTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("path") || !arguments["path"].is_string())
    return {"path required (string)", true};
  if (!arguments.contains("data") || !arguments["data"].is_string())
    return {"data required (string)", true};

  const std::string path = arguments["path"].get<std::string>();
  const std::string data = arguments["data"].get<std::string>();

  Result rc = FsInit();
  if (R_FAILED(rc)) return {"fs init failed: " + FmtRc(rc), true};

  FILE* fp = fopen(path.c_str(), "wb");
  if (!fp) {
    FsDeinit();
    return {"cannot open file for writing: " + path, true};
  }

  size_t written = fwrite(data.data(), 1, data.size(), fp);
  fclose(fp);
  FsDeinit();

  char buf[128];
  snprintf(buf, sizeof(buf), "Wrote %zu bytes to %s", written, path.c_str());
  return {buf, false};
}

// ---- FsDeleteTool ----------------------------------------------------------

std::string FsDeleteTool::Name() const { return "fs_delete"; }

std::string FsDeleteTool::Description() const {
  return "Deletes a file or empty directory from the SD card.";
}

nlohmann::json FsDeleteTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"path", {{"type","string"},{"description","Path to the file or directory to delete"}}}
    }},
    {"required", {"path"}}
  };
}

ToolResult FsDeleteTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("path") || !arguments["path"].is_string())
    return {"path required (string)", true};

  const std::string path = arguments["path"].get<std::string>();

  // fsFsDelete* expects a path rooted at the filesystem (no "sdmc:" prefix)
  const char* nx_path = path.c_str();
  if (path.rfind("sdmc:", 0) == 0)
    nx_path += 5;

  Result rc = fsInitialize();
  if (R_FAILED(rc)) return {"fsInitialize failed: " + FmtRc(rc), true};

  FsFileSystem sd_fs;
  rc = fsOpenSdCardFileSystem(&sd_fs);
  if (R_FAILED(rc)) {
    fsExit();
    return {"fsOpenSdCardFileSystem failed: " + FmtRc(rc), true};
  }

  rc = fsFsDeleteFile(&sd_fs, nx_path);
  if (R_SUCCEEDED(rc)) {
    fsFsClose(&sd_fs);
    fsExit();
    return {"Deleted (file): " + path, false};
  }

  rc = fsFsDeleteDirectoryRecursively(&sd_fs, nx_path);
  fsFsClose(&sd_fs);
  fsExit();

  if (R_SUCCEEDED(rc))
    return {"Deleted (directory): " + path, false};

  return {"failed to delete: " + path, true};
}

// ---- FsMkdirTool -----------------------------------------------------------

std::string FsMkdirTool::Name() const { return "fs_mkdir"; }

std::string FsMkdirTool::Description() const {
  return "Creates a directory on the SD card. Parent directories must already exist.";
}

nlohmann::json FsMkdirTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"path", {{"type","string"},{"description","Directory path to create, e.g. \"sdmc:/my_folder\""}}}
    }},
    {"required", {"path"}}
  };
}

ToolResult FsMkdirTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("path") || !arguments["path"].is_string())
    return {"path required (string)", true};

  const std::string path = arguments["path"].get<std::string>();

  Result rc = FsInit();
  if (R_FAILED(rc)) return {"fs init failed: " + FmtRc(rc), true};

  int r = mkdir(path.c_str(), 0777);
  FsDeinit();

  if (r == 0)
    return {"Created directory: " + path, false};

  return {"failed to create directory: " + path, true};
}

// ---- FsCrc32Tool -----------------------------------------------------------

std::string FsCrc32Tool::Name() const { return "fs_crc32"; }

std::string FsCrc32Tool::Description() const {
  return "Computes the CRC32 checksum of a file on the SD card using hardware acceleration.";
}

nlohmann::json FsCrc32Tool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"path", {{"type","string"},{"description","File path, e.g. \"sdmc:/atmosphere/config/system_settings.ini\""}}}
    }},
    {"required", {"path"}}
  };
}

ToolResult FsCrc32Tool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("path") || !arguments["path"].is_string())
    return {"path required (string)", true};

  const std::string path = arguments["path"].get<std::string>();

  Result rc = FsInit();
  if (R_FAILED(rc)) return {"fs init failed: " + FmtRc(rc), true};

  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) {
    FsDeinit();
    return {"cannot open file: " + path, true};
  }

  static constexpr size_t kChunk = 65536;
  static uint8_t buf[kChunk];

  u32 crc = 0;
  size_t total = 0;
  size_t n;
  while ((n = fread(buf, 1, kChunk, fp)) > 0) {
    crc = crc32CalculateWithSeed(crc, buf, n);
    total += n;
  }
  fclose(fp);
  FsDeinit();

  char out[128];
  snprintf(out, sizeof(out), "CRC32: 0x%08X  (%zu bytes)", crc, total);
  return {out, false};
}

}  // namespace tools
