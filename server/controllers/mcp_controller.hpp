#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include "controller.hpp"
#include "mcp_service.hpp"

namespace controllers {

class McpController : public IController {
 public:
  std::string Path() const override { return "/mcp"; }
  http::Response Handle(const http::Request& req) override;

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
