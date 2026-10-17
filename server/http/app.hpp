#pragma once
#include <cstdint>
#include <memory>
#include "server.hpp"
#include "mcp_controller.hpp"

namespace http {

class App {
 public:
  explicit App(std::unique_ptr<IServer> server);
  void Run(uint16_t port);

 private:
  std::unique_ptr<IServer> server_;
  controllers::McpController controller_;
};

}  // namespace http
