#pragma once
#include "tool.hpp"

namespace tools {

class FsListDirTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class FsReadFileTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class FsWriteFileTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class FsDeleteTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class FsMkdirTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class FsCrc32Tool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

}  // namespace tools
