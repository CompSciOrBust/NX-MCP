#pragma once
#include "tool.hpp"

namespace tools {

class NspInstallTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

}  // namespace tools
