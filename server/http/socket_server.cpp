#include "socket_server.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <switch.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace http {

// Lets Run() notice Suspend() without depending on the listener being woken.
static constexpr int kAcceptPollMs     = 250;
// A client that stops talking (e.g. its end vanished while the console was
// asleep) must not wedge the single serving thread forever.
static constexpr int kClientTimeoutSec = 30;
// Upper bound on how long Suspend() holds up the sleep transition.
static constexpr u64 kSuspendTimeoutNs = 3000000000ULL;

static std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

static std::string Trim(const std::string& s) {
  auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

bool SocketServer::RecvLine(int fd, std::string& out) {
  out.clear();
  char c;
  while (out.size() < 8192) {
    if (::recv(fd, &c, 1, 0) <= 0) return false;
    if (c == '\n') {
      if (!out.empty() && out.back() == '\r') out.pop_back();
      return true;
    }
    out += c;
  }
  return false;
}

bool SocketServer::ParseHeaders(int fd, Request& req) {
  std::string line;
  if (!RecvLine(fd, line) || line.empty()) return false;

  auto sp1 = line.find(' ');
  if (sp1 == std::string::npos) return false;
  auto sp2 = line.find(' ', sp1 + 1);
  if (sp2 == std::string::npos) return false;

  req.method = line.substr(0, sp1);
  std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
  auto q = target.find('?');
  if (q != std::string::npos) {
    req.path         = target.substr(0, q);
    req.query_string = target.substr(q + 1);
  } else {
    req.path = target;
  }

  while (true) {
    if (!RecvLine(fd, line)) return false;
    if (line.empty()) break;
    auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    req.headers[ToLower(Trim(line.substr(0, colon)))] = Trim(line.substr(colon + 1));
  }

  return true;
}

void SocketServer::SendResponse(int fd, const Response& resp) {
  const char* reason = "Unknown";
  switch (resp.status) {
    case 200: reason = "OK";                    break;
    case 204: reason = "No Content";            break;
    case 400: reason = "Bad Request";           break;
    case 404: reason = "Not Found";             break;
    case 405: reason = "Method Not Allowed";    break;
    case 500: reason = "Internal Server Error"; break;
  }

  std::string out;
  out.reserve(256 + resp.body.size());
  out += "HTTP/1.1 ";
  out += std::to_string(resp.status);
  out += ' ';
  out += reason;
  out += "\r\n";
  out += "Connection: close\r\n";
  out += "Access-Control-Allow-Origin: *\r\n";
  out += "Access-Control-Allow-Methods: POST, OPTIONS\r\n";
  out += "Access-Control-Allow-Headers: Content-Type, Accept, Mcp-Session-Id\r\n";
  if (!resp.content_type.empty()) {
    out += "Content-Type: ";
    out += resp.content_type;
    out += "\r\n";
  }
  out += "Content-Length: ";
  out += std::to_string(resp.body.size());
  out += "\r\n\r\n";
  out += resp.body;

  ::send(fd, out.data(), out.size(), 0);
}

void SocketServer::SetHandler(Handler handler) {
  handler_ = std::move(handler);
}

void SocketServer::HandleConnection(int fd) {
  Request req;
  if (!ParseHeaders(fd, req)) { ::close(fd); return; }
  req.fd = fd;

  Response resp = handler_ ? handler_(req) : Response::NotFound();
  if (resp.status != 0)
    SendResponse(fd, resp);
  ::close(fd);
}

int SocketServer::CreateServerSocket(uint16_t port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  int opt = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port        = htons(port);

  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
      ::listen(fd, 8) < 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

bool SocketServer::IsSuspended() {
  mutexLock(&mutex_);
  bool suspended = suspended_;
  mutexUnlock(&mutex_);
  return suspended;
}

void SocketServer::WaitWhileSuspended() {
  mutexLock(&mutex_);
  if (suspended_) {
    idle_ = true;
    condvarWakeAll(&cond_);
    while (suspended_) condvarWait(&cond_, &mutex_);
    idle_ = false;
  }
  mutexUnlock(&mutex_);
}

bool SocketServer::BeginConnection(int fd) {
  timeval timeout{};
  timeout.tv_sec = kClientTimeoutSec;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  return !IsSuspended();
}

void SocketServer::Suspend() {
  mutexLock(&mutex_);
  suspended_ = true;
  // No socket calls here: this runs on the PSC thread, and bsd may already be
  // asleep. Run() polls the listener and closes everything itself; a request
  // in progress is bounded by the client timeouts.

  u64 deadline = armGetSystemTick() + armNsToTicks(kSuspendTimeoutNs);
  while (!idle_) {
    u64 now = armGetSystemTick();
    if (now >= deadline) break;
    condvarWaitTimeout(&cond_, &mutex_, armTicksToNs(deadline - now));
  }
  mutexUnlock(&mutex_);
}

void SocketServer::Resume() {
  mutexLock(&mutex_);
  suspended_ = false;
  condvarWakeAll(&cond_);
  mutexUnlock(&mutex_);
}

void SocketServer::Run(uint16_t port) {
  while (true) {
    WaitWhileSuspended();

    int server_fd = CreateServerSocket(port);
    if (server_fd < 0) {
      svcSleepThread(1000000000LL);
      continue;
    }

    int failures = 0;
    while (failures < 10 && !IsSuspended()) {
      pollfd pfd{server_fd, POLLIN, 0};
      int ready = ::poll(&pfd, 1, kAcceptPollMs);
      if (ready == 0) continue;

      int client_fd = ready > 0 ? ::accept(server_fd, nullptr, nullptr) : -1;
      if (client_fd < 0) {
        failures++;
        svcSleepThread(100000000LL);
        continue;
      }
      failures = 0;

      if (!BeginConnection(client_fd)) {
        ::close(client_fd);
        break;
      }
      HandleConnection(client_fd);
    }

    // Consecutive accept failures - socket is likely dead after sleep, recreate it
    // (or we were suspended for sleep, see Suspend())
    ::close(server_fd);
    if (!IsSuspended()) svcSleepThread(500000000LL);
  }
}

}  // namespace http
