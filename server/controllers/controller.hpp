#pragma once
#include <string>
#include "types.hpp"

namespace controllers {

class IController {
 public:
  virtual ~IController() = default;
  virtual std::string Path() const = 0;
  virtual http::Response Handle(const http::Request& req) = 0;
};

}  // namespace controllers
