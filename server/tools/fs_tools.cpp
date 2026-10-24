#include "fs_tools.hpp"
#include "tool_registry.hpp"
#include "tool_util.hpp"
#include "../services/fs_service.hpp"
#include <switch.h>
#include <cstdio>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <minizip/zip.h>
#include <minizip/unzip.h>

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
static bool kZipRegistered =
    ToolRegistry::Instance().Register(std::make_unique<FsZipTool>());
static bool kUnzipRegistered =
    ToolRegistry::Instance().Register(std::make_unique<FsUnzipTool>());

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

  const std::string path = arguments["path"].get<std::string>();

  std::vector<services::DirEntry> entries;
  if (!services::FsService::Instance().ListDir(path, &entries))
    return {"cannot open directory: " + path, true};

  std::string out;
  char line[512];
  for (const auto& e : entries) {
    if (e.is_dir)
      snprintf(line, sizeof(line), "[DIR]  %s\n", e.name.c_str());
    else
      snprintf(line, sizeof(line), "[FILE] %s  (%lld bytes)\n",
               e.name.c_str(), (long long)e.size);
    out += line;
  }

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

  std::string buf;
  if (!services::FsService::Instance().ReadFile(path, offset, length, &buf))
    return {"cannot open file: " + path, true};

  char header[128];
  snprintf(header, sizeof(header), "Read %zu bytes from offset %lld:\n",
      buf.size(), (long long)offset);

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

  if (!services::FsService::Instance().WriteFile(path, data.data(), data.size()))
    return {"cannot open file for writing: " + path, true};

  char buf[128];
  snprintf(buf, sizeof(buf), "Wrote %zu bytes to %s", data.size(), path.c_str());
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

  if (mkdir(path.c_str(), 0777) == 0)
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

  u32 crc = 0;
  size_t total = 0;
  bool ok = services::FsService::Instance().StreamRead(path,
      [&](const char* chunk, size_t n) -> bool {
        crc = crc32CalculateWithSeed(crc, reinterpret_cast<const uint8_t*>(chunk), n);
        total += n;
        return true;
      });

  if (!ok) return {"cannot open file: " + path, true};

  char out[128];
  snprintf(out, sizeof(out), "CRC32: 0x%08X  (%zu bytes)", crc, total);
  return {out, false};
}

// ---- zip/unzip shared helpers ----------------------------------------------

static bool ZipAddFile(zipFile zf, const std::string& file_path,
                       const std::string& entry_name) {
  if (services::FsService::Instance().FileSize(file_path) < 0) return false;

  zip_fileinfo zi{};
  if (zipOpenNewFileInZip(zf, entry_name.c_str(), &zi,
                          NULL, 0, NULL, 0, NULL,
                          Z_DEFLATED, Z_DEFAULT_COMPRESSION) != ZIP_OK)
    return false;

  bool ok = true;
  services::FsService::Instance().StreamRead(file_path,
      [&](const char* chunk, size_t n) -> bool {
        if (zipWriteInFileInZip(zf, chunk, static_cast<unsigned int>(n)) < 0) {
          ok = false;
          return false;
        }
        return true;
      });

  zipCloseFileInZip(zf);
  return ok;
}

static int ZipAddDir(zipFile zf, const std::string& abs_path,
                     const std::string& entry_prefix) {
  std::vector<services::DirEntry> entries;
  if (!services::FsService::Instance().ListDir(abs_path, &entries)) return 0;

  int count = 0;
  for (const auto& e : entries) {
    std::string abs_child   = abs_path + (abs_path.back() == '/' ? "" : "/") + e.name;
    std::string entry_child = entry_prefix + "/" + e.name;

    if (e.is_dir)
      count += ZipAddDir(zf, abs_child, entry_child);
    else if (ZipAddFile(zf, abs_child, entry_child))
      count++;
  }
  return count;
}

// ---- FsZipTool -------------------------------------------------------------

std::string FsZipTool::Name() const { return "fs_zip"; }

std::string FsZipTool::Description() const {
  return "Creates a zip archive from a file or directory on the SD card.";
}

nlohmann::json FsZipTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"source", {{"type","string"},{"description","File or directory to zip, e.g. \"sdmc:/atmosphere\""}}},
      {"dest",   {{"type","string"},{"description","Output zip file path, e.g. \"sdmc:/atmosphere_backup.zip\""}}}
    }},
    {"required", {"source", "dest"}}
  };
}

