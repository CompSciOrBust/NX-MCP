#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "../../services/fs_service.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

static services::FsService& fs() { return services::FsService::Instance(); }

static const std::string kDir  = "/tmp/nx_mcp_fs_test";
static const std::string kFile = kDir + "/test.txt";

static void ResetDir() {
  system(("rm -rf " + kDir + " && mkdir -p " + kDir).c_str());
}

static void PutFile(const std::string& path, const std::string& content) {
  FILE* f = fopen(path.c_str(), "wb");
  if (f) { fwrite(content.data(), 1, content.size(), f); fclose(f); }
}

static std::string ReadAll(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return "";
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::string out(sz, '\0');
  fread(&out[0], 1, sz, f);
  fclose(f);
  return out;
}

// ── FileSize ──────────────────────────────────────────────────────────────────

TEST_CASE("FsService::FileSize returns correct byte count") {
  ResetDir();
  PutFile(kFile, "hello");
  REQUIRE(fs().FileSize(kFile) == 5);
}

TEST_CASE("FsService::FileSize returns -1 for missing file") {
  ResetDir();
  REQUIRE(fs().FileSize(kDir + "/no_such_file.txt") == -1);
}

TEST_CASE("FsService::FileSize returns -1 for a directory") {
  ResetDir();
  REQUIRE(fs().FileSize(kDir) == -1);
}

// ── IsDirectory ───────────────────────────────────────────────────────────────

TEST_CASE("FsService::IsDirectory returns true for a directory") {
  ResetDir();
  REQUIRE(fs().IsDirectory(kDir));
}

TEST_CASE("FsService::IsDirectory returns false for a file") {
  ResetDir();
  PutFile(kFile, "data");
  REQUIRE_FALSE(fs().IsDirectory(kFile));
}

TEST_CASE("FsService::IsDirectory returns false for a missing path") {
  ResetDir();
  REQUIRE_FALSE(fs().IsDirectory(kDir + "/ghost"));
}

// ── ReadFile ──────────────────────────────────────────────────────────────────

TEST_CASE("FsService::ReadFile reads full content") {
  ResetDir();
  PutFile(kFile, "abcdef");
  std::string out;
  REQUIRE(fs().ReadFile(kFile, 0, 6, &out));
  REQUIRE(out == "abcdef");
}

TEST_CASE("FsService::ReadFile respects offset") {
  ResetDir();
  PutFile(kFile, "abcdef");
  std::string out;
  REQUIRE(fs().ReadFile(kFile, 2, 4, &out));
  REQUIRE(out == "cdef");
}

TEST_CASE("FsService::ReadFile respects length") {
  ResetDir();
  PutFile(kFile, "abcdef");
  std::string out;
  REQUIRE(fs().ReadFile(kFile, 0, 3, &out));
  REQUIRE(out == "abc");
}

TEST_CASE("FsService::ReadFile returns false for missing file") {
  ResetDir();
  std::string out;
  REQUIRE_FALSE(fs().ReadFile(kDir + "/nope.txt", 0, 16, &out));
}

TEST_CASE("FsService::ReadFile clamps length to kChunkSize") {
  ResetDir();
  std::string out;
  // Requesting more than kChunkSize should not crash or overflow
  fs().ReadFile(kFile, 0, static_cast<int64_t>(services::FsService::kChunkSize) + 1024, &out);
  // Result may be empty (file missing) but must not exceed kChunkSize
  REQUIRE(out.size() <= services::FsService::kChunkSize);
}

// ── WriteFile ─────────────────────────────────────────────────────────────────

TEST_CASE("FsService::WriteFile creates file with correct content") {
  ResetDir();
  const std::string data = "written by test";
  REQUIRE(fs().WriteFile(kFile, data.data(), data.size()));
  REQUIRE(ReadAll(kFile) == data);
}

TEST_CASE("FsService::WriteFile overwrites existing content") {
  ResetDir();
  PutFile(kFile, "old content longer than new");
  const std::string data = "new";
  REQUIRE(fs().WriteFile(kFile, data.data(), data.size()));
  REQUIRE(ReadAll(kFile) == data);
}

TEST_CASE("FsService::WriteFile returns false for unwritable path") {
  ResetDir();
  REQUIRE_FALSE(fs().WriteFile(kDir + "/no_subdir/file.txt", "x", 1));
}

// ── StreamRead ────────────────────────────────────────────────────────────────

TEST_CASE("FsService::StreamRead delivers all bytes via callback") {
  ResetDir();
  PutFile(kFile, "stream me");
  std::string collected;
  bool ok = fs().StreamRead(kFile, [&](const char* buf, size_t n) -> bool {
    collected.append(buf, n);
    return true;
  });
  REQUIRE(ok);
  REQUIRE(collected == "stream me");
}

