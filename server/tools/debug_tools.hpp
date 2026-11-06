#pragma once
#include "tool.hpp"

namespace tools {

class DebugAttachTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugPauseTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugResumeTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugReadMemoryTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugWriteMemoryTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugQueryMemoryTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugDetachTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugGetThreadsTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugGetRegistersTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugSetRegistersTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugGetEventsTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugCopyMemoryTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugDumpCodeTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugBacktraceTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugFillMemoryTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugDumpRegionTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugValueScanTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

class DebugScanClearTool : public Tool {
 public:
  std::string Name() const override;
  std::string Description() const override;
  nlohmann::json InputSchema() const override;
  ToolResult Execute(const nlohmann::json& arguments) override;
};

}  // namespace tools
