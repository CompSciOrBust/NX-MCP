#include "screenshot_tool.hpp"
#include "tool_registry.hpp"
#include "tool_util.hpp"
#include <switch.h>
#include <cstdio>
#include <string>

namespace tools {

static bool kRegistered =
    ToolRegistry::Instance().Register(std::make_unique<ScreenshotTool>());

static const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(const u8* data, size_t len) {
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    u32 b = (u32)data[i] << 16;
    if (i + 1 < len) b |= (u32)data[i + 1] << 8;
    if (i + 2 < len) b |= (u32)data[i + 2];
    out += kB64[(b >> 18) & 0x3F];
    out += kB64[(b >> 12) & 0x3F];
    out += (i + 1 < len) ? kB64[(b >> 6) & 0x3F] : '=';
    out += (i + 2 < len) ? kB64[ b        & 0x3F] : '=';
  }
  return out;
}

static u8 jpeg_buf[CAPSSC_JPEG_BUFFER_SIZE];

std::string ScreenshotTool::Name() const { return "screenshot"; }

std::string ScreenshotTool::Description() const {
  return "Captures a JPEG screenshot of the Switch display and returns it as an image.";
}

nlohmann::json ScreenshotTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", nlohmann::json::object()},
    {"required", nlohmann::json::array()}
  };
}

ToolResult ScreenshotTool::Execute(const nlohmann::json&) {
  Result rc = capsscInitialize();
  if (R_FAILED(rc))
    return {"capsscInitialize failed: " + FmtRc(rc), true};

  u64 jpeg_size = 0;
  rc = capsscCaptureJpegScreenShot(
      &jpeg_size, jpeg_buf, sizeof(jpeg_buf),
      ViLayerStack_Default, 100000000 /* 100 ms timeout */);

  capsscExit();

  if (R_FAILED(rc))
    return {"capsscCaptureJpegScreenShot failed: " + FmtRc(rc), true};

  ToolResult result;
  result.image_data = Base64Encode(jpeg_buf, jpeg_size);
  result.image_mime = "image/jpeg";
  return result;
}

}  // namespace tools