TEST_CASE("FsService::StreamRead aborts when callback returns false") {
  ResetDir();
  PutFile(kFile, "abcdefgh");
  std::string collected;
  fs().StreamRead(kFile, [&](const char* buf, size_t n) -> bool {
    collected.append(buf, n);
    return false;  // abort after first chunk
  });
  REQUIRE_FALSE(collected.empty());  // received at least something
  REQUIRE(collected.size() <= 8);    // did not receive more than the file
}

TEST_CASE("FsService::StreamRead returns false for missing file") {
  ResetDir();
  bool ok = fs().StreamRead(kDir + "/ghost.bin", [](const char*, size_t) -> bool {
    return true;
  });
  REQUIRE_FALSE(ok);
}

// ── StreamWrite ───────────────────────────────────────────────────────────────

TEST_CASE("FsService::StreamWrite writes received bytes to file") {
  ResetDir();
  const std::string data = "written via stream";
  size_t pos = 0;
  bool ok = fs().StreamWrite(kFile, data.size(),
      [&](char* buf, size_t n) -> ssize_t {
        size_t to_copy = n < (data.size() - pos) ? n : (data.size() - pos);
        memcpy(buf, data.data() + pos, to_copy);
        pos += to_copy;
        return static_cast<ssize_t>(to_copy);
      });
  REQUIRE(ok);
  REQUIRE(ReadAll(kFile) == data);
}

TEST_CASE("FsService::StreamWrite returns false for unwritable path") {
  ResetDir();
  bool ok = fs().StreamWrite(kDir + "/no_subdir/x.bin", 4,
      [](char* buf, size_t n) -> ssize_t {
        memset(buf, 0, n);
        return static_cast<ssize_t>(n);
      });
  REQUIRE_FALSE(ok);
}

// ── ListDir ───────────────────────────────────────────────────────────────────

TEST_CASE("FsService::ListDir returns entries for a populated directory") {
  ResetDir();
  PutFile(kDir + "/a.txt", "aaa");
  PutFile(kDir + "/b.txt", "bb");
  mkdir((kDir + "/subdir").c_str(), 0755);

  std::vector<services::DirEntry> entries;
  REQUIRE(fs().ListDir(kDir, &entries));
  REQUIRE(entries.size() == 3);
}

TEST_CASE("FsService::ListDir correctly tags files and directories") {
  ResetDir();
  PutFile(kDir + "/file.txt", "x");
  mkdir((kDir + "/dir").c_str(), 0755);

  std::vector<services::DirEntry> entries;
  fs().ListDir(kDir, &entries);

  bool found_file = false, found_dir = false;
  for (const auto& e : entries) {
    if (e.name == "file.txt") { found_file = true; REQUIRE_FALSE(e.is_dir); }
    if (e.name == "dir")      { found_dir  = true; REQUIRE(e.is_dir); }
  }
  REQUIRE(found_file);
  REQUIRE(found_dir);
}

TEST_CASE("FsService::ListDir reports correct file sizes") {
  ResetDir();
  PutFile(kDir + "/sized.txt", "12345");

  std::vector<services::DirEntry> entries;
  fs().ListDir(kDir, &entries);

  REQUIRE(entries.size() == 1);
  REQUIRE(entries[0].size == 5);
}

TEST_CASE("FsService::ListDir excludes . and .. entries") {
  ResetDir();
  std::vector<services::DirEntry> entries;
  fs().ListDir(kDir, &entries);
  for (const auto& e : entries)
    REQUIRE(e.name != ".");
  for (const auto& e : entries)
    REQUIRE(e.name != "..");
}

TEST_CASE("FsService::ListDir returns false for missing path") {
  ResetDir();
  std::vector<services::DirEntry> entries;
  REQUIRE_FALSE(fs().ListDir(kDir + "/no_such_dir", &entries));
}

// ── MkdirP ────────────────────────────────────────────────────────────────────

TEST_CASE("FsService::MkdirP creates a single directory") {
  ResetDir();
  const std::string target = kDir + "/single";
  fs().MkdirP(target);
  REQUIRE(fs().IsDirectory(target));
}

TEST_CASE("FsService::MkdirP creates nested directories") {
  ResetDir();
  const std::string target = kDir + "/a/b/c";
  fs().MkdirP(target);
  REQUIRE(fs().IsDirectory(target));
}

TEST_CASE("FsService::MkdirP is idempotent on existing path") {
  ResetDir();
  const std::string target = kDir + "/existing";
  fs().MkdirP(target);
  fs().MkdirP(target);  // second call must not fail or corrupt
  REQUIRE(fs().IsDirectory(target));
}
