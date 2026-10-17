#pragma once
#include "types.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include "mcp_service.hpp"

namespace controllers {

class McpController {
 public:
  http::Response Handle(const http::Request& req);

 private:
  services::McpService mcp_service_;

  http::Response HandlePost(const http::Request& req);
  http::Response HandleModernRequest(const nlohmann::json& rpc);
  http::Response HandleLegacyRequest(const nlohmann::json& rpc);

  static nlohmann::json BuildError(const nlohmann::json& id, int code,
                                   const std::string& message,
                                   std::optional<nlohmann::json> data = std::nullopt);
};

}  // namespace controllers
