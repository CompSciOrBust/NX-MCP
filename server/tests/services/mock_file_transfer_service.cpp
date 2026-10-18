#include "../../services/file_transfer_service.hpp"
#include <functional>
#include <string>

namespace mock {

services::TransferStatus g_download_status = services::TransferStatus::Ok;
services::TransferStatus g_upload_status   = services::TransferStatus::Ok;
std::string              g_last_path;
size_t                   g_last_len = 0;

void Reset() {
  g_download_status = services::TransferStatus::Ok;
  g_upload_status   = services::TransferStatus::Ok;
  g_last_path.clear();
  g_last_len = 0;
}

}  // namespace mock

namespace services {

TransferStatus FileTransferService::Download(
    const std::string& path,
    std::function<void(size_t)>,
    std::function<bool(const char*, size_t)>) {
  mock::g_last_path = path;
  return mock::g_download_status;
}

TransferStatus FileTransferService::Upload(
    const std::string& path, size_t len,
    std::function<ssize_t(char*, size_t)>) {
  mock::g_last_path = path;
  mock::g_last_len  = len;
  return mock::g_upload_status;
}

}  // namespace services
