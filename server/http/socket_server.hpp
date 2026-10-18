#pragma once
#include "server.hpp"

namespace http {

class SocketServer : public IServer {
 public:
  void SetHandler(Handler handler) override;
  void Run(uint16_t port) override;

 private:
  Handler handler_;

  static bool RecvLine(int fd, std::string& out);
  static bool ParseHeaders(int fd, Request& req);
  static void SendResponse(int fd, const Response& resp);
  void HandleConnection(int fd);
};

}  // namespace http
