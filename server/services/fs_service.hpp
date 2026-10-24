#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace services {

struct DirEntry {
  std::string name;
  bool is_dir;
  int64_t size;
};

class FsService {
 public:
  static FsService& Instance();

  static constexpr size_t kChunkSize = 65536;

  // Reads up to `length` bytes from `path` at `offset` into `*out`.
  // Caps length at kChunkSize. Returns false on open failure.
  bool ReadFile(const std::string& path, int64_t offset, int64_t length,
                std::string* out);

  // Overwrites `path` with `len` bytes from `data`.
  // Returns false if the file cannot be opened or the write is incomplete.
  bool WriteFile(const std::string& path, const void* data, size_t len);

  // Streams `path` through on_chunk (called with each kChunkSize block).
  // on_chunk returns false to abort. Returns false if the file cannot be opened.
  // Not re-entrant — shares an internal buffer.
  bool StreamRead(const std::string& path,
                  std::function<bool(const char*, size_t)> on_chunk);

  // Receives `len` bytes via on_recv and writes them to `path`.
  // Returns false if the file cannot be opened or a receive/write error occurs.
  // Not re-entrant — shares an internal buffer.
  bool StreamWrite(const std::string& path, size_t len,
                   std::function<ssize_t(char*, size_t)> on_recv);

  bool ListDir(const std::string& path, std::vector<DirEntry>* out);
  void MkdirP(const std::string& path);
  bool IsDirectory(const std::string& path) const;
  int64_t FileSize(const std::string& path) const;  // -1 if not found or is a directory

 private:
  FsService() = default;
  char buf_[kChunkSize];
};

}  // namespace services
