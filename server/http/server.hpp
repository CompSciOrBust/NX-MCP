#pragma once
#include "types.hpp"
#include <cstdint>
#include <functional>

namespace http {

class IServer {
 public:
  using Handler = std::function<Response(const Request&)>;
  virtual ~IServer() = default;
  virtual void SetHandler(Handler handler) = 0;
  virtual void Run(uint16_t port) = 0;
};

}  // namespace http
