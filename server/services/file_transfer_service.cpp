#include "file_transfer_service.hpp"
#include "fs_service.hpp"
#include "led_notif.hpp"

namespace services {

TransferStatus FileTransferService::Download(
    const std::string& path,
    std::function<void(size_t)> on_ready,
    std::function<bool(const char*, size_t)> on_chunk) {
  led::LedGuard guard;
  int64_t size = FsService::Instance().FileSize(path);
  if (size < 0) return TransferStatus::NotFound;
  on_ready(static_cast<size_t>(size));
  if (!FsService::Instance().StreamRead(path, on_chunk))
    return TransferStatus::OpenError;
  return TransferStatus::Ok;
}

TransferStatus FileTransferService::Upload(
    const std::string& path, size_t len,
    std::function<ssize_t(char*, size_t)> on_recv) {
  led::LedGuard guard;
  if (!FsService::Instance().StreamWrite(path, len, on_recv))
    return TransferStatus::Incomplete;
  return TransferStatus::Ok;
}

}  // namespace services
