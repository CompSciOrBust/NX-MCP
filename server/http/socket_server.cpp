#include "socket_server.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <switch.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace http {

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

bool SocketServer::RecvExact(int fd, std::string& out, size_t n) {
  out.resize(n);
  size_t total = 0;
  while (total < n) {
    int got = ::recv(fd, out.data() + total, n - total, 0);
    if (got <= 0) return false;
    total += static_cast<size_t>(got);
  }
  return true;
}

bool SocketServer::ParseRequest(int fd, Request& req) {
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

  auto it = req.headers.find("content-length");
  if (it != req.headers.end()) {
    size_t len = std::stoul(it->second);
    if (len > 0 && len <= 1024 * 1024) {
      if (!RecvExact(fd, req.body, len)) return false;
    }
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
  if (!ParseRequest(fd, req)) { ::close(fd); return; }

  Response resp = handler_ ? handler_(req) : Response::NotFound();
  SendResponse(fd, resp);
  ::close(fd);
}

void SocketServer::Run(uint16_t port) {
  int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0)
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

  int opt = 1;
  ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port        = htons(port);

  if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));
  if (::listen(server_fd, 8) < 0)
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

  while (true) {
    int client_fd = ::accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) {
      svcSleepThread(1000000LL);
      continue;
    }
    HandleConnection(client_fd);
  }
}

}  // namespace http
