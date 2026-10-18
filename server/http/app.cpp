#include "app.hpp"

namespace http {

App::App(std::unique_ptr<IServer> server) : server_(std::move(server)),
    controllers_({&mcp_controller_, &ping_controller_}) {
  server_->SetHandler([this](const Request& req) {
    for (auto* c : controllers_)
      if (req.path.rfind(c->Path(), 0) == 0) return c->Handle(req);
    return Response::NotFound();
  });
}

void App::Run(uint16_t port) {
  server_->Run(port);
}

}  // namespace http
