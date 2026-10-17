#pragma once
#include "tool.hpp"

namespace tools {

class HidSequenceTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class HidPressButtonsTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class HidTouchTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class HidSwipeTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

}  // namespace tools