ToolResult FsZipTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("source") || !arguments["source"].is_string())
    return {"source required (string)", true};
  if (!arguments.contains("dest") || !arguments["dest"].is_string())
    return {"dest required (string)", true};

  const std::string src  = arguments["source"].get<std::string>();
  const std::string dest = arguments["dest"].get<std::string>();

  auto& fs = services::FsService::Instance();
  bool is_dir = fs.IsDirectory(src);
  if (!is_dir && fs.FileSize(src) < 0)
    return {"source not found: " + src, true};

  zipFile zf = zipOpen(dest.c_str(), APPEND_STATUS_CREATE);
  if (!zf)
    return {"cannot create zip: " + dest, true};

  int count = 0;
  if (is_dir) {
    std::string prefix = src;
    if (!prefix.empty() && prefix.back() == '/') prefix.pop_back();
    size_t slash = prefix.rfind('/');
    if (slash != std::string::npos) prefix = prefix.substr(slash + 1);
    count = ZipAddDir(zf, src, prefix);
  } else {
    size_t slash = src.rfind('/');
    std::string entry = (slash != std::string::npos) ? src.substr(slash + 1) : src;
    if (ZipAddFile(zf, src, entry)) count = 1;
  }

  zipClose(zf, NULL);

  char msg[256];
  snprintf(msg, sizeof(msg), "Zipped %d file(s) to %s", count, dest.c_str());
  return {msg, false};
}

// ---- FsUnzipTool -----------------------------------------------------------

std::string FsUnzipTool::Name() const { return "fs_unzip"; }

std::string FsUnzipTool::Description() const {
  return "Extracts a zip archive to a directory on the SD card.";
}

nlohmann::json FsUnzipTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"source", {{"type","string"},{"description","Zip file path, e.g. \"sdmc:/backup.zip\""}}},
      {"dest",   {{"type","string"},{"description","Destination directory, e.g. \"sdmc:/restored\""}}}
    }},
    {"required", {"source", "dest"}}
  };
}

ToolResult FsUnzipTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("source") || !arguments["source"].is_string())
    return {"source required (string)", true};
  if (!arguments.contains("dest") || !arguments["dest"].is_string())
    return {"dest required (string)", true};

  const std::string src = arguments["source"].get<std::string>();
  std::string dest      = arguments["dest"].get<std::string>();
  if (!dest.empty() && dest.back() != '/') dest += '/';

  unzFile uf = unzOpen(src.c_str());
  if (!uf)
    return {"cannot open zip: " + src, true};

  unz_global_info gi{};
  unzGetGlobalInfo(uf, &gi);

  auto& fs = services::FsService::Instance();
  int extracted = 0, errors = 0;

  for (uLong i = 0; i < gi.number_entry; i++) {
    char fname[512];
    unz_file_info fi{};
    unzGetCurrentFileInfo(uf, &fi, fname, sizeof(fname), NULL, 0, NULL, 0);

    std::string entry(fname);
    std::string out_path = dest + entry;

    if (!entry.empty() && entry.back() == '/') {
      fs.MkdirP(out_path.substr(0, out_path.size() - 1));
    } else {
      size_t slash = out_path.rfind('/');
      if (slash != std::string::npos)
        fs.MkdirP(out_path.substr(0, slash));

      unzOpenCurrentFile(uf);
      if (fs.StreamWrite(out_path, static_cast<size_t>(fi.uncompressed_size),
          [uf](char* buf, size_t n) -> ssize_t {
            return static_cast<ssize_t>(
                unzReadCurrentFile(uf, buf, static_cast<unsigned int>(n)));
          }))
        extracted++;
      else
        errors++;
      unzCloseCurrentFile(uf);
    }

    if (i + 1 < gi.number_entry)
      unzGoToNextFile(uf);
  }

  unzClose(uf);

  char msg[256];
  snprintf(msg, sizeof(msg), "Extracted %d file(s) to %s%s",
           extracted, dest.c_str(), errors > 0 ? " (with errors)" : "");
  return {msg, errors > 0 && extracted == 0};
}

}  // namespace tools
