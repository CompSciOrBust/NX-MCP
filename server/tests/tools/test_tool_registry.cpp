#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "../../tools/tool_registry.hpp"

class MockTool : public tools::Tool {
 public:
  MockTool(std::string name, std::string description = "A mock tool")
      : name_(std::move(name)), description_(std::move(description)) {}

  std::string Name() const override { return name_; }
  std::string Description() const override { return description_; }
  nlohmann::json InputSchema() const override { return {{"type", "object"}}; }
  tools::ToolResult Execute(const nlohmann::json&) override { return {"mock result"}; }

 private:
  std::string name_;
  std::string description_;
};

struct RegistryFixture {
  RegistryFixture() { tools::ToolRegistry::Instance().Clear(); }
};

TEST_CASE_METHOD(RegistryFixture, "Get returns nullptr for unknown tool") {
  REQUIRE(tools::ToolRegistry::Instance().Get("nonexistent") == nullptr);
}

TEST_CASE_METHOD(RegistryFixture, "Registered tool is retrievable by name") {
  tools::ToolRegistry::Instance().Register(std::make_unique<MockTool>("my_tool"));
  auto* tool = tools::ToolRegistry::Instance().Get("my_tool");
  REQUIRE(tool != nullptr);
  REQUIRE(tool->Name() == "my_tool");
}

TEST_CASE_METHOD(RegistryFixture, "All returns every registered tool") {
  tools::ToolRegistry::Instance().Register(std::make_unique<MockTool>("tool_a"));
  tools::ToolRegistry::Instance().Register(std::make_unique<MockTool>("tool_b"));
  REQUIRE(tools::ToolRegistry::Instance().All().size() == 2);
}

TEST_CASE_METHOD(RegistryFixture, "All returns empty when registry is empty") {
  REQUIRE(tools::ToolRegistry::Instance().All().empty());
}

TEST_CASE_METHOD(RegistryFixture, "Registering a duplicate name overwrites the previous tool") {
  tools::ToolRegistry::Instance().Register(std::make_unique<MockTool>("dup", "first"));
  tools::ToolRegistry::Instance().Register(std::make_unique<MockTool>("dup", "second"));
  auto* tool = tools::ToolRegistry::Instance().Get("dup");
  REQUIRE(tool != nullptr);
  REQUIRE(tool->Description() == "second");
  REQUIRE(tools::ToolRegistry::Instance().All().size() == 1);
}

TEST_CASE_METHOD(RegistryFixture, "Execute on a registered tool returns its result") {
  tools::ToolRegistry::Instance().Register(std::make_unique<MockTool>("exec_tool"));
  auto* tool = tools::ToolRegistry::Instance().Get("exec_tool");
  REQUIRE(tool != nullptr);
  auto result = tool->Execute({});
  REQUIRE(result.text == "mock result");
  REQUIRE(result.is_error == false);
}
