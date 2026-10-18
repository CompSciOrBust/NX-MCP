#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "server.hpp"
#include "controller.hpp"
#include "mcp_controller.hpp"
#include "ping_controller.hpp"
#include "file_transfer_controller.hpp"

namespace http {

class App {
 public:
  explicit App(std::unique_ptr<IServer> server);
  void Run(uint16_t port);

 private:
  std::unique_ptr<IServer> server_;
  controllers::McpController          mcp_controller_;
  controllers::PingController         ping_controller_;
  controllers::FileTransferController file_transfer_controller_;
  std::vector<controllers::IController*> controllers_;
};

}  // namespace http
