#include "fs_service.hpp"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace services {

FsService& FsService::Instance() {
  static FsService instance;
  return instance;
}

bool FsService::ReadFile(const std::string& path, int64_t offset, int64_t length,
                         std::string* out) {
  if (length > static_cast<int64_t>(kChunkSize)) length = static_cast<int64_t>(kChunkSize);
  if (length < 0) length = 0;

  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) return false;

  if (offset > 0) fseek(fp, static_cast<long>(offset), SEEK_SET);

  out->resize(static_cast<size_t>(length));
  size_t n = fread(&(*out)[0], 1, static_cast<size_t>(length), fp);
  fclose(fp);
  out->resize(n);
  return true;
}

bool FsService::WriteFile(const std::string& path, const void* data, size_t len) {
  FILE* fp = fopen(path.c_str(), "wb");
  if (!fp) return false;
  size_t written = fwrite(data, 1, len, fp);
  fclose(fp);
  return written == len;
}

bool FsService::StreamRead(const std::string& path,
                           std::function<bool(const char*, size_t)> on_chunk) {
  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) return false;

  size_t n;
  while ((n = fread(buf_, 1, kChunkSize, fp)) > 0)
    if (!on_chunk(buf_, n)) break;

  fclose(fp);
  return true;
}

bool FsService::StreamWrite(const std::string& path, size_t len,
                            std::function<ssize_t(char*, size_t)> on_recv) {
  FILE* fp = fopen(path.c_str(), "wb");
  if (!fp) return false;

  size_t remaining = len;
  bool ok = true;
  while (remaining > 0 && ok) {
    size_t to_recv = remaining < kChunkSize ? remaining : kChunkSize;
    ssize_t got = on_recv(buf_, to_recv);
    if (got <= 0 ||
        fwrite(buf_, 1, static_cast<size_t>(got), fp) != static_cast<size_t>(got)) {
      ok = false;
    }
    remaining -= ok ? static_cast<size_t>(got) : 0;
  }

  fclose(fp);
  return ok;
}

bool FsService::ListDir(const std::string& path, std::vector<DirEntry>* out) {
  std::string dir_path = path;
  if (!dir_path.empty() && dir_path.back() != '/') dir_path += '/';

  DIR* dir = opendir(path.c_str());
  if (!dir) return false;

  struct dirent* ent;
  while ((ent = readdir(dir)) != nullptr) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

    std::string full = dir_path + ent->d_name;
    struct stat st{};
    DirEntry entry;
    entry.name = ent->d_name;
    entry.is_dir = false;
    entry.size = 0;
    if (stat(full.c_str(), &st) == 0) {
      entry.is_dir = S_ISDIR(st.st_mode);
      entry.size = st.st_size;
    }
    out->push_back(entry);
  }
  closedir(dir);
  return true;
}

void FsService::MkdirP(const std::string& path) {
  std::string p = path;
  for (size_t i = 1; i < p.size(); i++) {
    if (p[i] == '/') {
      p[i] = '\0';
      mkdir(p.c_str(), 0777);
      p[i] = '/';
    }
  }
  mkdir(p.c_str(), 0777);
}

bool FsService::IsDirectory(const std::string& path) const {
  struct stat st{};
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

int64_t FsService::FileSize(const std::string& path) const {
  struct stat st{};
  if (stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return -1;
  return static_cast<int64_t>(st.st_size);
}

}  // namespace services
