#include "app.hpp"
#include <sys/socket.h>
#include <string>

namespace http {

App::App(std::unique_ptr<IServer> server) : server_(std::move(server)),
    controllers_({&mcp_controller_, &ping_controller_}) {
  server_->SetHandler([this](const Request& req) -> Response {
    // File transfer routes stream directly over req.fd — no body buffering
    if (req.path.rfind("/fs/", 0) == 0)
      return file_transfer_controller_.Handle(req);

    // Normal routes: buffer body from fd before dispatching
    Request r = req;
    auto it = req.headers.find("content-length");
    if (it != req.headers.end()) {
      size_t len = std::stoul(it->second);
      if (len > 0 && len <= 1024 * 1024) {
        r.body.resize(len);
        size_t total = 0;
        while (total < len) {
          int got = ::recv(req.fd, &r.body[total], len - total, 0);
          if (got <= 0) return Response::BadRequest("incomplete body");
          total += got;
        }
      }
    }

    for (auto* c : controllers_)
      if (r.path.rfind(c->Path(), 0) == 0) return c->Handle(r);
    return Response::NotFound();
  });
}

void App::Run(uint16_t port) {
  server_->Run(port);
}

}  // namespace http
