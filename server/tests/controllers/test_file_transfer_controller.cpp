#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <switch.h>
#include "../../controllers/file_transfer_controller.hpp"
#include "../../services/nsp_install_service.hpp"

namespace mock {
extern services::TransferStatus   download_status;
extern services::TransferStatus   upload_status;
extern std::string                last_path;
extern size_t                     last_len;
extern services::NspInstallStatus nsp_status;
extern uint8_t                    last_storage_id;
extern size_t                     last_nsp_len;
void Reset();
}

static http::Request MakeReq(std::string method, std::string path, std::string qs = {}) {
  http::Request r;
  r.method       = std::move(method);
  r.path         = std::move(path);
  r.query_string = std::move(qs);
  r.fd           = -1;
  return r;
}

struct Fixture {
  Fixture() { mock::Reset(); }
  controllers::FileTransferController ctrl_;
};

// ── IController interface ────────────────────────────────────────────────────

TEST_CASE_METHOD(Fixture, "Path returns /fs/") {
  REQUIRE(ctrl_.Path() == "/fs/");
}

TEST_CASE_METHOD(Fixture, "StreamsBody returns true") {
  REQUIRE(ctrl_.StreamsBody() == true);
}

// ── routing / method validation ──────────────────────────────────────────────

TEST_CASE_METHOD(Fixture, "Download: wrong method returns 405") {
  REQUIRE(ctrl_.Handle(MakeReq("PUT", "/fs/download", "path=foo")).status == 405);
}

TEST_CASE_METHOD(Fixture, "Download: missing path param returns 400") {
  auto resp = ctrl_.Handle(MakeReq("GET", "/fs/download"));
  REQUIRE(resp.status == 400);
  REQUIRE(resp.body == "missing path parameter");
}

TEST_CASE_METHOD(Fixture, "Upload: wrong method returns 405") {
  REQUIRE(ctrl_.Handle(MakeReq("GET", "/fs/upload", "path=foo")).status == 405);
}

TEST_CASE_METHOD(Fixture, "Upload: missing path param returns 400") {
  auto resp = ctrl_.Handle(MakeReq("PUT", "/fs/upload"));
  REQUIRE(resp.status == 400);
  REQUIRE(resp.body == "missing path parameter");
}

TEST_CASE_METHOD(Fixture, "Unknown /fs/ path returns 404") {
  REQUIRE(ctrl_.Handle(MakeReq("GET", "/fs/other")).status == 404);
}

// ── download service status mapping ─────────────────────────────────────────

TEST_CASE_METHOD(Fixture, "Download: Ok returns Raw response") {
  REQUIRE(ctrl_.Handle(MakeReq("GET", "/fs/download", "path=foo")).status == 0);
}

TEST_CASE_METHOD(Fixture, "Download: NotFound returns 404") {
  mock::download_status = services::TransferStatus::NotFound;
  REQUIRE(ctrl_.Handle(MakeReq("GET", "/fs/download", "path=foo")).status == 404);
}

TEST_CASE_METHOD(Fixture, "Download: FsInit error returns 500") {
  mock::download_status = services::TransferStatus::FsInit;
  REQUIRE(ctrl_.Handle(MakeReq("GET", "/fs/download", "path=foo")).status == 500);
}

TEST_CASE_METHOD(Fixture, "Download: OpenError returns 500") {
  mock::download_status = services::TransferStatus::OpenError;
  REQUIRE(ctrl_.Handle(MakeReq("GET", "/fs/download", "path=foo")).status == 500);
}

// ── upload service status mapping ────────────────────────────────────────────

TEST_CASE_METHOD(Fixture, "Upload: Ok returns 200") {
  REQUIRE(ctrl_.Handle(MakeReq("PUT", "/fs/upload", "path=foo")).status == 200);
}

TEST_CASE_METHOD(Fixture, "Upload: Incomplete returns 500 with message") {
  mock::upload_status = services::TransferStatus::Incomplete;
  auto resp = ctrl_.Handle(MakeReq("PUT", "/fs/upload", "path=foo"));
  REQUIRE(resp.status == 500);
  REQUIRE(resp.body == "transfer incomplete");
}

TEST_CASE_METHOD(Fixture, "Upload: FsInit error returns 500") {
  mock::upload_status = services::TransferStatus::FsInit;
  REQUIRE(ctrl_.Handle(MakeReq("PUT", "/fs/upload", "path=foo")).status == 500);
}

TEST_CASE_METHOD(Fixture, "Upload: OpenError returns 500") {
  mock::upload_status = services::TransferStatus::OpenError;
  REQUIRE(ctrl_.Handle(MakeReq("PUT", "/fs/upload", "path=foo")).status == 500);
}

// ── URL decoding and path parsing ────────────────────────────────────────────

TEST_CASE_METHOD(Fixture, "Download: URL-encoded path is decoded") {
  ctrl_.Handle(MakeReq("GET", "/fs/download", "path=%2Fswitch%2Ftest.nro"));
  REQUIRE(mock::last_path == "/switch/test.nro");
}

TEST_CASE_METHOD(Fixture, "Download: %20 decoded to space") {
  ctrl_.Handle(MakeReq("GET", "/fs/download", "path=hello%20world.txt"));
  REQUIRE(mock::last_path == "hello world.txt");
}

TEST_CASE_METHOD(Fixture, "Download: path extracted when other params present") {
  ctrl_.Handle(MakeReq("GET", "/fs/download", "other=x&path=myfile.bin"));
  REQUIRE(mock::last_path == "myfile.bin");
}

