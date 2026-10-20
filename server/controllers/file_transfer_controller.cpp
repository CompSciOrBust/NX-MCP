#include "file_transfer_controller.hpp"
#include <switch.h>
#include <sys/socket.h>
#include <cstdlib>
#include <string>

namespace controllers {

bool FileTransferController::SendAll(int fd, const char* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    int r = ::send(fd, data + sent, len - sent, 0);
    if (r <= 0) return false;
    sent += r;
  }
  return true;
}

std::string FileTransferController::UrlDecode(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      char hex[3] = {s[i + 1], s[i + 2], '\0'};
      char* end;
      long val = strtol(hex, &end, 16);
      if (end == hex + 2) {
        out += static_cast<char>(val);
        i += 2;
        continue;
      }
    }
    out += s[i];
  }
  return out;
}

std::string FileTransferController::ParsePathParam(const std::string& qs) {
  const std::string key = "path=";
  auto pos = qs.find(key);
  if (pos == std::string::npos) return {};
  pos += key.size();
  auto end = qs.find('&', pos);
  return UrlDecode(qs.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
}

std::string FileTransferController::ParseStorageParam(const std::string& qs) {
  const std::string key = "storage=";
  auto pos = qs.find(key);
  if (pos == std::string::npos) return {};
  pos += key.size();
  auto end = qs.find('&', pos);
  return qs.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

http::Response FileTransferController::Handle(const http::Request& req) {
  if (req.path == "/fs/download") {
    if (req.method != "GET") return http::Response::MethodNotAllowed();
    std::string path = ParsePathParam(req.query_string);
    if (path.empty()) return http::Response::BadRequest("missing path parameter");

    int fd = req.fd;
    auto status = file_service_.Download(path,
      [fd](size_t size) {
        std::string h = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                        "Content-Length: " + std::to_string(size) +
                        "\r\nConnection: close\r\n\r\n";
        SendAll(fd, h.data(), h.size());
      },
      [fd](const char* data, size_t n) -> bool {
        return SendAll(fd, data, n);
      });

    if (status == services::TransferStatus::NotFound)
      return http::Response::NotFound();
    if (status != services::TransferStatus::Ok)
      return {500, "text/plain", "internal error"};
    return http::Response::Raw();
  }

  if (req.path == "/fs/upload") {
    if (req.method != "PUT") return http::Response::MethodNotAllowed();
    std::string path = ParsePathParam(req.query_string);
    if (path.empty()) return http::Response::BadRequest("missing path parameter");
    size_t len = 0;
    auto it = req.headers.find("content-length");
    if (it != req.headers.end()) {
      try { len = std::stoul(it->second); } catch (...) {}
    }

    int fd = req.fd;
    auto status = file_service_.Upload(path, len,
      [fd](char* buf, size_t max) -> ssize_t {
        return ::recv(fd, buf, max, 0);
      });

    if (status == services::TransferStatus::Incomplete)
      return {500, "text/plain", "transfer incomplete"};
    if (status != services::TransferStatus::Ok)
      return {500, "text/plain", "internal error"};
    return http::Response::Ok("OK", "text/plain");
  }

  if (req.path == "/fs/install/nsp") {
    if (req.method != "PUT") return http::Response::MethodNotAllowed();

    std::string storage_val = ParseStorageParam(req.query_string);
    uint8_t storage_id = NcmStorageId_SdCard;
    if (storage_val == "nand") storage_id = NcmStorageId_BuiltInUser;

    size_t len = 0;
    auto it = req.headers.find("content-length");
    if (it != req.headers.end()) {
      try { len = std::stoul(it->second); } catch (...) {}
    }

    int fd = req.fd;
    auto status = nsp_service_.Install(len, storage_id,
      [fd](char* buf, size_t max) -> ssize_t {
        return ::recv(fd, buf, max, 0);
      });

    if (status == services::NspInstallStatus::kOk)
      return http::Response::Ok("{\"success\":true}", "application/json");

    const char* error = "unknown";
    switch (status) {
      case services::NspInstallStatus::kInvalidPfs0:        error = "invalid_pfs0"; break;
      case services::NspInstallStatus::kTooManyFiles:       error = "too_many_files"; break;
      case services::NspInstallStatus::kNcaInstallFailed:   error = "nca_install_failed"; break;
      case services::NspInstallStatus::kCnmtMissing:        error = "cnmt_missing"; break;
      case services::NspInstallStatus::kCnmtReadFailed:     error = "cnmt_read_failed"; break;
      case services::NspInstallStatus::kCnmtRegisterFailed: error = "cnmt_register_failed"; break;
      case services::NspInstallStatus::kRecordFailed:       error = "record_failed"; break;
      case services::NspInstallStatus::kNetworkError:       error = "network_error"; break;
      default: break;
    }
    return http::Response::Ok(
      std::string("{\"success\":false,\"error\":\"") + error + "\"}",
      "application/json");
  }

  return http::Response::NotFound();
}

}  // namespace controllers
