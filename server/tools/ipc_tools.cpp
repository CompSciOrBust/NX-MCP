#include "ipc_tools.hpp"
#include "tool_registry.hpp"
#include "tool_util.hpp"
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace tools {

static bool kCallRegistered =
    ToolRegistry::Instance().Register(std::make_unique<IpcCallTool>());
static bool kCloseRegistered =
    ToolRegistry::Instance().Register(std::make_unique<IpcCloseTool>());

// ---- helpers ---------------------------------------------------------------

static void HexDecode(const std::string& hex, u8* out, size_t max, size_t& out_sz) {
  size_t start = (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) ? 2 : 0;
  out_sz = 0;
  for (size_t i = start; i + 1 < hex.size() && out_sz < max; i += 2) {
    char b[3] = {hex[i], hex[i + 1], '\0'};
    out[out_sz++] = (u8)strtoul(b, nullptr, 16);
  }
}

static std::string HexEncode(const u8* buf, size_t len) {
  std::string s;
  s.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    char h[3];
    snprintf(h, sizeof(h), "%02X", buf[i]);
    s += h;
  }
  return s;
}

// ---- IPC worker thread state -----------------------------------------------

struct IpcWorkCtx {
  Service svc;
  u32 cmd_id;
  u8 in_buf[512];
  u32 in_size;
  u8 out_buf[512];
  u32 out_size;
  SfDispatchParams disp;
  Handle out_handles[8];
  Result rc;
  Handle signal_ev;
};

static IpcWorkCtx ipc_work;
static u8 ipc_stack[0x8000] __attribute__((aligned(0x1000)));
static volatile bool ipc_busy = false;

static void IpcWorkerFn(void*) {
  ipc_work.rc = serviceDispatchImpl(
      &ipc_work.svc, ipc_work.cmd_id,
      ipc_work.in_size  > 0 ? ipc_work.in_buf  : nullptr, ipc_work.in_size,
      ipc_work.out_size > 0 ? ipc_work.out_buf : nullptr, ipc_work.out_size,
      ipc_work.disp);
  svcSignalEvent(ipc_work.signal_ev);
}

// ---- IpcCallTool ------------------------------------------------------------

std::string IpcCallTool::Name() const { return "ipc_call"; }

std::string IpcCallTool::Description() const {
  return "Sends a raw IPC command to a named Switch system service. Supports inline data, "
         "send/receive buffer descriptors, and receiving output handles. Returns hex-encoded "
         "output and handle values.";
}

nlohmann::json IpcCallTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"service",     {{"type","string"},  {"description","Service name to open, e.g. \"set:sys\", \"acc:aa\""}}},
      {"session",     {{"type","string"},  {"description","Hex handle from a previous ipc_call (use instead of service)"}}},
      {"cmd_id",      {{"type","integer"}, {"description","Command ID to dispatch"}}},
      {"in_data",     {{"type","string"},  {"description","Hex-encoded inline input data (raw words section)"}}},
      {"out_size",    {{"type","integer"}, {"description","Number of inline output bytes to receive (sizeof the output struct). Must be set to a non-zero value to get any inline output back; omitting it returns nothing even if the command produces output. Max 512."}}},
      {"timeout_ms",  {{"type","integer"}, {"description","Timeout in milliseconds (default 5000)"}}},
      {"buffers", {
        {"type","array"},
        {"description","Buffer descriptors (up to 8). Each object: dir (\"in\"|\"out\"|\"inout\"), type (\"alias\"|\"pointer\"|\"auto\", default \"alias\"), fixed (bool). For in/inout buffers set data to the hex-encoded payload. For out/inout buffers set size to the number of bytes to allocate for the receive buffer — its contents are returned hex-encoded in the result. Both data and size may be present on an inout buffer."},
        {"items", {{"type","object"}}}
      }},
      {"out_handles", {
        {"type","array"},
        {"description","Expected output handles (up to 8). Each item is \"move\" or \"copy\". Returned values can be passed to session on follow-up calls."},
        {"items", {{"type","string"}}}
      }}
    }},
    {"required", {"cmd_id"}}
  };
}

