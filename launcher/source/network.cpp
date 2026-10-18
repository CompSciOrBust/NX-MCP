#include "network.hpp"
#include "constants.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <strings.h>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

char chunk[kUploadChunk];

ssize_t RecvLine(int fd, char* buf, size_t max) {
  size_t n = 0;
  char c;
  while (n < max - 1) {
    if (recv(fd, &c, 1, 0) <= 0) return -1;
    if (c == '\n') {
      if (n > 0 && buf[n - 1] == '\r') n--;
      break;
    }
    buf[n++] = c;
  }
  buf[n] = '\0';
  return (ssize_t)n;
}

void SendResponse(int fd, int status) {
  char buf[64];
  snprintf(buf, sizeof(buf),
    "HTTP/1.1 %d %s\r\nContent-Length: 0\r\n\r\n",
    status, status == 200 ? "OK" : "Error");
  send(fd, buf, strlen(buf), 0);
}

} // namespace

std::string GetLocalIp() {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) return "?.?.?.?";

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port   = htons(1);
  inet_pton(AF_INET, "1.1.1.1", &addr.sin_addr);

  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(sock);
    return "?.?.?.?";
  }

  struct sockaddr_in local{};
  socklen_t len = sizeof(local);
  getsockname(sock, (struct sockaddr*)&local, &len);
  close(sock);

  char buf[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
  return buf;
}

bool HandleUpload(int fd) {
  char line[512];
  size_t content_length = 0;

  if (RecvLine(fd, line, sizeof(line)) < 0) {
    SendResponse(fd, 400);
    return false;
  }

  while (true) {
    ssize_t n = RecvLine(fd, line, sizeof(line));
    if (n < 0) return false;
    if (n == 0) break;
    if (strncasecmp(line, "Content-Length:", 15) == 0)
      content_length = strtoul(line + 15, nullptr, 10);
  }

  if (content_length == 0) {
    SendResponse(fd, 400);
    return false;
  }

  FILE* f = fopen(kNspPath, "wb");
  if (!f) {
    SendResponse(fd, 500);
    return false;
  }

  size_t remaining = content_length;
  bool ok = true;
  while (remaining > 0) {
    size_t want = remaining < kUploadChunk ? remaining : kUploadChunk;
    ssize_t got = recv(fd, chunk, want, 0);
    if (got <= 0) { ok = false; break; }
    if (fwrite(chunk, 1, (size_t)got, f) != (size_t)got) { ok = false; break; }
    remaining -= (size_t)got;
  }
  fclose(f);

  SendResponse(fd, ok ? 200 : 500);
  return ok;
}

bool InstallNsp() {
  mkdir("sdmc:/atmosphere", 0777);
  mkdir("sdmc:/atmosphere/contents", 0777);
  mkdir("sdmc:/atmosphere/contents/0100000000057331", 0777);

  FILE* src = fopen("romfs:/exefs.nsp", "rb");
  if (!src) return false;

  FILE* dst = fopen(kNspPath, "wb");
  if (!dst) { fclose(src); return false; }

  bool ok = true;
  size_t n;
  while ((n = fread(chunk, 1, kUploadChunk, src)) > 0) {
    if (fwrite(chunk, 1, n, dst) != n) { ok = false; break; }
  }
  fclose(src);
  fclose(dst);
  if (!ok) remove(kNspPath);
  return ok;
}

int SetupUploadServer() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port        = htons(kUploadPort);

  if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
      listen(fd, 4) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}
