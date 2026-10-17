#include "mcp_controller.hpp"
#include "mcp_versions.hpp"

namespace controllers {

http::Response McpController::Handle(const http::Request& req) {
  if (req.path != "/mcp")    return http::Response::NotFound();
  if (req.method == "OPTIONS") return http::Response::NoContent();
  if (req.method != "POST")  return http::Response::MethodNotAllowed();
  return HandlePost(req);
}

http::Response McpController::HandlePost(const http::Request& req) {
  nlohmann::json rpc = nlohmann::json::parse(req.body, nullptr, false);
  if (rpc.is_discarded() || !rpc.is_object())
    return http::Response::Ok(BuildError(nullptr, -32700, "Parse error").dump());

  if (!rpc.contains("jsonrpc") || rpc["jsonrpc"] != "2.0" ||
      !rpc.contains("method") || !rpc["method"].is_string())
    return http::Response::Ok(BuildError(nullptr, -32600, "Invalid Request").dump());

  if (!rpc.contains("id"))
    return http::Response::NoContent();

  const nlohmann::json params = rpc.value("params", nlohmann::json::object());
  const bool is_modern = params.contains("_meta") &&
      params["_meta"].contains("io.modelcontextprotocol/protocolVersion");

  return is_modern ? HandleModernRequest(rpc) : HandleLegacyRequest(rpc);
}

http::Response McpController::HandleModernRequest(const nlohmann::json& rpc) {
  const nlohmann::json id = rpc["id"];
  if (id.is_null() || (!id.is_string() && !id.is_number_integer()))
    return http::Response::Ok(BuildError(nullptr, -32600, "Invalid Request").dump());

  const nlohmann::json& params = rpc.value("params", nlohmann::json::object());
  if (!params.is_object())
    return http::Response::Ok(BuildError(id, -32602,
        "Invalid params: params must be an object").dump());

  const nlohmann::json& meta = params["_meta"];

  if (!meta.contains("io.modelcontextprotocol/protocolVersion") ||
      !meta["io.modelcontextprotocol/protocolVersion"].is_string())
    return http::Response::Ok(BuildError(id, -32602,
        "Invalid params: missing protocolVersion in _meta").dump());

  if (!meta.contains("io.modelcontextprotocol/clientCapabilities"))
    return http::Response::Ok(BuildError(id, -32602,
        "Invalid params: missing clientCapabilities in _meta").dump());

  const std::string requested =
      meta["io.modelcontextprotocol/protocolVersion"].get<std::string>();
  if (requested != mcp::kVersion)
    return http::Response::Ok(BuildError(id, -32022, "Unsupported protocol version",
        nlohmann::json{{"supported", nlohmann::json::array({mcp::kVersion})},
                       {"requested", requested}}).dump());

  return http::Response::Ok(
      mcp_service_.Dispatch(rpc["method"].get<std::string>(), params, id).dump());
}

http::Response McpController::HandleLegacyRequest(const nlohmann::json& rpc) {
  const nlohmann::json id = rpc["id"];
  if (id.is_null() || (!id.is_string() && !id.is_number_integer()))
    return http::Response::Ok(BuildError(nullptr, -32600, "Invalid Request").dump());

  const nlohmann::json params = rpc.value("params", nlohmann::json::object());
  if (!params.is_object())
    return http::Response::Ok(BuildError(id, -32602,
        "Invalid params: params must be an object").dump());

  return http::Response::Ok(
      mcp_service_.Dispatch(rpc["method"].get<std::string>(), params, id).dump());
}

nlohmann::json McpController::BuildError(const nlohmann::json& id, int code,
                                         const std::string& message,
                                         std::optional<nlohmann::json> data) {
  nlohmann::json err = {{"code", code}, {"message", message}};
  if (data.has_value()) err["data"] = *data;
  return {{"jsonrpc", "2.0"}, {"id", id}, {"error", err}};
}

}  // namespace controllers
