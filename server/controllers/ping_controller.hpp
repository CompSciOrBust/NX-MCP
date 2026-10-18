#pragma once
#include "controller.hpp"

namespace controllers {

class PingController : public IController {
 public:
  std::string Path() const override { return "/ping"; }
  http::Response Handle(const http::Request& req) override;
};

}  // namespace controllers
