#include "ping_controller.hpp"

namespace controllers {

http::Response PingController::Handle(const http::Request& req) {
  if (req.path != "/ping")   return http::Response::NotFound();
  if (req.method != "GET")   return http::Response::MethodNotAllowed();
  return http::Response::Ok("pong", "text/plain");
}

}  // namespace controllers
