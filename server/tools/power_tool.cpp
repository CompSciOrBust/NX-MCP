#include "power_tool.hpp"
#include "tool_registry.hpp"
#include "tool_util.hpp"
#include <switch.h>
#include <string>

namespace tools {

static bool kRegistered =
    ToolRegistry::Instance().Register(std::make_unique<PowerTool>());

// ---- background power action -----------------------------------------------

struct PowerAction { bool reboot_; };

// Static storage so everything outlives Execute().
static PowerAction power_action;
static Thread      power_thread;
static u8          power_stack[0x2000] __attribute__((aligned(0x1000)));

static void PowerThreadFn(void* arg) {
  // Give the HTTP layer time to flush the response before the console dies.
  svcSleepThread(500'000'000LL); // 500 ms

  PowerAction* a = static_cast<PowerAction*>(arg);
  Result rc = bpcInitialize();
  if (R_SUCCEEDED(rc)) {
    if (a->reboot_)
      bpcRebootSystem();
    else
      bpcShutdownSystem();
    bpcExit(); // unreachable after power action
  }
}

// ---- PowerTool -------------------------------------------------------------

std::string PowerTool::Name() const { return "power"; }

std::string PowerTool::Description() const {
  return "Reboots or shuts down the console. "
         "Poll get_system_info or curl /ping to detect when the console comes back online after a reboot.";
}

nlohmann::json PowerTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"action", {
        {"type", "string"},
        {"enum", nlohmann::json::array({"reboot", "shutdown"})},
        {"description", "\"reboot\" or \"shutdown\""}
      }}
    }},
    {"required", {"action"}}
  };
}

ToolResult PowerTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("action") || !arguments["action"].is_string())
    return {"action required (\"reboot\" or \"shutdown\")", true};

  const std::string action = arguments["action"].get<std::string>();
  if (action != "reboot" && action != "shutdown")
    return {"action must be \"reboot\" or \"shutdown\"", true};

  // Probe bpc early so we can return an error before spawning the thread.
  Result rc = bpcInitialize();
  if (R_FAILED(rc))
    return {"bpcInitialize failed: " + FmtRc(rc), true};
  bpcExit();

  power_action.reboot_ = (action == "reboot");

  threadCreate(&power_thread, PowerThreadFn, &power_action,
               power_stack, sizeof(power_stack), 0x2C, -2);
  threadStart(&power_thread);

  if (power_action.reboot_)
    return {"Rebooting — connection will drop. Poll get_system_info or curl /ping to confirm restart.", false};
  else
    return {"Shutting down — connection will drop.", false};
}

}  // namespace tools
