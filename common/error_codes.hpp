#pragma once

namespace mcp::errors {

inline constexpr int kParseError = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kResourceNotFound = -32002;
inline constexpr int kUnsupportedProtocolVersion = -32022;

}  // namespace mcp::errors

namespace http::errors {

inline constexpr int kBadRequest = 400;
inline constexpr int kNotFound = 404;
inline constexpr int kMethodNotAllowed = 405;
inline constexpr int kInternalServerError = 500;

}  // namespace http::errors

namespace ipc::errors {

inline constexpr unsigned kCmifModule = 11;
inline constexpr unsigned kUnknownCommandDescription = 403;

}  // namespace ipc::errors

namespace services {

enum class TransferStatus { kOk, kFsInit, kNotFound, kOpenError, kIncomplete };

enum class NspInstallStatus {
  kOk,
  kInvalidPfs0,
  kTooManyFiles,
  kNcaInstallFailed,
  kCnmtMissing,
  kCnmtReadFailed,
  kCnmtRegisterFailed,
  kRecordFailed,
  kNetworkError,
};

}  // namespace services

namespace nsp::errors {

inline constexpr char kInvalidPfs0[] = "invalid_pfs0";
inline constexpr char kTooManyFiles[] = "too_many_files";
inline constexpr char kNcaInstallFailed[] = "nca_install_failed";
inline constexpr char kCnmtMissing[] = "cnmt_missing";
inline constexpr char kCnmtReadFailed[] = "cnmt_read_failed";
inline constexpr char kCnmtRegisterFailed[] = "cnmt_register_failed";
inline constexpr char kRecordFailed[] = "record_failed";
inline constexpr char kNetworkError[] = "network_error";
inline constexpr char kUnknown[] = "unknown";

}  // namespace nsp::errors