ToolResult IpcCallTool::Execute(const nlohmann::json& arguments) {
  bool has_service = arguments.contains("service") && arguments["service"].is_string();
  bool has_session = arguments.contains("session") && arguments["session"].is_string();
  if (!has_service && !has_session)
    return {"service (name) or session (hex handle) required", true};
  if (!arguments.contains("cmd_id") || !arguments["cmd_id"].is_number())
    return {"cmd_id required (integer)", true};

  u32 cmd_id = (u32)arguments["cmd_id"].get<int>();

  u8 in_buf[512] = {};
  u32 in_size = 0;
  if (arguments.contains("in_data") && arguments["in_data"].is_string()) {
    size_t sz = 0;
    HexDecode(arguments["in_data"].get<std::string>(), in_buf, sizeof(in_buf), sz);
    in_size = (u32)sz;
  }

  u32 out_size = 0;
  if (arguments.contains("out_size") && arguments["out_size"].is_number()) {
    int v = arguments["out_size"].get<int>();
    out_size = (v > 0 && v <= 512) ? (u32)v : 0;
  }

  static u8 buf_store[8][512];
  bool buf_is_readable[8] = {};
  size_t buf_readable_sz[8] = {};

  SfDispatchParams disp = {};
  int num_bufs = 0;

  auto SetBufAttr = [&](int idx, u32 val) {
    switch (idx) {
      case 0: disp.buffer_attrs.attr0 = val; break;
      case 1: disp.buffer_attrs.attr1 = val; break;
      case 2: disp.buffer_attrs.attr2 = val; break;
      case 3: disp.buffer_attrs.attr3 = val; break;
      case 4: disp.buffer_attrs.attr4 = val; break;
      case 5: disp.buffer_attrs.attr5 = val; break;
      case 6: disp.buffer_attrs.attr6 = val; break;
      case 7: disp.buffer_attrs.attr7 = val; break;
    }
  };

  if (arguments.contains("buffers") && arguments["buffers"].is_array()) {
    const auto& arr = arguments["buffers"];
    num_bufs = (int)std::min(arr.size(), (size_t)8);
    for (int i = 0; i < num_bufs; i++) {
      const auto& b = arr[(size_t)i];
      std::string dir  = b.value("dir",   "out");
      std::string type = b.value("type",  "alias");
      bool fixed_size  = b.value("fixed", false);

      u32 attr = 0;
      if      (dir == "in")    attr |= SfBufferAttr_In;
      else if (dir == "inout") attr |= SfBufferAttr_In | SfBufferAttr_Out;
      else                     attr |= SfBufferAttr_Out;

      if      (type == "pointer") attr |= SfBufferAttr_HipcPointer;
      else if (type == "auto")    attr |= SfBufferAttr_HipcAutoSelect;
      else                        attr |= SfBufferAttr_HipcMapAlias;

      if (fixed_size) attr |= SfBufferAttr_FixedSize;

      SetBufAttr(i, attr);
      memset(buf_store[i], 0, sizeof(buf_store[i]));

      size_t bsz = 0;
      if ((attr & SfBufferAttr_In) && b.contains("data") && b["data"].is_string())
        HexDecode(b["data"].get<std::string>(), buf_store[i], sizeof(buf_store[i]), bsz);
      if ((attr & SfBufferAttr_Out) && b.contains("size") && b["size"].is_number()) {
        int v = b["size"].get<int>();
        bsz = (v > 0 && v <= 512) ? (size_t)v : bsz;
        buf_is_readable[i] = true;
        buf_readable_sz[i] = bsz;
      } else if (attr & SfBufferAttr_In) {
        buf_is_readable[i] = true;
        buf_readable_sz[i] = bsz;
      }

      disp.buffers[i] = {buf_store[i], bsz};
    }
  }

  int num_out_handles = 0;

  auto SetHandleAttr = [&](int idx, SfOutHandleAttr val) {
    switch (idx) {
      case 0: disp.out_handle_attrs.attr0 = val; break;
      case 1: disp.out_handle_attrs.attr1 = val; break;
      case 2: disp.out_handle_attrs.attr2 = val; break;
      case 3: disp.out_handle_attrs.attr3 = val; break;
      case 4: disp.out_handle_attrs.attr4 = val; break;
      case 5: disp.out_handle_attrs.attr5 = val; break;
      case 6: disp.out_handle_attrs.attr6 = val; break;
      case 7: disp.out_handle_attrs.attr7 = val; break;
    }
  };

  if (arguments.contains("out_handles") && arguments["out_handles"].is_array()) {
    const auto& arr = arguments["out_handles"];
    num_out_handles = (int)std::min(arr.size(), (size_t)8);
    for (int i = 0; i < num_out_handles; i++) {
      std::string htype = arr[(size_t)i].is_string()
          ? arr[(size_t)i].get<std::string>() : "move";
      SetHandleAttr(i, htype == "copy" ? SfOutHandleAttr_HipcCopy : SfOutHandleAttr_HipcMove);
    }
  }

  Service svc = {};
  Result rc = 0;
  bool owns_svc = false;
  std::string label;

  if (has_session) {
    Handle h = (Handle)strtoul(arguments["session"].get<std::string>().c_str(), nullptr, 16);
    svc.session = h;
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "session 0x%X", (unsigned)h);
    label = tmp;
  } else {
    std::string svc_name = arguments["service"].get<std::string>();
    rc = smGetService(&svc, svc_name.c_str());
    if (R_FAILED(rc))
      return {"smGetService(\"" + svc_name + "\") failed: " + FmtRc(rc), true};
    label = "\"" + svc_name + "\"";
    owns_svc = true;
  }

  if (ipc_busy)
    return {"previous IPC call timed out — restart module to recover", true};

  ipc_work.svc    = svc;
  ipc_work.cmd_id = cmd_id;
  memcpy(ipc_work.in_buf, in_buf, sizeof(ipc_work.in_buf));
  ipc_work.in_size  = in_size;
  ipc_work.out_size = out_size;
  memset(ipc_work.out_buf, 0, sizeof(ipc_work.out_buf));
  ipc_work.disp = disp;
  if (num_out_handles > 0) {
    memset(ipc_work.out_handles, 0, sizeof(ipc_work.out_handles));
    ipc_work.disp.out_handles = ipc_work.out_handles;
  }

  u64 timeout_ns = 5000000000ULL;
  if (arguments.contains("timeout_ms") && arguments["timeout_ms"].is_number()) {
    int ms = arguments["timeout_ms"].get<int>();
    if (ms > 0) timeout_ns = (u64)ms * 1000000ULL;
  }

  Handle wait_ev;
  if (R_FAILED(svcCreateEvent(&ipc_work.signal_ev, &wait_ev))) {
    if (owns_svc) serviceClose(&ipc_work.svc);
    return {"svcCreateEvent failed", true};
  }

  Thread t;
  threadCreate(&t, IpcWorkerFn, nullptr, ipc_stack, sizeof(ipc_stack), 0x2C, -2);
  threadStart(&t);
  ipc_busy = true;

  s32 idx = 0;
  Result wait_rc = svcWaitSynchronization(&idx, &wait_ev, 1, (s64)timeout_ns);
  svcCloseHandle(wait_ev);

  if (R_FAILED(wait_rc)) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s cmd %u timed out — restart module to recover",
        label.c_str(), (unsigned)cmd_id);
    return {buf, true};
  }

  ipc_busy = false;
  threadClose(&t);
  if (owns_svc) serviceClose(&ipc_work.svc);

  rc = ipc_work.rc;
  if (R_FAILED(rc)) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s cmd %u failed: %s",
        label.c_str(), (unsigned)cmd_id, FmtRc(rc).c_str());
    return {buf, true};
  }

  char hdr[128];
  snprintf(hdr, sizeof(hdr), "%s cmd %u OK", label.c_str(), (unsigned)cmd_id);
  std::string result = hdr;

  if (out_size > 0) {
    char s[32];
    snprintf(s, sizeof(s), ", inline[%u]: ", out_size);
    result += s;
    result += HexEncode(ipc_work.out_buf, out_size);
  }

  for (int i = 0; i < num_bufs; i++) {
    if (!buf_is_readable[i]) continue;
    char s[32];
    snprintf(s, sizeof(s), ", buf%d[%zu]: ", i, buf_readable_sz[i]);
    result += s;
    result += HexEncode(buf_store[i], buf_readable_sz[i]);
  }

  for (int i = 0; i < num_out_handles; i++) {
    char s[32];
    snprintf(s, sizeof(s), ", handle%d=0x%X", i, (unsigned)ipc_work.out_handles[i]);
    result += s;
  }

  return {result, false};
}

// ---- IpcCloseTool -----------------------------------------------------------

std::string IpcCloseTool::Name() const { return "ipc_close"; }

std::string IpcCloseTool::Description() const {
  return "Closes a kernel session handle obtained from ipc_call out_handles.";
}

nlohmann::json IpcCloseTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"session", {{"type","string"}, {"description","Hex handle value to close, e.g. \"0x1234\""}}}
    }},
    {"required", {"session"}}
  };
}

ToolResult IpcCloseTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("session") || !arguments["session"].is_string())
    return {"session (hex handle value) required", true};

  Handle h = (Handle)strtoul(arguments["session"].get<std::string>().c_str(), nullptr, 16);
  svcCloseHandle(h);

  char buf[64];
  snprintf(buf, sizeof(buf), "Closed handle 0x%X", (unsigned)h);
  return {buf, false};
}

}  // namespace tools
