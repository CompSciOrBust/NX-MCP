#include "hid_input_tools.hpp"
#include "tool_registry.hpp"
#include <switch.h>
#include <switch/services/hiddbg.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>

namespace tools {

static bool kSeqRegistered =
    ToolRegistry::Instance().Register(std::make_unique<HidSequenceTool>());
static bool kPressRegistered =
    ToolRegistry::Instance().Register(std::make_unique<HidPressButtonsTool>());
static bool kTouchRegistered =
    ToolRegistry::Instance().Register(std::make_unique<HidTouchTool>());
static bool kSwipeRegistered =
    ToolRegistry::Instance().Register(std::make_unique<HidSwipeTool>());

// ---- shared helpers --------------------------------------------------------

static std::string FmtRc(Result rc) {
  char buf[12];
  snprintf(buf, sizeof(buf), "0x%08X", (unsigned)rc);
  return buf;
}

static u64 ParseButtons(const nlohmann::json& arr) {
  struct Entry { const char* name; u64 bit; };
  static const Entry kMap[] = {
    {"A",       HidNpadButton_A},
    {"B",       HidNpadButton_B},
    {"X",       HidNpadButton_X},
    {"Y",       HidNpadButton_Y},
    {"L",       HidNpadButton_L},
    {"R",       HidNpadButton_R},
    {"ZL",      HidNpadButton_ZL},
    {"ZR",      HidNpadButton_ZR},
    {"Plus",    HidNpadButton_Plus},
    {"Minus",   HidNpadButton_Minus},
    {"Up",      HidNpadButton_Up},
    {"Down",    HidNpadButton_Down},
    {"Left",    HidNpadButton_Left},
    {"Right",   HidNpadButton_Right},
    {"StickL",  HidNpadButton_StickL},
    {"StickR",  HidNpadButton_StickR},
    {"Home",    HiddbgNpadButton_Home},
    {"Capture", HiddbgNpadButton_Capture},
  };
  u64 mask = 0;
  if (!arr.is_array()) return mask;
  for (const auto& b : arr) {
    if (!b.is_string()) continue;
    const std::string s = b.get<std::string>();
    for (const auto& e : kMap)
      if (s == e.name) { mask |= e.bit; break; }
  }
  return mask;
}

static s32 StickVal(const nlohmann::json& args, const char* stick, const char* axis) {
  if (!args.contains(stick)) return 0;
  const nlohmann::json& s = args[stick];
  if (!s.is_object() || !s.contains(axis) || !s[axis].is_number()) return 0;
  double v = s[axis].get<double>();
  if (v >  1.0) v =  1.0;
  if (v < -1.0) v = -1.0;
  return (s32)(v * 0x7FFF);
}

// Page-aligned work buffer required by hiddbgAttachHdlsWorkBuffer.
alignas(0x1000) static u8 hdls_buf[0x1000];

struct HdlsCtx {
  HiddbgHdlsSessionId session;
  HiddbgHdlsHandle    handle;
};

static Result HdlsOpen(HdlsCtx& ctx) {
  Result rc = hiddbgInitialize();
  if (R_FAILED(rc)) return rc;

  rc = hiddbgAttachHdlsWorkBuffer(&ctx.session, hdls_buf, sizeof(hdls_buf));
  if (R_FAILED(rc)) { hiddbgExit(); return rc; }

  HiddbgHdlsDeviceInfo info = {};
  info.deviceType        = HidDeviceType_FullKey3;
  info.npadInterfaceType = HidNpadInterfaceType_USB;

  rc = hiddbgAttachHdlsVirtualDevice(&ctx.handle, &info);
  if (R_FAILED(rc)) {
    hiddbgReleaseHdlsWorkBuffer(ctx.session);
    hiddbgExit();
    return rc;
  }
  return 0;
}

static void HdlsClose(HdlsCtx& ctx) {
  hiddbgDetachHdlsVirtualDevice(ctx.handle);
  hiddbgReleaseHdlsWorkBuffer(ctx.session);
  hiddbgExit();
}

static void HidApplyStep(HdlsCtx& ctx, const nlohmann::json& step) {
  u64 buttons = 0;
  if (step.contains("buttons"))
    buttons = ParseButtons(step["buttons"]);

  u64 dur_ns = 100000000ULL;
  if (step.contains("duration_ms") && step["duration_ms"].is_number())
    dur_ns = (u64)(step["duration_ms"].get<double>() * 1000000.0);

  u64 delay_ns = 0;
  if (step.contains("delay_ms") && step["delay_ms"].is_number())
    delay_ns = (u64)(step["delay_ms"].get<double>() * 1000000.0);

  HiddbgHdlsState pressed = {};
  pressed.battery_level    = 8;
  pressed.flags            = 1;
  pressed.buttons          = buttons;
  pressed.analog_stick_l.x = StickVal(step, "stick_left",  "x");
  pressed.analog_stick_l.y = StickVal(step, "stick_left",  "y");
  pressed.analog_stick_r.x = StickVal(step, "stick_right", "x");
  pressed.analog_stick_r.y = StickVal(step, "stick_right", "y");

  hiddbgSetHdlsState(ctx.handle, &pressed);
  svcSleepThread((s64)dur_ns);

  HiddbgHdlsState released = {};
  released.battery_level = 8;
  released.flags         = 1;
  hiddbgSetHdlsState(ctx.handle, &released);

  if (delay_ns > 0)
    svcSleepThread((s64)delay_ns);
}

// ---- HidSequenceTool -------------------------------------------------------

std::string HidSequenceTool::Name() const { return "hid_sequence"; }

std::string HidSequenceTool::Description() const {
  return "Executes a sequence of controller inputs in one call. Each step presses "
         "buttons/sticks for duration_ms then releases, then waits delay_ms before the next step.";
}

nlohmann::json HidSequenceTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"steps", {
        {"type", "array"},
        {"description", "Ordered list of input steps"},
        {"items", {
          {"type", "object"},
          {"properties", {
            {"buttons",     {{"type","array"},{"items",{{"type","string"}}},{"description","Buttons: A B X Y L R ZL ZR Plus Minus Up Down Left Right StickL StickR Home Capture"}}},
            {"duration_ms", {{"type","number"},{"description","Hold time in ms (default: 100)"}}},
            {"delay_ms",    {{"type","number"},{"description","Wait after release before next step (default: 0)"}}},
            {"stick_left",  {{"type","object"},{"properties",{{"x",{{"type","number"}}},{"y",{{"type","number"}}}}}}},
            {"stick_right", {{"type","object"},{"properties",{{"x",{{"type","number"}}},{"y",{{"type","number"}}}}}}}
          }}
        }}
      }}
    }},
    {"required", {"steps"}}
  };
}

