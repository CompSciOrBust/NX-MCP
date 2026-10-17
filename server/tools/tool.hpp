#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace tools {

struct ToolResult {
  std::string text;
  bool is_error = false;
  std::string image_data;  // non-empty → return image content instead of text
  std::string image_mime;
};

class Tool {
 public:
  virtual ~Tool() = default;
  virtual std::string Name() const = 0;
  virtual std::string Description() const = 0;
  virtual nlohmann::json InputSchema() const = 0;
  virtual ToolResult Execute(const nlohmann::json& arguments) = 0;
};

}  // namespace tools
