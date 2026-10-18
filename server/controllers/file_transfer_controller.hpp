#pragma once
#include "types.hpp"
#include "file_transfer_service.hpp"
#include <string>

namespace controllers {

class FileTransferController {
 public:
  http::Response Handle(const http::Request& req);

 private:
  services::FileTransferService service_;
  static bool SendAll(int fd, const char* data, size_t len);
  static std::string ParsePathParam(const std::string& qs);
  static std::string UrlDecode(const std::string& s);
};

}  // namespace controllers
