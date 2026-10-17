#pragma once
#include "tool.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tools {

class ToolRegistry {
 public:
  static ToolRegistry& Instance();

  bool Register(std::unique_ptr<Tool> tool);
  Tool* Get(const std::string& name) const;
  std::vector<const Tool*> All() const;
  void Clear();

 private:
  ToolRegistry() = default;
  std::unordered_map<std::string, std::unique_ptr<Tool>> tools_;
};

}  // namespace tools
