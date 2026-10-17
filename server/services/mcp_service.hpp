#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace services {

class McpService {
 public:
  nlohmann::json Dispatch(const std::string& method,
                          const nlohmann::json& params,
                          const nlohmann::json& id);

 private:
  nlohmann::json HandleInitialize(const nlohmann::json& params, const nlohmann::json& id);
  nlohmann::json HandleDiscover(const nlohmann::json& id);
  nlohmann::json HandleToolsList(const nlohmann::json& id);
  nlohmann::json HandleToolsCall(const nlohmann::json& params, const nlohmann::json& id);
};

}  // namespace services