ToolResult HidSequenceTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("steps") || !arguments["steps"].is_array())
    return {"steps array required", true};

  HdlsCtx ctx;
  Result rc = HdlsOpen(ctx);
  if (R_FAILED(rc))
    return {"hiddbg init failed: " + FmtRc(rc), true};

  const nlohmann::json& steps = arguments["steps"];
  for (const auto& step : steps)
    HidApplyStep(ctx, step);

  HdlsClose(ctx);

  char buf[32];
  snprintf(buf, sizeof(buf), "Executed %d steps", (int)steps.size());
  return {buf, false};
}

// ---- HidPressButtonsTool ---------------------------------------------------

std::string HidPressButtonsTool::Name() const { return "hid_press_buttons"; }

std::string HidPressButtonsTool::Description() const {
  return "Injects virtual controller input. Buttons are held for duration_ms then released. "
         "Analog sticks are also released after the duration.";
}

nlohmann::json HidPressButtonsTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"buttons", {
        {"type", "array"},
        {"items", {{"type","string"}}},
        {"description", "Buttons to press: A B X Y L R ZL ZR Plus Minus Up Down Left Right StickL StickR Home Capture"}
      }},
      {"duration_ms", {
        {"type", "number"},
        {"description", "Hold duration in milliseconds (default: 100)"}
      }},
      {"stick_left", {
        {"type", "object"},
        {"description", "Left analog stick, x/y in range [-1.0, 1.0]"},
        {"properties", {{"x", {{"type","number"}}}, {"y", {{"type","number"}}}}}
      }},
      {"stick_right", {
        {"type", "object"},
        {"description", "Right analog stick, x/y in range [-1.0, 1.0]"},
        {"properties", {{"x", {{"type","number"}}}, {"y", {{"type","number"}}}}}
      }}
    }},
    {"required", nlohmann::json::array()}
  };
}

