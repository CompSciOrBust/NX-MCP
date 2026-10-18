#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "../../controllers/mcp_controller.hpp"
#include "../../mcp_versions.hpp"

namespace mock {
extern std::string    g_last_method;
extern nlohmann::json g_last_params;
extern nlohmann::json g_last_id;
extern nlohmann::json g_dispatch_result;
void Reset();
}

static http::Request PostReq(const nlohmann::json& body) {
  http::Request r;
  r.method = "POST";
  r.path   = "/mcp";
  r.body   = body.dump();
  r.fd     = -1;
  return r;
}

static nlohmann::json Body(const http::Response& resp) {
  return nlohmann::json::parse(resp.body);
}

// Valid modern RPC with all required _meta fields
static nlohmann::json ModernRpc(const std::string& method, nlohmann::json id = 1) {
  return {
    {"jsonrpc", "2.0"},
    {"method", method},
    {"id", std::move(id)},
    {"params", {
      {"_meta", {
        {"io.modelcontextprotocol/protocolVersion", mcp::kVersion},
        {"io.modelcontextprotocol/clientCapabilities", nlohmann::json::object()}
      }}
    }}
  };
}

// Valid legacy RPC (no _meta)
static nlohmann::json LegacyRpc(const std::string& method, nlohmann::json id = 1) {
  return {
    {"jsonrpc", "2.0"},
    {"method", method},
    {"id", std::move(id)},
    {"params", nlohmann::json::object()}
  };
}

struct Fixture {
  Fixture() { mock::Reset(); }
  controllers::McpController ctrl_;
};

// ── Handle routing ────────────────────────────────────────────────────────────

TEST_CASE_METHOD(Fixture, "Handle: wrong path returns 404") {
  http::Request r; r.method = "POST"; r.path = "/mcp/extra"; r.fd = -1;
  REQUIRE(ctrl_.Handle(r).status == 404);
}

TEST_CASE_METHOD(Fixture, "Handle: OPTIONS returns 204") {
  http::Request r; r.method = "OPTIONS"; r.path = "/mcp"; r.fd = -1;
  REQUIRE(ctrl_.Handle(r).status == 204);
}

TEST_CASE_METHOD(Fixture, "Handle: GET returns 405") {
  http::Request r; r.method = "GET"; r.path = "/mcp"; r.fd = -1;
  REQUIRE(ctrl_.Handle(r).status == 405);
}

TEST_CASE_METHOD(Fixture, "Handle: Path() returns /mcp") {
  REQUIRE(ctrl_.Path() == "/mcp");
}

// ── HandlePost: JSON-RPC envelope validation ──────────────────────────────────

TEST_CASE_METHOD(Fixture, "Post: invalid JSON returns parse error -32700") {
  http::Request r; r.method = "POST"; r.path = "/mcp"; r.body = "not json"; r.fd = -1;
  REQUIRE(Body(ctrl_.Handle(r))["error"]["code"] == -32700);
}

TEST_CASE_METHOD(Fixture, "Post: JSON array returns parse error -32700") {
  REQUIRE(Body(ctrl_.Handle(PostReq(nlohmann::json::array({1, 2}))))["error"]["code"] == -32700);
}

TEST_CASE_METHOD(Fixture, "Post: missing jsonrpc field returns invalid request -32600") {
  REQUIRE(Body(ctrl_.Handle(PostReq({{"method", "ping"}, {"id", 1}})))["error"]["code"] == -32600);
}

TEST_CASE_METHOD(Fixture, "Post: wrong jsonrpc version returns invalid request -32600") {
  REQUIRE(Body(ctrl_.Handle(PostReq({{"jsonrpc", "1.0"}, {"method", "ping"}, {"id", 1}})))["error"]["code"] == -32600);
}

TEST_CASE_METHOD(Fixture, "Post: missing method returns invalid request -32600") {
  REQUIRE(Body(ctrl_.Handle(PostReq({{"jsonrpc", "2.0"}, {"id", 1}})))["error"]["code"] == -32600);
}

TEST_CASE_METHOD(Fixture, "Post: non-string method returns invalid request -32600") {
  REQUIRE(Body(ctrl_.Handle(PostReq({{"jsonrpc", "2.0"}, {"method", 42}, {"id", 1}})))["error"]["code"] == -32600);
}

