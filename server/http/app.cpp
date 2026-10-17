#include "app.hpp"

namespace http {

App::App(std::unique_ptr<IServer> server) : server_(std::move(server)) {
  server_->SetHandler([this](const Request& req) {
    return controller_.Handle(req);
  });
}

void App::Run(uint16_t port) {
  server_->Run(port);
}

}  // namespace http
