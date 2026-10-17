#include "tool_registry.hpp"

namespace tools {

ToolRegistry& ToolRegistry::Instance() {
  static ToolRegistry instance;
  return instance;
}

bool ToolRegistry::Register(std::unique_ptr<Tool> tool) {
  std::string name = tool->Name();
  tools_[std::move(name)] = std::move(tool);
  return true;
}

Tool* ToolRegistry::Get(const std::string& name) const {
  auto it = tools_.find(name);
  return it != tools_.end() ? it->second.get() : nullptr;
}

std::vector<const Tool*> ToolRegistry::All() const {
  std::vector<const Tool*> result;
  result.reserve(tools_.size());
  for (const auto& [name, tool] : tools_) {
    result.push_back(tool.get());
  }
  return result;
}

void ToolRegistry::Clear() {
  tools_.clear();
}

}  // namespace tools