ToolResult HidPressButtonsTool::Execute(const nlohmann::json& arguments) {
  HdlsCtx ctx;
  Result rc = HdlsOpen(ctx);
  if (R_FAILED(rc))
    return {"hiddbg init failed: " + FmtRc(rc), true};

  u64 buttons = 0;
  if (arguments.contains("buttons"))
    buttons = ParseButtons(arguments["buttons"]);

  u64 dur_ns = 100000000ULL;
  if (arguments.contains("duration_ms") && arguments["duration_ms"].is_number())
    dur_ns = (u64)(arguments["duration_ms"].get<double>() * 1000000.0);

  HiddbgHdlsState pressed = {};
  pressed.battery_level    = 8;
  pressed.flags            = 1;
  pressed.buttons          = buttons;
  pressed.analog_stick_l.x = StickVal(arguments, "stick_left",  "x");
  pressed.analog_stick_l.y = StickVal(arguments, "stick_left",  "y");
  pressed.analog_stick_r.x = StickVal(arguments, "stick_right", "x");
  pressed.analog_stick_r.y = StickVal(arguments, "stick_right", "y");

  rc = hiddbgSetHdlsState(ctx.handle, &pressed);
  if (R_SUCCEEDED(rc)) {
    svcSleepThread((s64)dur_ns);
    HiddbgHdlsState released = {};
    released.battery_level = 8;
    released.flags         = 1;
    hiddbgSetHdlsState(ctx.handle, &released);
  }

  HdlsClose(ctx);

  if (R_FAILED(rc))
    return {"SetHdlsState failed: " + FmtRc(rc), true};

  char buf[80];
  snprintf(buf, sizeof(buf), "Pressed 0x%016llX for %llu ms",
      (unsigned long long)buttons, (unsigned long long)(dur_ns / 1000000ULL));
  return {buf, false};
}

// ---- HidTouchTool ----------------------------------------------------------

std::string HidTouchTool::Name() const { return "hid_touch"; }

std::string HidTouchTool::Description() const {
  return "Injects a touch screen tap at given coordinates. Screen is 1280x720. "
         "Supports multi-touch via the touches array.";
}

nlohmann::json HidTouchTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"x", {{"type","integer"},{"description","X coordinate (0-1279), single touch"}}},
      {"y", {{"type","integer"},{"description","Y coordinate (0-719), single touch"}}},
      {"touches", {
        {"type", "array"},
        {"description", "Multi-touch: array of {x, y} objects (up to 16)"},
        {"items", {{"type","object"},{"properties",{{"x",{{"type","integer"}}},{"y",{{"type","integer"}}}}}}}
      }},
      {"duration_ms", {{"type","number"},{"description","Hold duration in ms (default: 100)"}}}
    }},
    {"required", nlohmann::json::array()}
  };
}

