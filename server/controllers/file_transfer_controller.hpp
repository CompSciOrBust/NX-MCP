#pragma once
#include "controller.hpp"
#include "types.hpp"
#include "file_transfer_service.hpp"
#include <string>

namespace controllers {

class FileTransferController : public IController {
 public:
  std::string Path() const override { return "/fs/"; }
  bool StreamsBody() const override { return true; }
  http::Response Handle(const http::Request& req) override;

 private:
  services::FileTransferService service_;
  static bool SendAll(int fd, const char* data, size_t len);
  static std::string ParsePathParam(const std::string& qs);
  static std::string UrlDecode(const std::string& s);
};

}  // namespace controllers
