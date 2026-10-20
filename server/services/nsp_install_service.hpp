#pragma once
#include <functional>
#include <cstddef>
#include <sys/types.h>

namespace services {

enum class NspInstallStatus {
  kOk,
  kInvalidPfs0,
  kTooManyFiles,
  kNcaInstallFailed,
  kCnmtMissing,
  kCnmtReadFailed,
  kCnmtRegisterFailed,
  kRecordFailed,
  kNetworkError,
};

class NspInstallService {
 public:
  NspInstallStatus Install(
      size_t total_size,
      uint8_t storage_id,
      std::function<ssize_t(char*, size_t)> on_recv);
};

}  // namespace services
