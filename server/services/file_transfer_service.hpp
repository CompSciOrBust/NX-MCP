#pragma once
#include <cstddef>
#include <functional>
#include <string>

namespace services {

enum class TransferStatus { Ok, FsInit, NotFound, OpenError, Incomplete };

class FileTransferService {
 public:
  TransferStatus Download(const std::string& path,
                          std::function<void(size_t)> on_ready,
                          std::function<bool(const char*, size_t)> on_chunk);
  TransferStatus Upload(const std::string& path, size_t len,
                        std::function<ssize_t(char*, size_t)> on_recv);
};

}  // namespace services
