#include "../../services/mcp_service.hpp"

namespace mock {

std::string    g_last_method;
nlohmann::json g_last_params;
nlohmann::json g_last_id;
nlohmann::json g_dispatch_result = {
    {"jsonrpc", "2.0"}, {"id", 1}, {"result", nlohmann::json::object()}};

void Reset() {
  g_last_method.clear();
  g_last_params = {};
  g_last_id     = {};
  g_dispatch_result = {
      {"jsonrpc", "2.0"}, {"id", 1}, {"result", nlohmann::json::object()}};
}

}  // namespace mock

namespace services {

nlohmann::json McpService::Dispatch(const std::string& method,
                                    const nlohmann::json& params,
                                    const nlohmann::json& id) {
  mock::g_last_method = method;
  mock::g_last_params = params;
  mock::g_last_id     = id;
  return mock::g_dispatch_result;
}

// private stubs — never called through the mock
nlohmann::json McpService::HandleInitialize(const nlohmann::json&, const nlohmann::json&) { return {}; }
nlohmann::json McpService::HandleDiscover(const nlohmann::json&) { return {}; }
nlohmann::json McpService::HandleToolsList(const nlohmann::json&) { return {}; }
nlohmann::json McpService::HandleToolsCall(const nlohmann::json&, const nlohmann::json&) { return {}; }

}  // namespace services
