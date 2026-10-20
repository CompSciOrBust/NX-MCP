#include "../../services/file_transfer_service.hpp"
#include "../../services/nsp_install_service.hpp"
#include <functional>
#include <string>

namespace mock {

services::TransferStatus   download_status = services::TransferStatus::Ok;
services::TransferStatus   upload_status   = services::TransferStatus::Ok;
std::string                last_path;
size_t                     last_len = 0;
services::NspInstallStatus nsp_status      = services::NspInstallStatus::kOk;
uint8_t                    last_storage_id = 0;
size_t                     last_nsp_len    = 0;

void Reset() {
  download_status = services::TransferStatus::Ok;
  upload_status   = services::TransferStatus::Ok;
  last_path.clear();
  last_len        = 0;
  nsp_status      = services::NspInstallStatus::kOk;
  last_storage_id = 0;
  last_nsp_len    = 0;
}

}  // namespace mock

namespace services {

TransferStatus FileTransferService::Download(
    const std::string& path,
    std::function<void(size_t)>,
    std::function<bool(const char*, size_t)>) {
  mock::last_path = path;
  return mock::download_status;
}

TransferStatus FileTransferService::Upload(
    const std::string& path, size_t len,
    std::function<ssize_t(char*, size_t)>) {
  mock::last_path = path;
  mock::last_len  = len;
  return mock::upload_status;
}

NspInstallStatus NspInstallService::Install(
    size_t total_size, uint8_t storage_id,
    std::function<ssize_t(char*, size_t)>) {
  mock::last_nsp_len    = total_size;
  mock::last_storage_id = storage_id;
  return mock::nsp_status;
}

}  // namespace services
