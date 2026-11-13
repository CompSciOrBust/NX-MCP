#pragma once
#include <switch.h>
#include "server.hpp"

namespace http {

class SocketServer : public IServer {
 public:
  void SetHandler(Handler handler) override;
  void Run(uint16_t port) override;

  // Thread-safe sleep handling. Suspend() makes Run() close its sockets and
  // waits (bounded) until it holds none, so the console can sleep. Resume()
  // lets Run() recreate the listener.
  void Suspend();
  void Resume();

 private:
  Handler handler_;

  Mutex   mutex_     = {};
  CondVar cond_      = {};
  bool    suspended_ = false;
  bool    idle_      = false;  // Run() is parked in WaitWhileSuspended()

  static bool RecvLine(int fd, std::string& out);
  static bool ParseHeaders(int fd, Request& req);
  static void SendResponse(int fd, const Response& resp);
  void HandleConnection(int fd);
  static int CreateServerSocket(uint16_t port);
  bool IsSuspended();
  void WaitWhileSuspended();
  bool BeginConnection(int fd);
};

}  // namespace http