TEST_CASE_METHOD(Fixture, "Post: notification (no id) returns 204") {
  REQUIRE(ctrl_.Handle(PostReq({{"jsonrpc", "2.0"}, {"method", "ping"}})).status == 204);
}

// ── HandleModernRequest validation ────────────────────────────────────────────

TEST_CASE_METHOD(Fixture, "Modern: null id returns invalid request -32600") {
  auto rpc = ModernRpc("ping"); rpc["id"] = nullptr;
  REQUIRE(Body(ctrl_.Handle(PostReq(rpc)))["error"]["code"] == -32600);
}

TEST_CASE_METHOD(Fixture, "Modern: non-string protocolVersion returns invalid params -32602") {
  // protocolVersion must be present (triggers modern path) but is not a string
  auto rpc = ModernRpc("ping");
  rpc["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"] = 20260728;
  REQUIRE(Body(ctrl_.Handle(PostReq(rpc)))["error"]["code"] == -32602);
}

TEST_CASE_METHOD(Fixture, "Modern: missing clientCapabilities returns invalid params -32602") {
  auto rpc = ModernRpc("ping");
  rpc["params"]["_meta"].erase("io.modelcontextprotocol/clientCapabilities");
  REQUIRE(Body(ctrl_.Handle(PostReq(rpc)))["error"]["code"] == -32602);
}

TEST_CASE_METHOD(Fixture, "Modern: unsupported protocol version returns -32022 with version data") {
  auto rpc = ModernRpc("ping");
  rpc["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"] = "1999-01-01";
  auto j = Body(ctrl_.Handle(PostReq(rpc)));
  REQUIRE(j["error"]["code"] == -32022);
  REQUIRE(j["error"]["data"]["requested"] == "1999-01-01");
  REQUIRE(j["error"]["data"]["supported"][0] == mcp::kVersion);
}

TEST_CASE_METHOD(Fixture, "Modern: valid request dispatches to service with correct method and id") {
  ctrl_.Handle(PostReq(ModernRpc("tools/list", 42)));
  REQUIRE(mock::g_last_method == "tools/list");
  REQUIRE(mock::g_last_id == 42);
}

TEST_CASE_METHOD(Fixture, "Modern: service result is returned as response body") {
  mock::g_dispatch_result = {{"jsonrpc", "2.0"}, {"id", 1}, {"result", {{"foo", "bar"}}}};
  auto resp = ctrl_.Handle(PostReq(ModernRpc("tools/list")));
  REQUIRE(resp.status == 200);
  REQUIRE(Body(resp)["result"]["foo"] == "bar");
}

// ── HandleLegacyRequest validation ────────────────────────────────────────────

TEST_CASE_METHOD(Fixture, "Legacy: null id returns invalid request -32600") {
  auto rpc = LegacyRpc("initialize"); rpc["id"] = nullptr;
  REQUIRE(Body(ctrl_.Handle(PostReq(rpc)))["error"]["code"] == -32600);
}

TEST_CASE_METHOD(Fixture, "Legacy: array params returns invalid params -32602") {
  auto rpc = LegacyRpc("ping"); rpc["params"] = nlohmann::json::array({1, 2});
  REQUIRE(Body(ctrl_.Handle(PostReq(rpc)))["error"]["code"] == -32602);
}

TEST_CASE_METHOD(Fixture, "Legacy: valid request dispatches to service with correct method and id") {
  ctrl_.Handle(PostReq(LegacyRpc("initialize", "req-1")));
  REQUIRE(mock::g_last_method == "initialize");
  REQUIRE(mock::g_last_id == "req-1");
}

TEST_CASE_METHOD(Fixture, "Legacy: service result is returned as response body") {
  mock::g_dispatch_result = {{"jsonrpc", "2.0"}, {"id", 1}, {"result", {{"version", "ok"}}}};
  auto resp = ctrl_.Handle(PostReq(LegacyRpc("initialize")));
  REQUIRE(resp.status == 200);
  REQUIRE(Body(resp)["result"]["version"] == "ok");
}