ToolResult HidTouchTool::Execute(const nlohmann::json& arguments) {
  HidTouchState states[16] = {};
  s32 count = 0;

  if (arguments.contains("touches") && arguments["touches"].is_array()) {
    const auto& arr = arguments["touches"];
    count = (s32)std::min(arr.size(), (size_t)16);
    for (s32 i = 0; i < count; i++) {
      const auto& t = arr[(size_t)i];
      states[i].finger_id  = (u32)i;
      states[i].x          = t.value("x", 0);
      states[i].y          = t.value("y", 0);
      states[i].diameter_x = 10;
      states[i].diameter_y = 10;
    }
  } else {
    if (!arguments.contains("x") || !arguments.contains("y"))
      return {"x and y required", true};
    states[0].finger_id  = 0;
    states[0].x          = (u32)arguments["x"].get<int>();
    states[0].y          = (u32)arguments["y"].get<int>();
    states[0].diameter_x = 10;
    states[0].diameter_y = 10;
    count = 1;
  }

  u64 dur_ns = 100000000ULL;
  if (arguments.contains("duration_ms") && arguments["duration_ms"].is_number())
    dur_ns = (u64)(arguments["duration_ms"].get<double>() * 1000000.0);

  Result rc = hiddbgInitialize();
  if (R_FAILED(rc)) return {"hiddbg init failed: " + FmtRc(rc), true};

  rc = hiddbgSetTouchScreenAutoPilotState(states, count);
  if (R_SUCCEEDED(rc)) {
    svcSleepThread((s64)dur_ns);
    hiddbgUnsetTouchScreenAutoPilotState();
  }
  hiddbgExit();

  if (R_FAILED(rc))
    return {"SetTouchScreenAutoPilotState failed: " + FmtRc(rc), true};

  char buf[80];
  snprintf(buf, sizeof(buf), "Touched %d point(s) for %llu ms",
      (int)count, (unsigned long long)(dur_ns / 1000000ULL));
  return {buf, false};
}

// ---- HidSwipeTool ----------------------------------------------------------

std::string HidSwipeTool::Name() const { return "hid_swipe"; }

std::string HidSwipeTool::Description() const {
  return "Drags a finger across the screen from (x1,y1) to (x2,y2). "
         "Useful for scrolling lists or swiping UI elements. Screen is 1280x720.";
}

nlohmann::json HidSwipeTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"x1", {{"type","integer"},{"description","Start X (0-1279)"}}},
      {"y1", {{"type","integer"},{"description","Start Y (0-719)"}}},
      {"x2", {{"type","integer"},{"description","End X (0-1279)"}}},
      {"y2", {{"type","integer"},{"description","End Y (0-719)"}}},
      {"duration_ms", {{"type","number"},{"description","Swipe duration in ms (default: 300)"}}},
      {"steps", {{"type","integer"},{"description","Number of interpolation steps (default: 30, max: 120)"}}}
    }},
    {"required", {"x1", "y1", "x2", "y2"}}
  };
}

ToolResult HidSwipeTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("x1") || !arguments.contains("y1") ||
      !arguments.contains("x2") || !arguments.contains("y2"))
    return {"x1, y1, x2, y2 required", true};

  int x1 = arguments["x1"].get<int>();
  int y1 = arguments["y1"].get<int>();
  int x2 = arguments["x2"].get<int>();
  int y2 = arguments["y2"].get<int>();

  double dur_ms = 300.0;
  if (arguments.contains("duration_ms") && arguments["duration_ms"].is_number())
    dur_ms = arguments["duration_ms"].get<double>();

  int steps = 30;
  if (arguments.contains("steps") && arguments["steps"].is_number())
    steps = arguments["steps"].get<int>();
  if (steps < 2)   steps = 2;
  if (steps > 120) steps = 120;

  Result rc = hiddbgInitialize();
  if (R_FAILED(rc)) return {"hiddbg init failed: " + FmtRc(rc), true};

  u64 step_ns = (u64)((dur_ms / steps) * 1000000.0);

  for (int i = 0; i < steps; i++) {
    float t = (float)i / (float)(steps - 1);
    HidTouchState state = {};
    state.finger_id  = 0;
    state.x          = (u32)(x1 + (x2 - x1) * t);
    state.y          = (u32)(y1 + (y2 - y1) * t);
    state.diameter_x = 10;
    state.diameter_y = 10;
    hiddbgSetTouchScreenAutoPilotState(&state, 1);
    svcSleepThread((s64)step_ns);
  }

  hiddbgUnsetTouchScreenAutoPilotState();
  hiddbgExit();

  char buf[128];
  snprintf(buf, sizeof(buf), "Swiped (%d,%d) -> (%d,%d) over %.0f ms in %d steps",
      x1, y1, x2, y2, dur_ms, steps);
  return {buf, false};
}

}  // namespace tools
