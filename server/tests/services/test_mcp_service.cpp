#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "../../services/mcp_service.hpp"
#include "../../mcp_versions.hpp"
#include "../../tools/tool_registry.hpp"
#include "../../tools/tool.hpp"
#include <stdexcept>

// ── test doubles ─────────────────────────────────────────────────────────────

class MockTool : public tools::Tool {
 public:
  MockTool(std::string name, std::string desc = "a tool",
           nlohmann::json schema = {{"type", "object"}})
      : name_(std::move(name)), desc_(std::move(desc)), schema_(std::move(schema)) {}

  std::string Name() const override { return name_; }
  std::string Description() const override { return desc_; }
  nlohmann::json InputSchema() const override { return schema_; }
  tools::ToolResult Execute(const nlohmann::json& args) override {
    last_args_ = args;
    return result_;
  }

  tools::ToolResult result_{"default result"};
  nlohmann::json    last_args_;

 private:
  std::string    name_;
  std::string    desc_;
  nlohmann::json schema_;
};

class ThrowingTool : public tools::Tool {
 public:
  std::string Name() const override { return "throw_tool"; }
  std::string Description() const override { return "throws"; }
  nlohmann::json InputSchema() const override { return {{"type", "object"}}; }
  tools::ToolResult Execute(const nlohmann::json&) override {
    throw std::runtime_error("something went wrong");
  }
};

class UnknownThrowTool : public tools::Tool {
 public:
  std::string Name() const override { return "unknown_throw"; }
  std::string Description() const override { return "throws unknown"; }
  nlohmann::json InputSchema() const override { return {{"type", "object"}}; }
  tools::ToolResult Execute(const nlohmann::json&) override { throw 42; }
};

// ── fixture ───────────────────────────────────────────────────────────────────

struct Fixture {
  Fixture() { tools::ToolRegistry::Instance().Clear(); }
  services::McpService svc_;
};

// ── Dispatch routing ──────────────────────────────────────────────────────────

TEST_CASE_METHOD(Fixture, "Dispatch: unknown method returns -32601") {
  auto j = svc_.Dispatch("no/such/method", {}, 1);
  REQUIRE(j["error"]["code"] == -32601);
}

TEST_CASE_METHOD(Fixture, "Dispatch: ping returns empty result object") {
  auto j = svc_.Dispatch("ping", {}, 1);
  REQUIRE(j.contains("result"));
  REQUIRE(j["result"].is_object());
  REQUIRE(j["result"].empty());
}

TEST_CASE_METHOD(Fixture, "Dispatch: id is echoed back in response") {
  auto j = svc_.Dispatch("ping", {}, "req-42");
  REQUIRE(j["id"] == "req-42");
  REQUIRE(j["jsonrpc"] == "2.0");
}

// ── HandleInitialize ──────────────────────────────────────────────────────────

TEST_CASE_METHOD(Fixture, "Initialize: wrong protocol version returns -32022 with version data") {
  auto j = svc_.Dispatch("initialize", {{"protocolVersion", "1999-01-01"}}, 1);
  REQUIRE(j["error"]["code"] == -32022);
  REQUIRE(j["error"]["data"]["requested"] == "1999-01-01");
  REQUIRE(j["error"]["data"]["supported"][0] == mcp::kLegacyVersion);
}

TEST_CASE_METHOD(Fixture, "Initialize: correct version returns serverInfo and capabilities") {
  auto j = svc_.Dispatch("initialize", {{"protocolVersion", mcp::kLegacyVersion}}, 1);
  REQUIRE(j.contains("result"));
  REQUIRE(j["result"]["protocolVersion"] == mcp::kLegacyVersion);
  REQUIRE(j["result"]["serverInfo"]["name"] == mcp::kServerName);
  REQUIRE(j["result"]["capabilities"].contains("tools"));
  REQUIRE(j["result"].contains("instructions"));
}

// ── HandleDiscover ────────────────────────────────────────────────────────────

TEST_CASE_METHOD(Fixture, "Discover: returns complete result with supported version") {
  auto j = svc_.Dispatch("server/discover", {}, 1);
  REQUIRE(j["result"]["resultType"] == "complete");
  REQUIRE(j["result"]["supportedVersions"][0] == mcp::kVersion);
  REQUIRE(j["result"]["capabilities"].contains("tools"));
  REQUIRE(j["result"]["_meta"]["io.modelcontextprotocol/serverInfo"]["name"] == mcp::kServerName);
}

// ── HandleToolsList ───────────────────────────────────────────────────────────

TEST_CASE_METHOD(Fixture, "ToolsList: empty registry returns empty tools array") {
  auto j = svc_.Dispatch("tools/list", {}, 1);
  REQUIRE(j["result"]["tools"].is_array());
  REQUIRE(j["result"]["tools"].empty());
}

