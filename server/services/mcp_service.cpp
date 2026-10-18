#include "mcp_service.hpp"
#include "mcp_versions.hpp"
#include "tool_registry.hpp"

namespace services {

nlohmann::json McpService::Dispatch(const std::string& method,
                                    const nlohmann::json& params,
                                    const nlohmann::json& id) {
  if (method == "initialize") return HandleInitialize(params, id);
  if (method == "ping") return {{"jsonrpc", "2.0"}, {"id", id}, {"result", nlohmann::json::object()}};
  if (method == "server/discover") return HandleDiscover(id);
  if (method == "tools/list") return HandleToolsList(id);
  if (method == "tools/call") return HandleToolsCall(params, id);

  return {
    {"jsonrpc", "2.0"},
    {"id", id},
    {"error", {{"code", -32601}, {"message", "Method not found"}}}
  };
}

nlohmann::json McpService::HandleInitialize(const nlohmann::json& params,
                                             const nlohmann::json& id) {
  const std::string requested =
      params.value("protocolVersion", std::string(""));
  if (requested != mcp::kLegacyVersion) {
    return {
      {"jsonrpc", "2.0"},
      {"id", id},
      {"error", {
        {"code", -32022},
        {"message", "Unsupported protocol version"},
        {"data", {
          {"supported", nlohmann::json::array({mcp::kLegacyVersion})},
          {"requested", requested}
        }}
      }}
    };
  }
  return {
    {"jsonrpc", "2.0"},
    {"id", id},
    {"result", {
      {"protocolVersion", mcp::kLegacyVersion},
      {"capabilities", {{"tools", nlohmann::json::object()}}},
      {"serverInfo", {{"name", mcp::kServerName}, {"version", mcp::kServerVersion}}}
    }}
  };
}

nlohmann::json McpService::HandleDiscover(const nlohmann::json& id) {
  return {
    {"jsonrpc", "2.0"},
    {"id", id},
    {"result", {
      {"resultType", "complete"},
      {"supportedVersions", {mcp::kVersion}},
      {"capabilities", {{"tools", nlohmann::json::object()}}},
      {"_meta", {
        {"io.modelcontextprotocol/serverInfo", {
          {"name", mcp::kServerName},
          {"version", mcp::kServerVersion}
        }}
      }}
    }}
  };
}

nlohmann::json McpService::HandleToolsList(const nlohmann::json& id) {
  nlohmann::json tool_list = nlohmann::json::array();
  for (const auto* tool : tools::ToolRegistry::Instance().All()) {
    tool_list.push_back({
      {"name", tool->Name()},
      {"description", tool->Description()},
      {"inputSchema", tool->InputSchema()}
    });
  }
  return {
    {"jsonrpc", "2.0"},
    {"id", id},
    {"result", {{"resultType", "complete"}, {"tools", tool_list}}}
  };
}

nlohmann::json McpService::HandleToolsCall(const nlohmann::json& params,
                                            const nlohmann::json& id) {
  if (!params.contains("name") || !params["name"].is_string()) {
    return {
      {"jsonrpc", "2.0"},
      {"id", id},
      {"error", {{"code", -32602}, {"message", "Invalid params: missing tool name"}}}
    };
  }

  const std::string name = params["name"].get<std::string>();
  auto* tool = tools::ToolRegistry::Instance().Get(name);
  if (!tool) {
    return {
      {"jsonrpc", "2.0"},
      {"id", id},
      {"error", {{"code", -32602}, {"message", "Unknown tool: " + name}}}
    };
  }

  const nlohmann::json arguments = params.value("arguments", nlohmann::json::object());

  tools::ToolResult result;
  try {
    result = tool->Execute(arguments);
  } catch (const std::exception& e) {
    result = {std::string("Tool threw exception: ") + e.what(), true};
  } catch (...) {
    result = {"Tool threw unknown exception", true};
  }

  nlohmann::json content_item;
  if (!result.image_data.empty()) {
    content_item = {{"type", "image"}, {"data", result.image_data}, {"mimeType", result.image_mime}};
  } else {
    content_item = {{"type", "text"}, {"text", result.text}};
  }

  return {
    {"jsonrpc", "2.0"},
    {"id", id},
    {"result", {
      {"resultType", "complete"},
      {"isError", result.is_error},
      {"content", nlohmann::json::array({content_item})}
    }}
  };
}

}  // namespace services
