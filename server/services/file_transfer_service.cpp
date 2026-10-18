#include "file_transfer_service.hpp"
#include <sys/stat.h>
#include <switch.h>
#include <cstdio>
#include <string>

namespace services {

static constexpr size_t kChunkSize = 65536;
static char buf[kChunkSize];

static Result FsInit() {
  Result rc = fsInitialize();
  if (R_FAILED(rc)) return rc;
  rc = fsdevMountSdmc();
  if (R_FAILED(rc)) { fsExit(); return rc; }
  return 0;
}

static void FsDeinit() {
  fsdevUnmountAll();
  fsExit();
}

TransferStatus FileTransferService::Download(
    const std::string& path,
    std::function<void(size_t)> on_ready,
    std::function<bool(const char*, size_t)> on_chunk) {
  if (R_FAILED(FsInit())) return TransferStatus::FsInit;

  struct stat st{};
  if (stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) {
    FsDeinit();
    return TransferStatus::NotFound;
  }

  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) {
    FsDeinit();
    return TransferStatus::OpenError;
  }

  on_ready(static_cast<size_t>(st.st_size));

  size_t n;
  while ((n = fread(buf, 1, kChunkSize, fp)) > 0)
    if (!on_chunk(buf, n)) break;

  fclose(fp);
  FsDeinit();
  return TransferStatus::Ok;
}

TransferStatus FileTransferService::Upload(
    const std::string& path, size_t len,
    std::function<ssize_t(char*, size_t)> on_recv) {
  if (R_FAILED(FsInit())) return TransferStatus::FsInit;

  FILE* fp = fopen(path.c_str(), "wb");
  if (!fp) {
    FsDeinit();
    return TransferStatus::OpenError;
  }

  size_t remaining = len;
  bool ok = true;
  while (remaining > 0 && ok) {
    size_t to_recv = remaining < kChunkSize ? remaining : kChunkSize;
    ssize_t got = on_recv(buf, to_recv);
    if (got <= 0 || fwrite(buf, 1, got, fp) != static_cast<size_t>(got)) {
      ok = false;
      break;
    }
    remaining -= got;
  }

  fclose(fp);
  FsDeinit();
  return ok ? TransferStatus::Ok : TransferStatus::Incomplete;
}

}  // namespace services