TEST_CASE_METHOD(Fixture, "ToolsList: registered tools appear with name, description, inputSchema") {
  nlohmann::json schema = {{"type", "object"}, {"properties", {{"path", {{"type", "string"}}}}}};
  tools::ToolRegistry::Instance().Register(
      std::make_unique<MockTool>("my_tool", "does something", schema));

  auto j = svc_.Dispatch("tools/list", {}, 1);
  auto& tools = j["result"]["tools"];
  REQUIRE(tools.size() == 1);
  REQUIRE(tools[0]["name"] == "my_tool");
  REQUIRE(tools[0]["description"] == "does something");
  REQUIRE(tools[0]["inputSchema"] == schema);
}

TEST_CASE_METHOD(Fixture, "ToolsList: multiple registered tools all appear") {
  tools::ToolRegistry::Instance().Register(std::make_unique<MockTool>("tool_a"));
  tools::ToolRegistry::Instance().Register(std::make_unique<MockTool>("tool_b"));
  auto j = svc_.Dispatch("tools/list", {}, 1);
  REQUIRE(j["result"]["tools"].size() == 2);
}

// ── HandleToolsCall ───────────────────────────────────────────────────────────

TEST_CASE_METHOD(Fixture, "ToolsCall: missing name param returns -32602") {
  auto j = svc_.Dispatch("tools/call", {}, 1);
  REQUIRE(j["error"]["code"] == -32602);
}

TEST_CASE_METHOD(Fixture, "ToolsCall: unknown tool name returns -32602") {
  auto j = svc_.Dispatch("tools/call", {{"name", "no_such_tool"}}, 1);
  REQUIRE(j["error"]["code"] == -32602);
  REQUIRE(j["error"]["message"] == "Unknown tool: no_such_tool");
}

TEST_CASE_METHOD(Fixture, "ToolsCall: successful call returns text content") {
  auto owned = std::make_unique<MockTool>("my_tool");
  owned->result_ = {"hello world"};
  tools::ToolRegistry::Instance().Register(std::move(owned));

  auto j = svc_.Dispatch("tools/call", {{"name", "my_tool"}}, 1);
  REQUIRE(j["result"]["isError"] == false);
  REQUIRE(j["result"]["content"][0]["type"] == "text");
  REQUIRE(j["result"]["content"][0]["text"] == "hello world");
}

TEST_CASE_METHOD(Fixture, "ToolsCall: error result sets isError true") {
  auto owned = std::make_unique<MockTool>("err_tool");
  owned->result_ = {"something failed", true};
  tools::ToolRegistry::Instance().Register(std::move(owned));

  auto j = svc_.Dispatch("tools/call", {{"name", "err_tool"}}, 1);
  REQUIRE(j["result"]["isError"] == true);
  REQUIRE(j["result"]["content"][0]["text"] == "something failed");
}

TEST_CASE_METHOD(Fixture, "ToolsCall: image result returns image content item") {
  auto owned = std::make_unique<MockTool>("img_tool");
  owned->result_ = {"", false, "base64data==", "image/png"};
  tools::ToolRegistry::Instance().Register(std::move(owned));

  auto j = svc_.Dispatch("tools/call", {{"name", "img_tool"}}, 1);
  REQUIRE(j["result"]["content"][0]["type"] == "image");
  REQUIRE(j["result"]["content"][0]["data"] == "base64data==");
  REQUIRE(j["result"]["content"][0]["mimeType"] == "image/png");
}

TEST_CASE_METHOD(Fixture, "ToolsCall: arguments are forwarded to the tool") {
  auto owned = std::make_unique<MockTool>("arg_tool");
  MockTool* tool = owned.get();
  tools::ToolRegistry::Instance().Register(std::move(owned));

  svc_.Dispatch("tools/call", {{"name", "arg_tool"}, {"arguments", {{"key", "val"}}}}, 1);
  REQUIRE(tool->last_args_["key"] == "val");
}

TEST_CASE_METHOD(Fixture, "ToolsCall: std::exception from tool is caught and returned as error") {
  tools::ToolRegistry::Instance().Register(std::make_unique<ThrowingTool>());

  auto j = svc_.Dispatch("tools/call", {{"name", "throw_tool"}}, 1);
  REQUIRE(j["result"]["isError"] == true);
  REQUIRE(j["result"]["content"][0]["text"] == "Tool threw exception: something went wrong");
}

TEST_CASE_METHOD(Fixture, "ToolsCall: unknown exception from tool is caught and returned as error") {
  tools::ToolRegistry::Instance().Register(std::make_unique<UnknownThrowTool>());

  auto j = svc_.Dispatch("tools/call", {{"name", "unknown_throw"}}, 1);
  REQUIRE(j["result"]["isError"] == true);
  REQUIRE(j["result"]["content"][0]["text"] == "Tool threw unknown exception");
}