TEST_CASE_METHOD(Fixture, "Download: path stops at ampersand") {
  ctrl_.Handle(MakeReq("GET", "/fs/download", "path=foo.bin&extra=bar"));
  REQUIRE(mock::last_path == "foo.bin");
}

TEST_CASE_METHOD(Fixture, "Upload: content-length is forwarded to service") {
  auto req = MakeReq("PUT", "/fs/upload", "path=foo");
  req.headers["content-length"] = "1024";
  ctrl_.Handle(req);
  REQUIRE(mock::last_len == 1024);
}

// ── NSP install routing ──────────────────────────────────────────────────────

TEST_CASE_METHOD(Fixture, "NSP Install: wrong method returns 405") {
  REQUIRE(ctrl_.Handle(MakeReq("GET", "/fs/install/nsp")).status == 405);
}

// ── NSP install service status mapping ──────────────────────────────────────

TEST_CASE_METHOD(Fixture, "NSP Install: kOk returns success JSON") {
  auto resp = ctrl_.Handle(MakeReq("PUT", "/fs/install/nsp"));
  REQUIRE(resp.status == 200);
  REQUIRE(resp.body == "{\"success\":true}");
}

TEST_CASE_METHOD(Fixture, "NSP Install: kInvalidPfs0 returns error JSON") {
  mock::nsp_status = services::NspInstallStatus::kInvalidPfs0;
  auto resp = ctrl_.Handle(MakeReq("PUT", "/fs/install/nsp"));
  REQUIRE(resp.status == 200);
  REQUIRE(resp.body == "{\"success\":false,\"error\":\"invalid_pfs0\"}");
}

TEST_CASE_METHOD(Fixture, "NSP Install: kTooManyFiles returns error JSON") {
  mock::nsp_status = services::NspInstallStatus::kTooManyFiles;
  auto resp = ctrl_.Handle(MakeReq("PUT", "/fs/install/nsp"));
  REQUIRE(resp.body == "{\"success\":false,\"error\":\"too_many_files\"}");
}

TEST_CASE_METHOD(Fixture, "NSP Install: kNcaInstallFailed returns error JSON") {
  mock::nsp_status = services::NspInstallStatus::kNcaInstallFailed;
  auto resp = ctrl_.Handle(MakeReq("PUT", "/fs/install/nsp"));
  REQUIRE(resp.body == "{\"success\":false,\"error\":\"nca_install_failed\"}");
}

TEST_CASE_METHOD(Fixture, "NSP Install: kCnmtMissing returns error JSON") {
  mock::nsp_status = services::NspInstallStatus::kCnmtMissing;
  auto resp = ctrl_.Handle(MakeReq("PUT", "/fs/install/nsp"));
  REQUIRE(resp.body == "{\"success\":false,\"error\":\"cnmt_missing\"}");
}

TEST_CASE_METHOD(Fixture, "NSP Install: kCnmtReadFailed returns error JSON") {
  mock::nsp_status = services::NspInstallStatus::kCnmtReadFailed;
  auto resp = ctrl_.Handle(MakeReq("PUT", "/fs/install/nsp"));
  REQUIRE(resp.body == "{\"success\":false,\"error\":\"cnmt_read_failed\"}");
}

TEST_CASE_METHOD(Fixture, "NSP Install: kCnmtRegisterFailed returns error JSON") {
  mock::nsp_status = services::NspInstallStatus::kCnmtRegisterFailed;
  auto resp = ctrl_.Handle(MakeReq("PUT", "/fs/install/nsp"));
  REQUIRE(resp.body == "{\"success\":false,\"error\":\"cnmt_register_failed\"}");
}

TEST_CASE_METHOD(Fixture, "NSP Install: kRecordFailed returns error JSON") {
  mock::nsp_status = services::NspInstallStatus::kRecordFailed;
  auto resp = ctrl_.Handle(MakeReq("PUT", "/fs/install/nsp"));
  REQUIRE(resp.body == "{\"success\":false,\"error\":\"record_failed\"}");
}

TEST_CASE_METHOD(Fixture, "NSP Install: kNetworkError returns error JSON") {
  mock::nsp_status = services::NspInstallStatus::kNetworkError;
  auto resp = ctrl_.Handle(MakeReq("PUT", "/fs/install/nsp"));
  REQUIRE(resp.body == "{\"success\":false,\"error\":\"network_error\"}");
}

// ── NSP install storage param ────────────────────────────────────────────────

TEST_CASE_METHOD(Fixture, "NSP Install: default storage is SdCard") {
  ctrl_.Handle(MakeReq("PUT", "/fs/install/nsp"));
  REQUIRE(mock::last_storage_id == NcmStorageId_SdCard);
}

TEST_CASE_METHOD(Fixture, "NSP Install: storage=nand sets BuiltInUser") {
  ctrl_.Handle(MakeReq("PUT", "/fs/install/nsp", "storage=nand"));
  REQUIRE(mock::last_storage_id == NcmStorageId_BuiltInUser);
}

TEST_CASE_METHOD(Fixture, "NSP Install: content-length is forwarded to service") {
  auto req = MakeReq("PUT", "/fs/install/nsp");
  req.headers["content-length"] = "2048";
  ctrl_.Handle(req);
  REQUIRE(mock::last_nsp_len == 2048);
}
