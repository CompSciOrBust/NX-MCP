#pragma once
#include <functional>
#include <cstddef>
#include <sys/types.h>

#include "error_codes.hpp"

namespace services {

class NspInstallService {
 public:
  NspInstallStatus Install(
      size_t total_size,
      uint8_t storage_id,
      std::function<ssize_t(char*, size_t)> on_recv);
};

}  // namespace services
