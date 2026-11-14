#include "power_tool.hpp"
#include "tool_registry.hpp"
#include "tool_util.hpp"
#include <switch.h>
#include <string>

namespace tools {

static bool kRegistered =
    ToolRegistry::Instance().Register(std::make_unique<PowerTool>());

// ---- background power action -----------------------------------------------

struct PowerAction { bool reboot_; bool hard_; };

// Static storage so everything outlives Execute().
static PowerAction power_action;
static Thread      power_thread;
static u8          power_stack[0x2000] __attribute__((aligned(0x1000)));

static void PowerThreadFn(void* arg) {
  // Give the HTTP layer time to flush the response before the console dies.
  svcSleepThread(500'000'000LL); // 500 ms

  PowerAction* a = static_cast<PowerAction*>(arg);
  if (a->hard_) {
    // Immediate reset through the board power controller; system modules get
    // no shutdown notification and fs does not flush.
    if (R_SUCCEEDED(bpcInitialize())) {
      if (a->reboot_)
        bpcRebootSystem();
      else
        bpcShutdownSystem();
      bpcExit(); // unreachable after power action
    }
  } else {
    // The system's regular shutdown sequence: every PSC module is notified
    // and can finish up before spsm powers off or resets the console.
    if (R_SUCCEEDED(spsmInitialize())) {
      spsmShutdown(a->reboot_);
      spsmExit(); // unreachable after power action
    }
  }
}

// ---- PowerTool -------------------------------------------------------------

std::string PowerTool::Name() const { return "power"; }

std::string PowerTool::Description() const {
  return "Reboots or shuts down the console through the system's regular shutdown sequence. "
         "Set hard=true only if that hangs: it resets immediately, without letting system modules "
         "finish or flush (like holding the power button). "
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
      }},
      {"hard", {
        {"type", "boolean"},
        {"description", "Reset immediately instead of running the shutdown sequence (default false)"}
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

  const bool hard = arguments.value("hard", false);

  // Probe the service early so we can return an error before spawning the thread.
  Result rc = hard ? bpcInitialize() : spsmInitialize();
  if (R_FAILED(rc))
    return {(hard ? "bpcInitialize failed: " : "spsmInitialize failed: ") + FmtRc(rc), true};
  if (hard)
    bpcExit();
  else
    spsmExit();

  power_action.reboot_ = (action == "reboot");
  power_action.hard_   = hard;

  threadCreate(&power_thread, PowerThreadFn, &power_action,
               power_stack, sizeof(power_stack), 0x2C, -2);
  threadStart(&power_thread);

  if (power_action.reboot_)
    return {"Rebooting — connection will drop. Poll get_system_info or curl /ping to confirm restart.", false};
  else
    return {"Shutting down — connection will drop.", false};
}

}  // namespace tools
