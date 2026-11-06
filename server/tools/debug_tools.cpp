#include "debug_tools.hpp"
#include "tool_registry.hpp"
#include "tool_util.hpp"
#include <switch.h>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace tools {

// ---- global debug session ---------------------------------------------------

static Handle g_debug_handle = INVALID_HANDLE;
static u64 g_debug_pid = 0;
static Mutex g_debug_mutex;

// ---- value-scan candidate list -----------------------------------------------
// Cheat-engine style: start scan builds this list; filter scan removes entries
// that no longer match. Everything stays on-device until the list shrinks to a
// useful size.

static std::vector<u64> g_scan_candidates;
static constexpr size_t kMaxScanCandidates = 65536;  // 512 KB

static void ClearScanList() {
  g_scan_candidates.clear();
  g_scan_candidates.shrink_to_fit();
}

// ---- event-pump thread -------------------------------------------------------
// While the process is running, this thread drains housekeeping debug events
// (thread create/exit, DebuggerAttached) and re-issues svcContinueDebugEvent.
// It self-stops — leaving the process paused — when any other exception fires
// (hardware BP hit, watchpoint, crash). The triggering event is staged in
// g_pump_staged_event and surfaced by the next DrainEvents(capture=true) call.

static std::atomic<bool> g_pump_running{false};
static Thread g_pump_thread;
static std::vector<DebugEventInfo> g_saved_events;

// Staging for events captured by the pump on self-stop.
static std::atomic<bool>  g_pump_staged_pending{false};
static DebugEventInfo     g_pump_staged_event{};
// True iff a pump thread has been started and not yet joined.
static std::atomic<bool>  g_pump_thread_alive{false};

static void PumpThreadFunc(void*) {
  while (g_pump_running.load(std::memory_order_relaxed)) {
    Handle h = g_debug_handle;
    if (h == INVALID_HANDLE) break;
    DebugEventInfo ev;
    if (R_SUCCEEDED(svcGetDebugEvent(&ev, h))) {
      // Stop on any exception except the two that we generate ourselves.
      // DebuggerAttached fires once at attach; DebuggerBreak is our own
      // svcBreakDebugProcess call from debug_pause. Everything else
      // (hardware BP hit, watchpoint, crash) should pause the process.
      if (ev.type == DebugEventType_Exception &&
          ev.info.exception.type != DebugException_DebuggerAttached &&
          ev.info.exception.type != DebugException_DebuggerBreak) {
        g_pump_staged_event = ev;
        g_pump_staged_pending.store(true, std::memory_order_release);
        g_pump_running.store(false, std::memory_order_relaxed);
        return;  // Leave process paused — do NOT call svcContinueDebugEvent.
      }
      svcContinueDebugEvent(h, 5, nullptr, 0);
    } else {
      svcSleepThread(500'000LL);  // 0.5ms idle poll
    }
  }
}

static void StartPump() {
  if (g_pump_running.exchange(true)) return;  // already running
  g_pump_thread_alive.store(true, std::memory_order_relaxed);
  threadCreate(&g_pump_thread, PumpThreadFunc, nullptr, nullptr, 0x2000, 44, -2);
  threadStart(&g_pump_thread);
}

static void StopPump() {
  g_pump_running.store(false, std::memory_order_relaxed);
  if (g_pump_thread_alive.exchange(false)) {
    threadWaitForExit(&g_pump_thread);
    threadClose(&g_pump_thread);
  }
}

static void CloseDebugSession() {
  StopPump();
  if (g_debug_handle != INVALID_HANDLE) {
    svcCloseHandle(g_debug_handle);
    g_debug_handle = INVALID_HANDLE;
    g_debug_pid = 0;
  }
}

// Drain all queued debug events.
// If capture=true, exception and process-exit events are saved to g_saved_events
// (replacing any previously saved batch).
static int DrainEvents(Handle debug, bool capture = false) {
  if (capture) {
    g_saved_events.clear();
    if (g_pump_staged_pending.exchange(false))
      g_saved_events.push_back(g_pump_staged_event);
  }
  int count = 0;
  DebugEventInfo ev;
  while (R_SUCCEEDED(svcGetDebugEvent(&ev, debug))) {
    if (capture &&
        (ev.type == DebugEventType_Exception || ev.type == DebugEventType_ExitProcess))
      g_saved_events.push_back(ev);
    count++;
  }
  return count;
}

// ---- registration -----------------------------------------------------------

static bool kAttachRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugAttachTool>());
static bool kPauseRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugPauseTool>());
static bool kResumeRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugResumeTool>());
static bool kReadMemRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugReadMemoryTool>());
static bool kWriteMemRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugWriteMemoryTool>());
static bool kQueryMemRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugQueryMemoryTool>());
static bool kDetachRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugDetachTool>());
static bool kGetThreadsRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugGetThreadsTool>());
static bool kGetRegsRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugGetRegistersTool>());
static bool kSetRegsRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugSetRegistersTool>());
static bool kGetEventsRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugGetEventsTool>());
static bool kCopyMemRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugCopyMemoryTool>());
static bool kDumpCodeRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugDumpCodeTool>());
static bool kBacktraceRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugBacktraceTool>());
static bool kFillMemRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugFillMemoryTool>());
static bool kDumpRegionRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugDumpRegionTool>());
static bool kValueScanRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugValueScanTool>());
static bool kScanClearRegistered =
    ToolRegistry::Instance().Register(std::make_unique<DebugScanClearTool>());

// ---- shared helpers ---------------------------------------------------------

static std::string Hex64(u64 v) {
  char buf[20];
  snprintf(buf, sizeof(buf), "0x%016llX", (unsigned long long)v);
  return buf;
}

static u64 ParseU64(const std::string& s) {
  return strtoull(s.c_str(), nullptr, 0);
}

static const char* ExceptionTypeName(u32 t) {
  switch (t) {
    case DebugException_UndefinedInstruction: return "UndefinedInstruction";
    case DebugException_InstructionAbort:     return "InstructionAbort";
    case DebugException_DataAbort:            return "DataAbort";
    case DebugException_AlignmentFault:       return "AlignmentFault";
    case DebugException_DebuggerAttached:     return "DebuggerAttached";
    case DebugException_BreakPoint:           return "BreakPoint";
    case DebugException_UserBreak:            return "UserBreak";
    case DebugException_DebuggerBreak:        return "DebuggerBreak";
    case DebugException_UndefinedSystemCall:  return "UndefinedSystemCall";
    case DebugException_MemorySystemError:    return "MemorySystemError";
    default:                                   return "Unknown";
  }
}

static std::string FmtDebugEvent(const DebugEventInfo& ev) {
  char buf[256];
  switch (ev.type) {
    case DebugEventType_CreateProcess:
      snprintf(buf, sizeof(buf),
          "[CreateProcess] tid=0x%llX title=%s pid=0x%llX name=\"%.12s\"\n",
          (unsigned long long)ev.thread_id,
          Hex64(ev.info.create_process.program_id).c_str(),
          (unsigned long long)ev.info.create_process.process_id,
          ev.info.create_process.name);
      return buf;
    case DebugEventType_CreateThread:
      snprintf(buf, sizeof(buf),
          "[CreateThread] tid=0x%llX tls=%s entry=%s\n",
          (unsigned long long)ev.thread_id,
          Hex64((u64)ev.info.create_thread.tls_address).c_str(),
          Hex64((u64)ev.info.create_thread.entrypoint).c_str());
      return buf;
    case DebugEventType_ExitProcess:
      snprintf(buf, sizeof(buf), "[ExitProcess] tid=0x%llX reason=%u\n",
          (unsigned long long)ev.thread_id, ev.info.exit_process.reason);
      return buf;
    case DebugEventType_ExitThread:
      snprintf(buf, sizeof(buf), "[ExitThread] tid=0x%llX reason=%u\n",
          (unsigned long long)ev.thread_id, ev.info.exit_thread.reason);
      return buf;
    case DebugEventType_Exception:
      snprintf(buf, sizeof(buf),
          "[Exception] tid=0x%llX type=%s addr=%s\n",
          (unsigned long long)ev.thread_id,
          ExceptionTypeName(ev.info.exception.type),
          Hex64((u64)ev.info.exception.address).c_str());
      return buf;
    default:
      snprintf(buf, sizeof(buf), "[Event type=%u tid=0x%llX]\n",
          ev.type, (unsigned long long)ev.thread_id);
      return buf;
  }
}

static const char* MemTypeName(u32 t) {
  switch (t) {
    case MemType_Unmapped:            return "Unmapped";
    case MemType_Io:                  return "Io";
    case MemType_Normal:              return "Normal";
    case MemType_CodeStatic:          return "CodeStatic";
    case MemType_CodeMutable:         return "CodeMutable";
    case MemType_Heap:                return "Heap";
    case MemType_SharedMem:           return "SharedMem";
    case MemType_WeirdMappedMem:      return "WeirdMappedMem";
    case MemType_ModuleCodeStatic:    return "ModuleCodeStatic";
    case MemType_ModuleCodeMutable:   return "ModuleCodeMutable";
    case MemType_IpcBuffer0:          return "IpcBuffer0";
    case MemType_MappedMemory:        return "MappedMemory";
    case MemType_ThreadLocal:         return "ThreadLocal";
    case MemType_TransferMemIsolated: return "TransferMemIsolated";
    case MemType_TransferMem:         return "TransferMem";
    case MemType_ProcessMem:          return "ProcessMem";
    case MemType_Reserved:            return "Reserved";
    case MemType_IpcBuffer1:          return "IpcBuffer1";
    case MemType_IpcBuffer3:          return "IpcBuffer3";
    case MemType_KernelStack:         return "KernelStack";
    case MemType_CodeReadOnly:        return "CodeReadOnly";
    case MemType_CodeWritable:        return "CodeWritable";
    default:                          return "Unknown";
  }
}

static std::string PermStr(u32 perm) {
  char buf[4] = "---";
  if (perm & Perm_R) buf[0] = 'r';
  if (perm & Perm_W) buf[1] = 'w';
  if (perm & Perm_X) buf[2] = 'x';
  return {buf, 3};
}

static std::string FmtThreadContext(u64 tid, const ThreadContext& ctx) {
  std::string out;
  char buf[128];
  snprintf(buf, sizeof(buf), "Thread 0x%llX:\n", (unsigned long long)tid);
  out += buf;
  for (int i = 0; i <= 28; i++) {
    snprintf(buf, sizeof(buf), "  x%-2d= %s", i, Hex64(ctx.cpu_gprs[i].x).c_str());
    out += buf;
    out += ((i % 4 == 3) ? "\n" : "  ");
  }
  snprintf(buf, sizeof(buf),
      "\n  fp = %s  lr = %s\n"
      "  sp = %s  pc = %s\n"
      "  psr= 0x%08X\n",
      Hex64(ctx.fp).c_str(), Hex64(ctx.lr).c_str(),
      Hex64(ctx.sp).c_str(), Hex64(ctx.pc.x).c_str(),
      ctx.psr);
  out += buf;
  return out;
}

// Dump thread list with register contexts. Requires process to be stopped.
static std::string DumpThreadContexts(Handle debug) {
  static u64 tids[256];
  s32 count = 0;
  Result rc = svcGetThreadList(&count, tids, 256, debug);
  if (R_FAILED(rc)) {
    char buf[64];
    snprintf(buf, sizeof(buf), "(svcGetThreadList failed: %s)\n", FmtRc(rc).c_str());
    return buf;
  }

  std::string out;
  char buf[64];
  snprintf(buf, sizeof(buf), "Threads (%d):\n", count);
  out += buf;

  for (s32 i = 0; i < count; i++) {
    ThreadContext ctx{};
    rc = svcGetDebugThreadContext(&ctx, debug, tids[i], RegisterGroup_CpuAll);
    if (R_FAILED(rc)) {
      snprintf(buf, sizeof(buf), "  Thread 0x%llX: context unavailable (%s)\n",
               (unsigned long long)tids[i], FmtRc(rc).c_str());
      out += buf;
    } else {
      out += FmtThreadContext(tids[i], ctx);
    }
  }
  return out;
}

// Parse a hex string (with optional spaces) into bytes.
// Returns false if the string contains invalid characters.
static bool ParseHexBytes(const std::string& hex, std::vector<u8>& out) {
  out.clear();
  std::string clean;
  for (char c : hex) {
    if (c == ' ' || c == '\t') continue;
    if (!isxdigit(static_cast<unsigned char>(c))) return false;
    clean += c;
  }
  if (clean.size() % 2 != 0) return false;
  for (size_t i = 0; i < clean.size(); i += 2) {
    char byte_str[3] = {clean[i], clean[i + 1], '\0'};
    out.push_back(static_cast<u8>(strtoul(byte_str, nullptr, 16)));
  }
  return true;
}

// ---- DebugAttachTool --------------------------------------------------------

std::string DebugAttachTool::Name() const { return "debug_attach"; }

std::string DebugAttachTool::Description() const {
  return "Attaches the kernel debugger to a process by PID. The process is immediately "
         "paused. Returns thread list with full CPU register state (PC, SP, LR, x0-x28). "
         "Any existing debug session is closed first. Use debug_resume to let it run again.";
}

nlohmann::json DebugAttachTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"pid", {{"type","string"},{"description","Target process ID (decimal or hex, e.g. '256' or '0x100')"}}}
    }},
    {"required", {"pid"}}
  };
}

ToolResult DebugAttachTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("pid") || !arguments["pid"].is_string())
    return {"pid required (string, decimal or hex)", true};

  u64 pid = ParseU64(arguments["pid"].get<std::string>());

  mutexLock(&g_debug_mutex);

  // Preserve scan candidates when re-attaching to the same process;
  // clear them when switching to a different one (addresses are no longer valid).
  if (g_debug_pid != 0 && g_debug_pid != pid)
    ClearScanList();

  CloseDebugSession();

  Handle debug = INVALID_HANDLE;
  Result rc = svcDebugActiveProcess(&debug, pid);
  if (R_FAILED(rc)) {
    mutexUnlock(&g_debug_mutex);
    return {"svcDebugActiveProcess failed: " + FmtRc(rc), true};
  }

  g_debug_handle = debug;
  g_debug_pid = pid;

  DrainEvents(debug, /*capture=*/true);

  std::string out;
  char buf[64];
  snprintf(buf, sizeof(buf), "Attached to PID %llu (stopped)\n", (unsigned long long)pid);
  out += buf;

  if (!g_saved_events.empty()) {
    out += "\nEvents:\n";
    for (const auto& ev : g_saved_events)
      out += "  " + FmtDebugEvent(ev);
  }

  out += "\n";
  out += DumpThreadContexts(debug);

  mutexUnlock(&g_debug_mutex);
  return {out, false};
}

// ---- DebugPauseTool ---------------------------------------------------------

std::string DebugPauseTool::Name() const { return "debug_pause"; }

std::string DebugPauseTool::Description() const {
  return "Breaks (pauses) the currently attached process and returns the register state of all threads. "
         "Requires an active debug session (debug_attach first).";
}

nlohmann::json DebugPauseTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", nlohmann::json::object()},
    {"required", nlohmann::json::array()}
  };
}

ToolResult DebugPauseTool::Execute(const nlohmann::json&) {
  mutexLock(&g_debug_mutex);

  if (g_debug_handle == INVALID_HANDLE) {
    mutexUnlock(&g_debug_mutex);
    return {"No active debug session. Call debug_attach first.", true};
  }

  // Stop the pump first so it doesn't auto-continue the DebuggerBreak event.
  StopPump();

  Result rc = svcBreakDebugProcess(g_debug_handle);
  if (R_FAILED(rc)) {
    mutexUnlock(&g_debug_mutex);
    return {"svcBreakDebugProcess failed: " + FmtRc(rc), true};
  }

  DrainEvents(g_debug_handle, /*capture=*/true);

  std::string out;
  char buf[64];
  snprintf(buf, sizeof(buf), "PID %llu paused\n", (unsigned long long)g_debug_pid);
  out += buf;

  if (!g_saved_events.empty()) {
    out += "\nEvents:\n";
    for (const auto& ev : g_saved_events)
      out += "  " + FmtDebugEvent(ev);
  }

  out += "\n";
  out += DumpThreadContexts(g_debug_handle);

  mutexUnlock(&g_debug_mutex);
  return {out, false};
}

// ---- DebugResumeTool --------------------------------------------------------

std::string DebugResumeTool::Name() const { return "debug_resume"; }

std::string DebugResumeTool::Description() const {
  return "Resumes the currently attached process. The debug session remains active — "
         "memory reads/writes still work, and debug_pause can pause it again.";
}

nlohmann::json DebugResumeTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", nlohmann::json::object()},
    {"required", nlohmann::json::array()}
  };
}

ToolResult DebugResumeTool::Execute(const nlohmann::json&) {
  mutexLock(&g_debug_mutex);

  if (g_debug_handle == INVALID_HANDLE) {
    mutexUnlock(&g_debug_mutex);
    return {"No active debug session.", true};
  }

  // Drain any events that arrived since attach/last-pause, then continue.
  DrainEvents(g_debug_handle);
  Result rc = svcContinueDebugEvent(g_debug_handle, 5, nullptr, 0);

  if (R_SUCCEEDED(rc))
    StartPump();  // keep draining future events so the process stays running

  char buf[64];
  snprintf(buf, sizeof(buf), "PID %llu resumed (rc=%s)",
           (unsigned long long)g_debug_pid, FmtRc(rc).c_str());

  mutexUnlock(&g_debug_mutex);
  return {buf, R_FAILED(rc)};
}

// ---- DebugReadMemoryTool ----------------------------------------------------

std::string DebugReadMemoryTool::Name() const { return "debug_read_memory"; }

std::string DebugReadMemoryTool::Description() const {
  return "Reads memory from the attached process and returns a hex dump. "
         "Maximum 4096 bytes per call. Works while the process is running or paused.";
}

nlohmann::json DebugReadMemoryTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"address", {{"type","string"},{"description","Start address (hex, e.g. '0x8000000')"}}},
      {"size",    {{"type","string"},{"description","Byte count to read (decimal or hex, max 4096)"}}}
    }},
    {"required", {"address", "size"}}
  };
}

ToolResult DebugReadMemoryTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("address") || !arguments["address"].is_string() ||
      !arguments.contains("size")    || !arguments["size"].is_string())
    return {"address and size required (strings)", true};

  u64 addr = ParseU64(arguments["address"].get<std::string>());
  u64 size = ParseU64(arguments["size"].get<std::string>());

  if (size == 0 || size > 4096)
    return {"size must be 1-4096", true};

  mutexLock(&g_debug_mutex);

  if (g_debug_handle == INVALID_HANDLE) {
    mutexUnlock(&g_debug_mutex);
    return {"No active debug session.", true};
  }

  std::vector<u8> buf(size);
  Result rc = svcReadDebugProcessMemory(buf.data(), g_debug_handle, addr, size);

  mutexUnlock(&g_debug_mutex);

  if (R_FAILED(rc))
    return {"svcReadDebugProcessMemory failed: " + FmtRc(rc), true};

  // xxd-style hex dump: address | 16 bytes hex | ASCII
  std::string out;
  char line[80];
  for (u64 i = 0; i < size; i += 16) {
    u64 row_addr = addr + i;
    u64 row_size = (size - i < 16) ? (size - i) : 16;

    // address column
    snprintf(line, sizeof(line), "%016llX  ", (unsigned long long)row_addr);
    out += line;

    // hex columns
    for (u64 j = 0; j < 16; j++) {
      if (j < row_size) {
        snprintf(line, sizeof(line), "%02X ", buf[i + j]);
        out += line;
      } else {
        out += "   ";
      }
      if (j == 7) out += " ";
    }

    // ASCII column
    out += " |";
    for (u64 j = 0; j < row_size; j++) {
      u8 c = buf[i + j];
      out += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.';
    }
    out += "|\n";
  }

  return {out, false};
}

// ---- DebugWriteMemoryTool ---------------------------------------------------

std::string DebugWriteMemoryTool::Name() const { return "debug_write_memory"; }

std::string DebugWriteMemoryTool::Description() const {
  return "Writes bytes to the attached process's memory. "
         "data is a hex string (e.g. 'DEADBEEF' or 'DE AD BE EF').";
}

nlohmann::json DebugWriteMemoryTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"address", {{"type","string"},{"description","Target address (hex)"}}},
      {"data",    {{"type","string"},{"description","Bytes to write as hex string (e.g. 'DEADBEEF')"}}}
    }},
    {"required", {"address", "data"}}
  };
}

ToolResult DebugWriteMemoryTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("address") || !arguments["address"].is_string() ||
      !arguments.contains("data")    || !arguments["data"].is_string())
    return {"address and data required (strings)", true};

  u64 addr = ParseU64(arguments["address"].get<std::string>());
  std::vector<u8> bytes;
  if (!ParseHexBytes(arguments["data"].get<std::string>(), bytes) || bytes.empty())
    return {"data must be a valid hex string", true};

  mutexLock(&g_debug_mutex);

  if (g_debug_handle == INVALID_HANDLE) {
    mutexUnlock(&g_debug_mutex);
    return {"No active debug session.", true};
  }

  Result rc = svcWriteDebugProcessMemory(g_debug_handle, bytes.data(), addr, bytes.size());

  mutexUnlock(&g_debug_mutex);

  char buf[80];
  snprintf(buf, sizeof(buf), "Wrote %zu bytes to %s (rc=%s)",
           bytes.size(), Hex64(addr).c_str(), FmtRc(rc).c_str());
  return {buf, R_FAILED(rc)};
}

// ---- DebugQueryMemoryTool ---------------------------------------------------

std::string DebugQueryMemoryTool::Name() const { return "debug_query_memory"; }

std::string DebugQueryMemoryTool::Description() const {
  return "Walks the memory map of the attached process starting from address, "
         "returning up to 64 consecutive regions. Useful for finding code, heap, and stack regions.";
}

nlohmann::json DebugQueryMemoryTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"address", {{"type","string"},{"description","Start address for the walk (use '0x0' for full map)"}}},
      {"limit",   {{"type","string"},{"description","Max regions to return (default 64, max 256)"}}}
    }},
    {"required", {"address"}}
  };
}

ToolResult DebugQueryMemoryTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("address") || !arguments["address"].is_string())
    return {"address required (string, hex)", true};

  u64 addr = ParseU64(arguments["address"].get<std::string>());
  u64 limit = 64;
  if (arguments.contains("limit") && arguments["limit"].is_string())
    limit = ParseU64(arguments["limit"].get<std::string>());
  if (limit == 0 || limit > 256) limit = 64;

  mutexLock(&g_debug_mutex);

  if (g_debug_handle == INVALID_HANDLE) {
    mutexUnlock(&g_debug_mutex);
    return {"No active debug session.", true};
  }

  std::string out;
  char buf[128];
  u64 cursor = addr;

  for (u64 i = 0; i < limit; i++) {
    MemoryInfo mi{};
    u32 pageinfo = 0;
    Result rc = svcQueryDebugProcessMemory(&mi, &pageinfo, g_debug_handle, cursor);
    if (R_FAILED(rc)) {
      snprintf(buf, sizeof(buf), "svcQueryDebugProcessMemory failed at %s: %s\n",
               Hex64(cursor).c_str(), FmtRc(rc).c_str());
      out += buf;
      break;
    }

    snprintf(buf, sizeof(buf), "%s-%s  %-22s  %s\n",
             Hex64(mi.addr).c_str(),
             Hex64(mi.addr + mi.size).c_str(),
             MemTypeName(mi.type),
             PermStr(mi.perm).c_str());
    out += buf;

    // End of address space
    u64 next = mi.addr + mi.size;
    if (next <= cursor || next == 0) break;
    cursor = next;
  }

  mutexUnlock(&g_debug_mutex);
  return {out, false};
}

// ---- DebugDetachTool --------------------------------------------------------

std::string DebugDetachTool::Name() const { return "debug_detach"; }

std::string DebugDetachTool::Description() const {
  return "Closes the active debug session and detaches from the process. "
         "The process resumes running automatically when the handle is closed. "
         "Call this instead of debug_resume when you are done debugging.";
}

nlohmann::json DebugDetachTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", nlohmann::json::object()},
    {"required", nlohmann::json::array()}
  };
}

ToolResult DebugDetachTool::Execute(const nlohmann::json&) {
  mutexLock(&g_debug_mutex);

  if (g_debug_handle == INVALID_HANDLE) {
    mutexUnlock(&g_debug_mutex);
    return {"No active debug session.", true};
  }

  u64 pid = g_debug_pid;
  ClearScanList();
  CloseDebugSession();

  mutexUnlock(&g_debug_mutex);

  char buf[64];
  snprintf(buf, sizeof(buf), "Detached from PID %llu.", (unsigned long long)pid);
  return {buf, false};
}

// ---- DebugGetThreadsTool ----------------------------------------------------

std::string DebugGetThreadsTool::Name() const { return "debug_get_threads"; }

std::string DebugGetThreadsTool::Description() const {
  return "Lists all threads in the attached process with full CPU register context "
         "(PC, SP, LR, x0-x28). For accurate register values the process should be paused. "
         "Optionally pass a thread ID to show only that thread.";
}

nlohmann::json DebugGetThreadsTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"tid", {{"type","string"},{"description","Thread ID to inspect (hex or decimal). Omit to show all threads."}}}
    }},
    {"required", nlohmann::json::array()}
  };
}

ToolResult DebugGetThreadsTool::Execute(const nlohmann::json& arguments) {
  mutexLock(&g_debug_mutex);

  if (g_debug_handle == INVALID_HANDLE) {
    mutexUnlock(&g_debug_mutex);
    return {"No active debug session.", true};
  }

  std::string out;

  if (arguments.contains("tid") && arguments["tid"].is_string()) {
    u64 tid = ParseU64(arguments["tid"].get<std::string>());
    ThreadContext ctx{};
    Result rc = svcGetDebugThreadContext(&ctx, g_debug_handle, tid, RegisterGroup_CpuAll);
    if (R_FAILED(rc)) {
      mutexUnlock(&g_debug_mutex);
      char buf[64];
      snprintf(buf, sizeof(buf), "svcGetDebugThreadContext failed: %s", FmtRc(rc).c_str());
      return {buf, true};
    }
    out = FmtThreadContext(tid, ctx);
  } else {
    out = DumpThreadContexts(g_debug_handle);
  }

  mutexUnlock(&g_debug_mutex);
  return {out, false};
}

// ---- DebugGetRegistersTool --------------------------------------------------

std::string DebugGetRegistersTool::Name() const { return "debug_get_registers"; }

std::string DebugGetRegistersTool::Description() const {
  return "Returns the full CPU register context (x0-x28, fp, lr, sp, pc, psr) for a specific "
         "thread. The process should be paused for accurate values.";
}

nlohmann::json DebugGetRegistersTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"tid", {{"type","string"},{"description","Thread ID (hex or decimal)"}}}
    }},
    {"required", {"tid"}}
  };
}

ToolResult DebugGetRegistersTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("tid") || !arguments["tid"].is_string())
    return {"tid required (string, hex or decimal)", true};

  u64 tid = ParseU64(arguments["tid"].get<std::string>());

  mutexLock(&g_debug_mutex);

  if (g_debug_handle == INVALID_HANDLE) {
    mutexUnlock(&g_debug_mutex);
    return {"No active debug session.", true};
  }

  ThreadContext ctx{};
  Result rc = svcGetDebugThreadContext(&ctx, g_debug_handle, tid, RegisterGroup_CpuAll);

  mutexUnlock(&g_debug_mutex);

  if (R_FAILED(rc)) {
    char buf[64];
    snprintf(buf, sizeof(buf), "svcGetDebugThreadContext failed: %s", FmtRc(rc).c_str());
    return {buf, true};
  }

  return {FmtThreadContext(tid, ctx), false};
}

// ---- DebugSetRegistersTool --------------------------------------------------

std::string DebugSetRegistersTool::Name() const { return "debug_set_registers"; }

std::string DebugSetRegistersTool::Description() const {
  return "Writes CPU registers for a specific thread. Pass 'registers' as a JSON object "
         "mapping register names to hex/decimal value strings. "
         "Supported names: x0-x28, fp, lr, sp, pc, psr. "
         "Only the specified registers are changed; others are preserved. "
         "The process must be paused.";
}

nlohmann::json DebugSetRegistersTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"tid",       {{"type","string"},{"description","Thread ID (hex or decimal)"}}},
      {"registers", {{"type","object"},{"description","Map of register name to value string, e.g. {\"pc\":\"0x8001234\",\"x0\":\"0\"}"}}}
    }},
    {"required", {"tid", "registers"}}
  };
}

ToolResult DebugSetRegistersTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("tid") || !arguments["tid"].is_string())
    return {"tid required (string)", true};
  if (!arguments.contains("registers") || !arguments["registers"].is_object())
    return {"registers required (object)", true};

  u64 tid = ParseU64(arguments["tid"].get<std::string>());
  const auto& regs = arguments["registers"];

  mutexLock(&g_debug_mutex);

  if (g_debug_handle == INVALID_HANDLE) {
    mutexUnlock(&g_debug_mutex);
    return {"No active debug session.", true};
  }

  // Read current context.
  ThreadContext ctx{};
  Result rc = svcGetDebugThreadContext(&ctx, g_debug_handle, tid, RegisterGroup_CpuAll);
  if (R_FAILED(rc)) {
    mutexUnlock(&g_debug_mutex);
    return {"svcGetDebugThreadContext failed: " + FmtRc(rc), true};
  }

  // Apply requested changes.
  std::string applied;
  char buf[64];
  for (auto it = regs.begin(); it != regs.end(); ++it) {
    if (!it.value().is_string()) continue;
    const std::string& name = it.key();
    u64 val = ParseU64(it.value().get<std::string>());

    if (name.size() >= 2 && name[0] == 'x') {
      int idx = atoi(name.c_str() + 1);
      if (idx >= 0 && idx <= 28) {
        ctx.cpu_gprs[idx].x = val;
        snprintf(buf, sizeof(buf), " x%d=%s", idx, Hex64(val).c_str());
        applied += buf;
        continue;
      }
    }
    if (name == "fp")  { ctx.fp    = val; applied += " fp="  + Hex64(val); }
    else if (name == "lr")  { ctx.lr    = val; applied += " lr="  + Hex64(val); }
    else if (name == "sp")  { ctx.sp    = val; applied += " sp="  + Hex64(val); }
    else if (name == "pc")  { ctx.pc.x  = val; applied += " pc="  + Hex64(val); }
    else if (name == "psr") { ctx.psr   = static_cast<u32>(val);
                              snprintf(buf, sizeof(buf), " psr=0x%08X", ctx.psr);
                              applied += buf; }
  }

  if (applied.empty()) {
    mutexUnlock(&g_debug_mutex);
    return {"No valid register names provided.", true};
  }

  rc = svcSetDebugThreadContext(g_debug_handle, tid, &ctx, RegisterGroup_CpuAll);

  mutexUnlock(&g_debug_mutex);

  if (R_FAILED(rc))
    return {"svcSetDebugThreadContext failed: " + FmtRc(rc), true};

  return {"Set registers for thread 0x" +
          std::to_string(tid) + ":" + applied, false};
}

// ---- DebugGetEventsTool -----------------------------------------------------

std::string DebugGetEventsTool::Name() const { return "debug_get_events"; }

std::string DebugGetEventsTool::Description() const {
  return "Returns debug events captured during the last debug_attach or debug_pause call, "
         "plus any newly queued events (requires the process to be paused). "
         "Exception events include the fault address and exception type, making this the "
         "primary tool for diagnosing crash cause. Call immediately after debug_attach on "
         "a crashed process to see the exception event before resuming.";
}

nlohmann::json DebugGetEventsTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", nlohmann::json::object()},
    {"required", nlohmann::json::array()}
  };
}

ToolResult DebugGetEventsTool::Execute(const nlohmann::json&) {
  mutexLock(&g_debug_mutex);

  if (g_debug_handle == INVALID_HANDLE) {
    mutexUnlock(&g_debug_mutex);
    return {"No active debug session.", true};
  }

  // Pick up any exception event staged by the pump while the process was running.
  // Do NOT call DrainEvents here — that clears g_saved_events and re-drains the
  // kernel queue which was already drained by debug_attach/debug_pause.
  if (g_pump_staged_pending.exchange(false))
    g_saved_events.push_back(g_pump_staged_event);

  if (g_saved_events.empty()) {
    mutexUnlock(&g_debug_mutex);
    return {"No events captured. Attach to a process or call debug_pause first.", false};
  }

  std::string out;
  char buf[64];
  snprintf(buf, sizeof(buf), "%zu event(s):\n", g_saved_events.size());
  out += buf;
  for (const auto& ev : g_saved_events)
    out += "  " + FmtDebugEvent(ev);

  mutexUnlock(&g_debug_mutex);
  return {out, false};
}

// ---- DebugCopyMemoryTool ----------------------------------------------------

std::string DebugCopyMemoryTool::Name() const { return "debug_copy_memory"; }

std::string DebugCopyMemoryTool::Description() const {
  return "Copies a block of memory from one address to another within the attached process, "
         "entirely on-device. No data passes through the model's context. "
         "Maximum 1 MB per call; transfer is done in 64 KB chunks. "
         "The process may be running or paused.";
}

nlohmann::json DebugCopyMemoryTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"src",  {{"type","string"},{"description","Source address (hex)"}}},
      {"dst",  {{"type","string"},{"description","Destination address (hex)"}}},
      {"size", {{"type","string"},{"description","Number of bytes to copy (decimal or hex, max 1048576)"}}}
    }},
    {"required", {"src", "dst", "size"}}
  };
}

ToolResult DebugCopyMemoryTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("src")  || !arguments["src"].is_string()  ||
      !arguments.contains("dst")  || !arguments["dst"].is_string()  ||
      !arguments.contains("size") || !arguments["size"].is_string())
    return {"src, dst, and size required (strings)", true};

  u64 src  = ParseU64(arguments["src"].get<std::string>());
  u64 dst  = ParseU64(arguments["dst"].get<std::string>());
  u64 size = ParseU64(arguments["size"].get<std::string>());

  if (size == 0 || size > 0x100000)
    return {"size must be 1–1048576 (1 MB)", true};

  mutexLock(&g_debug_mutex);

  if (g_debug_handle == INVALID_HANDLE) {
    mutexUnlock(&g_debug_mutex);
    return {"No active debug session.", true};
  }

  static constexpr size_t kChunk = 0x10000;  // 64 KB
  std::vector<u8> buf(kChunk);

  u64 copied = 0;
  while (copied < size) {
    size_t n = static_cast<size_t>(
        (size - copied < kChunk) ? (size - copied) : kChunk);

    Result rc = svcReadDebugProcessMemory(buf.data(), g_debug_handle, src + copied, n);
    if (R_FAILED(rc)) {
      mutexUnlock(&g_debug_mutex);
      char msg[80];
      snprintf(msg, sizeof(msg), "Read failed at %s after %llu bytes: %s",
               Hex64(src + copied).c_str(), (unsigned long long)copied, FmtRc(rc).c_str());
      return {msg, true};
    }

    rc = svcWriteDebugProcessMemory(g_debug_handle, buf.data(), dst + copied, n);
    if (R_FAILED(rc)) {
      mutexUnlock(&g_debug_mutex);
      char msg[80];
      snprintf(msg, sizeof(msg), "Write failed at %s after %llu bytes: %s",
               Hex64(dst + copied).c_str(), (unsigned long long)copied, FmtRc(rc).c_str());
      return {msg, true};
    }

    copied += n;
  }

  mutexUnlock(&g_debug_mutex);

  char msg[128];
  snprintf(msg, sizeof(msg), "Copied %llu bytes from %s to %s",
           (unsigned long long)size, Hex64(src).c_str(), Hex64(dst).c_str());
  return {msg, false};
}

// ---- DebugDumpCodeTool -------------------------------------------------------

std::string DebugDumpCodeTool::Name() const { return "debug_dump_code"; }

std::string DebugDumpCodeTool::Description() const {
  return "Dumps all executable code regions of a process to the SD card. "
         "Detects module boundaries (contiguous runs of CodeStatic/CodeMutable regions) "
         "and writes each module as a flat binary named by its base address under "
         "sdmc:/dumps/<pid>/. Does not require an existing debug session — opens a "
         "temporary one and resumes the process when done. "
         "Returns the output directory and a list of modules with base address and size.";
}

nlohmann::json DebugDumpCodeTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"pid", {{"type","string"},{"description","Target process ID (decimal or hex)"}}}
    }},
    {"required", {"pid"}}
  };
}

static bool IsCodeType(u32 type) {
  return type == MemType_CodeStatic    || type == MemType_CodeMutable ||
         type == MemType_ModuleCodeStatic || type == MemType_ModuleCodeMutable;
}

ToolResult DebugDumpCodeTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("pid") || !arguments["pid"].is_string())
    return {"pid required (string, decimal or hex)", true};

  u64 pid = ParseU64(arguments["pid"].get<std::string>());

  mutexLock(&g_debug_mutex);

  // Open a temporary debug handle (or reuse the active session if it matches).
  Handle debug = INVALID_HANDLE;
  bool owned = false;

  if (g_debug_pid == pid && g_debug_handle != INVALID_HANDLE) {
    debug = g_debug_handle;
  } else {
    Result rc = svcDebugActiveProcess(&debug, pid);
    if (R_FAILED(rc)) {
      mutexUnlock(&g_debug_mutex);
      return {"svcDebugActiveProcess failed: " + FmtRc(rc), true};
    }
    owned = true;
    // Drain the initial attach events.
    DebugEventInfo ev;
    while (R_SUCCEEDED(svcGetDebugEvent(&ev, debug))) {}
  }

  // Create output directory: sdmc:/dumps/<pid>/
  mkdir("sdmc:/dumps", 0777);
  char dir[64];
  snprintf(dir, sizeof(dir), "sdmc:/dumps/%llu", (unsigned long long)pid);
  mkdir(dir, 0777);

  static constexpr size_t kChunk = 0x10000;  // 64 KB
  std::vector<u8> buf(kChunk);

  std::string out;
  char line[128];
  snprintf(line, sizeof(line), "Output: %s\n\n", dir);
  out += line;

  // Walk the address space and group contiguous code regions into modules.
  u64 cursor = 0;
  u64 mod_start = 0;
  u64 mod_end = 0;
  bool in_module = false;
  int mod_count = 0;

  auto flush_module = [&]() -> std::string {
    if (!in_module) return {};
    in_module = false;

    char path[96];
    snprintf(path, sizeof(path), "%s/%016llX.bin", dir, (unsigned long long)mod_start);
    FILE* f = fopen(path, "wb");
    if (!f) {
      char msg[128];
      snprintf(msg, sizeof(msg), "  fopen failed: %s\n", path);
      return msg;
    }

    u64 pos = mod_start;
    while (pos < mod_end) {
      size_t n = static_cast<size_t>(
          (mod_end - pos < kChunk) ? (mod_end - pos) : kChunk);
      Result rc = svcReadDebugProcessMemory(buf.data(), debug, pos, n);
      if (R_FAILED(rc)) memset(buf.data(), 0, n);
      fwrite(buf.data(), 1, n, f);
      pos += n;
    }
    fclose(f);

    char msg[128];
    snprintf(msg, sizeof(msg), "  [%016llX] %.2f MB -> %s\n",
             (unsigned long long)mod_start,
             (double)(mod_end - mod_start) / (1024.0 * 1024.0),
             path);
    mod_count++;
    return msg;
  };

  while (true) {
    MemoryInfo mi{};
    u32 pageinfo = 0;
    Result rc = svcQueryDebugProcessMemory(&mi, &pageinfo, debug, cursor);
    if (R_FAILED(rc)) break;

    if (IsCodeType(mi.type)) {
      if (!in_module) {
        mod_start = mi.addr;
        mod_end   = mi.addr + mi.size;
        in_module = true;
      } else if (mi.addr == mod_end) {
        mod_end = mi.addr + mi.size;  // extend current module
      } else {
        out += flush_module();          // gap — new module
        mod_start = mi.addr;
        mod_end   = mi.addr + mi.size;
        in_module = true;
      }
    } else {
      out += flush_module();
    }

    u64 next = mi.addr + mi.size;
    if (next <= cursor || next == 0) break;
    cursor = next;
  }
  out += flush_module();  // flush last module if any

  if (owned) {
    svcContinueDebugEvent(debug, 5, nullptr, 0);
    svcCloseHandle(debug);
  }

  mutexUnlock(&g_debug_mutex);

  if (mod_count == 0)
    return {"No code regions found for PID " + std::to_string(pid), true};

  snprintf(line, sizeof(line), "\n%d module(s) dumped.", mod_count);
  out += line;
  return {out, false};
}

// ---- DebugFillMemoryTool ------------------------------------------------------

std::string DebugFillMemoryTool::Name() const { return "debug_fill_memory"; }

std::string DebugFillMemoryTool::Description() const {
  return "Fills a range of memory in the attached process with a repeated byte pattern. "
         "pattern is a hex string of 1–64 bytes that tiles across the region (e.g. 'DEADBEEF'). "
         "Maximum 1 MB per call. Useful for poisoning freed memory or zeroing a buffer.";
}

nlohmann::json DebugFillMemoryTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"address", {{"type","string"},{"description","Start address (hex)"}}},
      {"size",    {{"type","string"},{"description","Number of bytes to fill (decimal or hex, max 1048576)"}}},
      {"pattern", {{"type","string"},{"description","Bytes to repeat as hex string (e.g. 'DEADBEEF' or '00')"}}}
    }},
    {"required", {"address", "size", "pattern"}}
  };
}

ToolResult DebugFillMemoryTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("address") || !arguments["address"].is_string() ||
      !arguments.contains("size")    || !arguments["size"].is_string()    ||
      !arguments.contains("pattern") || !arguments["pattern"].is_string())
    return {"address, size, and pattern required (strings)", true};

  u64 addr = ParseU64(arguments["address"].get<std::string>());
  u64 size = ParseU64(arguments["size"].get<std::string>());

  if (size == 0 || size > 0x100000)
    return {"size must be 1–1048576 (1 MB)", true};

  std::vector<u8> pattern;
  if (!ParseHexBytes(arguments["pattern"].get<std::string>(), pattern) || pattern.empty())
    return {"pattern must be a non-empty hex string", true};
  if (pattern.size() > 64)
    return {"pattern must be 64 bytes or fewer", true};

  mutexLock(&g_debug_mutex);

  if (g_debug_handle == INVALID_HANDLE) {
    mutexUnlock(&g_debug_mutex);
    return {"No active debug session.", true};
  }

  static constexpr size_t kChunk = 0x10000;  // 64 KB
  std::vector<u8> buf(kChunk);

  // Pre-fill the chunk buffer by tiling the pattern.
  for (size_t i = 0; i < kChunk; i++)
    buf[i] = pattern[i % pattern.size()];

  u64 written = 0;
  while (written < size) {
    size_t n = static_cast<size_t>(
        (size - written < kChunk) ? (size - written) : kChunk);

    // Fix up the tail if the chunk doesn't end on a pattern boundary.
    // (The pre-filled buffer already tiles correctly; no per-chunk fixup needed.)

    Result rc = svcWriteDebugProcessMemory(g_debug_handle, buf.data(), addr + written, n);
    if (R_FAILED(rc)) {
      mutexUnlock(&g_debug_mutex);
      char msg[96];
      snprintf(msg, sizeof(msg), "Write failed at %s after %llu bytes: %s",
               Hex64(addr + written).c_str(), (unsigned long long)written, FmtRc(rc).c_str());
      return {msg, true};
    }

    written += n;
  }

  mutexUnlock(&g_debug_mutex);

  char msg[128];
  snprintf(msg, sizeof(msg), "Filled %llu bytes at %s with pattern %s",
           (unsigned long long)size, Hex64(addr).c_str(),
           arguments["pattern"].get<std::string>().c_str());
  return {msg, false};
}

// ---- DebugBacktraceTool -------------------------------------------------------

std::string DebugBacktraceTool::Name() const { return "debug_backtrace"; }

std::string DebugBacktraceTool::Description() const {
  return "Walks the ARM64 call stack by following the frame pointer (x29/fp) chain. "
         "Returns each frame's PC/return address with its offset from the containing code region — "
         "load the module binary into Ghidra/IDA at the reported region base to resolve symbols. "
         "The process must be paused. If tid is omitted, the thread from the last exception event "
         "is used, falling back to the first thread.";
}

nlohmann::json DebugBacktraceTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"tid",   {{"type","string"},{"description","Thread ID (hex or decimal). Omit to auto-select from the last exception event."}}},
      {"limit", {{"type","string"},{"description","Maximum frames to walk (default 64, max 256)"}}}
    }},
    {"required", nlohmann::json::array()}
  };
}

ToolResult DebugBacktraceTool::Execute(const nlohmann::json& arguments) {
  u64 limit = 64;
  if (arguments.contains("limit") && arguments["limit"].is_string()) {
    limit = ParseU64(arguments["limit"].get<std::string>());
    if (limit == 0 || limit > 256) limit = 64;
  }

  mutexLock(&g_debug_mutex);

  if (g_debug_handle == INVALID_HANDLE) {
    mutexUnlock(&g_debug_mutex);
    return {"No active debug session.", true};
  }

  // Determine which thread to trace.
  u64 tid = 0;
  if (arguments.contains("tid") && arguments["tid"].is_string()) {
    tid = ParseU64(arguments["tid"].get<std::string>());
  } else {
    for (const auto& ev : g_saved_events) {
      if (ev.type == DebugEventType_Exception) {
        tid = ev.thread_id;
        break;
      }
    }
    if (tid == 0) {
      static u64 tids[256];
      s32 count = 0;
      if (R_SUCCEEDED(svcGetThreadList(&count, tids, 256, g_debug_handle)) && count > 0)
        tid = tids[0];
    }
    if (tid == 0) {
      mutexUnlock(&g_debug_mutex);
      return {"Could not determine thread ID. Pass tid explicitly.", true};
    }
  }

  ThreadContext ctx{};
  Result rc = svcGetDebugThreadContext(&ctx, g_debug_handle, tid, RegisterGroup_CpuAll);
  if (R_FAILED(rc)) {
    mutexUnlock(&g_debug_mutex);
    return {"svcGetDebugThreadContext failed: " + FmtRc(rc), true};
  }

  // Resolve an address to "0x... [region_base+offset type]".
  auto resolve = [&](u64 addr) -> std::string {
    MemoryInfo mi{};
    u32 pageinfo = 0;
    char buf[96];
    if (R_SUCCEEDED(svcQueryDebugProcessMemory(&mi, &pageinfo, g_debug_handle, addr)) &&
        mi.type != MemType_Unmapped) {
      snprintf(buf, sizeof(buf), "%s  [%s+0x%llX %s]",
               Hex64(addr).c_str(),
               Hex64(mi.addr).c_str(),
               (unsigned long long)(addr - mi.addr),
               MemTypeName(mi.type));
    } else {
      snprintf(buf, sizeof(buf), "%s  [??]", Hex64(addr).c_str());
    }
    return buf;
  };

  std::string out;
  char buf[160];
  snprintf(buf, sizeof(buf), "Backtrace for thread 0x%llX  fp=%s  sp=%s\n",
           (unsigned long long)tid, Hex64(ctx.fp).c_str(), Hex64(ctx.sp).c_str());
  out += buf;

  snprintf(buf, sizeof(buf), "#%-3u pc  %s\n", 0, resolve(ctx.pc.x).c_str());
  out += buf;

  u64 fp = ctx.fp;
  u64 seen_fps[256]{};
  u32 frame = 1;

  while (fp != 0 && frame <= limit) {
    if (fp & 0xF) {
      out += "(misaligned frame pointer — stopping)\n";
      break;
    }

    // Cycle detection.
    bool cycle = false;
    for (u32 i = 0; i < frame; i++) {
      if (seen_fps[i] == fp) { cycle = true; break; }
    }
    if (cycle) { out += "(frame pointer cycle — stopping)\n"; break; }
    seen_fps[frame < 256 ? frame : 255] = fp;

    u8 raw[16];
    rc = svcReadDebugProcessMemory(raw, g_debug_handle, fp, 16);
    if (R_FAILED(rc)) {
      snprintf(buf, sizeof(buf), "(unreadable fp %s — stopping)\n", Hex64(fp).c_str());
      out += buf;
      break;
    }

    u64 prev_fp, saved_lr;
    memcpy(&prev_fp,  raw,      sizeof(u64));
    memcpy(&saved_lr, raw + 8,  sizeof(u64));

    snprintf(buf, sizeof(buf), "#%-3u lr  %s\n", frame, resolve(saved_lr).c_str());
    out += buf;

    fp = prev_fp;
    frame++;
  }

  if (frame > limit)
    out += "(limit reached)\n";
  else if (fp == 0)
    out += "(end of stack)\n";

  mutexUnlock(&g_debug_mutex);
  return {out, false};
}


// ---- DebugDumpRegionTool -------------------------------------------------------

std::string DebugDumpRegionTool::Name() const { return "debug_dump_region"; }

std::string DebugDumpRegionTool::Description() const {
  return "Dumps an arbitrary memory region of a process to the SD card. "
         "Useful for heap snapshots or capturing any mapped region. "
         "Does not require an existing debug session — opens a temporary one and resumes when done. "
         "Unreadable subregions are zero-filled so the file offset always matches the address. "
         "Output path defaults to sdmc:/dumps/<pid>/<address>.bin. "
         "Returns the path written and the number of bytes dumped.";
}

nlohmann::json DebugDumpRegionTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"pid",     {{"type","string"},{"description","Target process ID (decimal or hex)"}}},
      {"address", {{"type","string"},{"description","Start address of the region to dump (hex)"}}},
      {"size",    {{"type","string"},{"description","Number of bytes to dump (decimal or hex, max 256 MB)"}}},
      {"path",    {{"type","string"},{"description","Optional output path on SD card (default: sdmc:/dumps/<pid>/<address>.bin)"}}}
    }},
    {"required", {"pid", "address", "size"}}
  };
}

ToolResult DebugDumpRegionTool::Execute(const nlohmann::json& arguments) {
  if (!arguments.contains("pid")     || !arguments["pid"].is_string()     ||
      !arguments.contains("address") || !arguments["address"].is_string() ||
      !arguments.contains("size")    || !arguments["size"].is_string())
    return {"pid, address, and size required (strings)", true};

  u64 pid  = ParseU64(arguments["pid"].get<std::string>());
  u64 addr = ParseU64(arguments["address"].get<std::string>());
  u64 size = ParseU64(arguments["size"].get<std::string>());

  if (size == 0 || size > 0x10000000ULL)
    return {"size must be 1–268435456 (256 MB)", true};

  mutexLock(&g_debug_mutex);

  Handle debug = INVALID_HANDLE;
  bool owned = false;

  if (g_debug_pid == pid && g_debug_handle != INVALID_HANDLE) {
    debug = g_debug_handle;
  } else {
    Result rc = svcDebugActiveProcess(&debug, pid);
    if (R_FAILED(rc)) {
      mutexUnlock(&g_debug_mutex);
      return {"svcDebugActiveProcess failed: " + FmtRc(rc), true};
    }
    owned = true;
    DebugEventInfo ev;
    while (R_SUCCEEDED(svcGetDebugEvent(&ev, debug))) {}
  }

  std::string out_path;
  if (arguments.contains("path") && arguments["path"].is_string()) {
    out_path = arguments["path"].get<std::string>();
  } else {
    mkdir("sdmc:/dumps", 0777);
    char dir[64];
    snprintf(dir, sizeof(dir), "sdmc:/dumps/%llu", (unsigned long long)pid);
    mkdir(dir, 0777);
    char path_buf[96];
    snprintf(path_buf, sizeof(path_buf), "%s/%016llX.bin", dir, (unsigned long long)addr);
    out_path = path_buf;
  }

  FILE* f = fopen(out_path.c_str(), "wb");
  if (!f) {
    if (owned) { svcContinueDebugEvent(debug, 5, nullptr, 0); svcCloseHandle(debug); }
    mutexUnlock(&g_debug_mutex);
    return {"fopen failed: " + out_path, true};
  }

  static constexpr size_t kChunk = 0x10000;  // 64 KB
  std::vector<u8> buf(kChunk);

  u64 dumped = 0;
  bool had_gap = false;
  while (dumped < size) {
    size_t n = static_cast<size_t>(
        (size - dumped < kChunk) ? (size - dumped) : kChunk);
    Result rc = svcReadDebugProcessMemory(buf.data(), debug, addr + dumped, n);
    if (R_FAILED(rc)) {
      memset(buf.data(), 0, n);
      had_gap = true;
    }
    fwrite(buf.data(), 1, n, f);
    dumped += n;
  }
  fclose(f);

  if (owned) {
    svcContinueDebugEvent(debug, 5, nullptr, 0);
    svcCloseHandle(debug);
  }

  mutexUnlock(&g_debug_mutex);

  char msg[256];
  snprintf(msg, sizeof(msg), "Dumped %llu bytes%s to %s",
           (unsigned long long)dumped,
           had_gap ? " (unreadable subregions zero-filled)" : "",
           out_path.c_str());
  return {msg, false};
}


// ---- scan helpers (shared by DebugValueScanTool) -----------------------------

static void U64ToLE(u64 val, u32 size, u8* out) {
  for (u32 i = 0; i < size; i++) out[i] = (u8)(val >> (8 * i));
}

// Build the candidate list from scratch, scanning all readable memory (or a
// sub-range when start/scan_size are non-zero). Nodes are prepended so the
// list ends up in reverse address order — that's fine; order doesn't matter.
// alignment > 1: only check naturally-aligned addresses (int/float/double).
// alignment == 1: check every byte; overlaps chunk boundaries to catch patterns
//                 that straddle the 64 KB read boundary.
static std::string DoStartScan(const u8* pattern, u32 size, u32 alignment,
                                u64 range_start, u64 range_size) {
  ClearScanList();

  static constexpr size_t kChunk = 0x10000;  // 64 KB
  std::vector<u8> buf(kChunk);

  bool has_range = (range_size > 0);
  u64 cursor     = has_range ? range_start : 0;
  u64 scan_end   = has_range ? (range_start + range_size) : UINT64_MAX;
  bool capped    = false;

  while (cursor < scan_end) {
    MemoryInfo mi{};
    u32 pageinfo = 0;
    if (R_FAILED(svcQueryDebugProcessMemory(&mi, &pageinfo, g_debug_handle, cursor)))
      break;

    bool readable = (mi.perm & Perm_R) && (mi.type != MemType_Unmapped);
    if (readable) {
      u64 region_end = mi.addr + mi.size;
      u64 pos = mi.addr;
      if (has_range && pos < range_start) pos = range_start;
      if (alignment > 1)
        pos = (pos + alignment - 1) & ~(u64)(alignment - 1);
      u64 end = (region_end < scan_end) ? region_end : scan_end;
      size_t step = alignment > 1 ? (size_t)alignment : 1;

      while (pos + size <= end && !capped) {
        u64 bytes_left = end - pos;
        size_t read_sz = (bytes_left < kChunk) ? (size_t)bytes_left : kChunk;
        if (alignment > 1) {
          read_sz = (read_sz / alignment) * alignment;
          if (read_sz == 0) break;
        }

        if (R_FAILED(svcReadDebugProcessMemory(buf.data(), g_debug_handle, pos, read_sz))) {
          pos += read_sz ? read_sz : alignment;
          continue;
        }

        for (size_t i = 0; i + size <= read_sz; i += step) {
          if (memcmp(buf.data() + i, pattern, size) == 0) {
            if (g_scan_candidates.size() >= kMaxScanCandidates) { capped = true; break; }
            g_scan_candidates.push_back(pos + i);
          }
        }

        if (alignment > 1) {
          pos += read_sz;
        } else {
          // Overlap by (size-1) so patterns straddling a chunk boundary are found.
          size_t advance = (read_sz > size - 1) ? (read_sz - (size - 1)) : read_sz;
          pos += advance;
        }
      }
    }

    u64 next = mi.addr + mi.size;
    if (next <= cursor || next == 0) break;
    cursor = next;
  }

  char out[128];
  snprintf(out, sizeof(out), "Start scan: %zu candidate(s)%s",
           g_scan_candidates.size(), capped ? " (capped — refine with a range)" : "");
  return out;
}

// Walk the existing candidate list, read each address, drop non-matching nodes.
static std::string DoFilterScan(const u8* pattern, u32 size) {
  if (g_scan_candidates.empty())
    return "No candidates — run a start scan first";

  u8 buf[256]{};
  size_t before = g_scan_candidates.size();

  auto end = std::remove_if(g_scan_candidates.begin(), g_scan_candidates.end(),
    [&](u64 addr) {
      return R_FAILED(svcReadDebugProcessMemory(buf, g_debug_handle, addr, size))
             || memcmp(buf, pattern, size) != 0;
    });
  g_scan_candidates.erase(end, g_scan_candidates.end());

  char out[128];
  snprintf(out, sizeof(out), "Filter scan: %zu candidate(s) remaining (%zu removed)",
           g_scan_candidates.size(), before - g_scan_candidates.size());
  return out;
}

// Format up to max_show addresses from the list.
static std::string FormatScanList(size_t max_show) {
  if (g_scan_candidates.empty()) return "  (none)\n";
  std::string out;
  char buf[64];
  size_t shown = 0;
  for (u64 addr : g_scan_candidates) {
    if (shown >= max_show) break;
    out += "  ";
    out += Hex64(addr);
    out += "\n";
    shown++;
  }
  if (shown < g_scan_candidates.size()) {
    snprintf(buf, sizeof(buf), "  ... and %zu more\n", g_scan_candidates.size() - shown);
    out += buf;
  }
  return out;
}

// ---- DebugValueScanTool -------------------------------------------------------

std::string DebugValueScanTool::Name() const { return "debug_value_scan"; }

std::string DebugValueScanTool::Description() const {
  return "Cheat-engine style value scanner. "
         "mode='start': scans all readable memory (or a restricted range) for the given value "
         "and stores matching addresses in an on-device linked list. "
         "mode='filter': re-reads every stored candidate and discards those that no longer "
         "match — call repeatedly as the target changes to converge on one address. "
         "type: 'int' (default), 'float', 'double', 'bytes', or 'string'. "
         "int — pass value (decimal or hex) and size (1/2/4/8, default 4), little-endian. "
         "float/double — pass value as a decimal string (e.g. '3.14'). "
         "bytes — pass pattern as a hex string (e.g. 'DEADBEEF'), up to 256 bytes. "
         "string — pass string as UTF-8 text, up to 256 bytes. "
         "Candidates are cleared automatically on debug_attach/debug_detach.";
}

nlohmann::json DebugValueScanTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"type",    {{"type","string"},{"description","'int' (default), 'float', 'double', 'bytes', or 'string'"}}},
      {"value",   {{"type","string"},{"description","For int: decimal or hex integer. For float/double: decimal string (e.g. '3.14')."}}},
      {"pattern", {{"type","string"},{"description","For bytes type: hex string to search for (e.g. 'DEADBEEF')."}}},
      {"string",  {{"type","string"},{"description","For string type: UTF-8 text to search for."}}},
      {"size",    {{"type","string"},{"description","For int type — bytes per value: 1, 2, 4, or 8 (default 4)."}}},
      {"mode",    {{"type","string"},{"description","'start' (fresh scan, default) or 'filter' (narrow existing candidates)."}}},
      {"start",   {{"type","string"},{"description","Optional start address to restrict the initial scan range (hex)."}}},
      {"scan_size", {{"type","string"},{"description","Optional byte count to restrict the initial scan range."}}},
      {"alignment", {{"type","string"},{"description","Only match at addresses that are a multiple of this value: 1 (any), 2, 4, or 8. Defaults to the type size for int/float/double, 1 for bytes/string."}}}
    }},
    {"required", nlohmann::json::array()}
  };
}

ToolResult DebugValueScanTool::Execute(const nlohmann::json& arguments) {
  std::string type_str = "int";
  if (arguments.contains("type") && arguments["type"].is_string())
    type_str = arguments["type"].get<std::string>();

  u8  pat[256]{};
  u32 pat_size  = 0;
  u32 alignment = 1;

  if (type_str == "int") {
    if (!arguments.contains("value") || !arguments["value"].is_string())
      return {"value required for int type (decimal or hex)", true};
    u32 size = 4;
    if (arguments.contains("size") && arguments["size"].is_string()) {
      size = (u32)ParseU64(arguments["size"].get<std::string>());
      if (size != 1 && size != 2 && size != 4 && size != 8)
        return {"size must be 1, 2, 4, or 8", true};
    }
    U64ToLE(ParseU64(arguments["value"].get<std::string>()), size, pat);
    pat_size  = size;
    alignment = size;
  } else if (type_str == "float") {
    if (!arguments.contains("value") || !arguments["value"].is_string())
      return {"value required for float type (e.g. '3.14')", true};
    float f = strtof(arguments["value"].get<std::string>().c_str(), nullptr);
    memcpy(pat, &f, 4);
    pat_size  = 4;
    alignment = 4;
  } else if (type_str == "double") {
    if (!arguments.contains("value") || !arguments["value"].is_string())
      return {"value required for double type (e.g. '3.14159')", true};
    double d = strtod(arguments["value"].get<std::string>().c_str(), nullptr);
    memcpy(pat, &d, 8);
    pat_size  = 8;
    alignment = 8;
  } else if (type_str == "bytes") {
    if (!arguments.contains("pattern") || !arguments["pattern"].is_string())
      return {"pattern required for bytes type (hex string, e.g. 'DEADBEEF')", true};
    std::vector<u8> tmp;
    if (!ParseHexBytes(arguments["pattern"].get<std::string>(), tmp) || tmp.empty())
      return {"pattern must be a non-empty hex string", true};
    if (tmp.size() > 256)
      return {"pattern must be 256 bytes or fewer", true};
    memcpy(pat, tmp.data(), tmp.size());
    pat_size  = (u32)tmp.size();
    alignment = 1;
  } else if (type_str == "string") {
    if (!arguments.contains("string") || !arguments["string"].is_string())
      return {"string required for string type", true};
    const std::string& s = arguments["string"].get<std::string>();
    if (s.empty()) return {"string must not be empty", true};
    if (s.size() > 256) return {"string must be 256 bytes or fewer", true};
    memcpy(pat, s.data(), s.size());
    pat_size  = (u32)s.size();
    alignment = 1;
  } else {
    return {"type must be 'int', 'float', 'double', 'bytes', or 'string'", true};
  }

  std::string mode = "start";
  if (arguments.contains("mode") && arguments["mode"].is_string())
    mode = arguments["mode"].get<std::string>();
  if (mode != "start" && mode != "filter")
    return {"mode must be 'start' or 'filter'", true};

  if (arguments.contains("alignment") && arguments["alignment"].is_string()) {
    u32 user_align = (u32)ParseU64(arguments["alignment"].get<std::string>());
    if (user_align != 1 && user_align != 2 && user_align != 4 && user_align != 8)
      return {"alignment must be 1, 2, 4, or 8", true};
    alignment = user_align;
  }

  u64 range_start = 0, range_size = 0;
  if (arguments.contains("start") && arguments["start"].is_string())
    range_start = ParseU64(arguments["start"].get<std::string>());
  if (arguments.contains("scan_size") && arguments["scan_size"].is_string())
    range_size = ParseU64(arguments["scan_size"].get<std::string>());

  mutexLock(&g_debug_mutex);

  if (g_debug_handle == INVALID_HANDLE) {
    mutexUnlock(&g_debug_mutex);
    return {"No active debug session.", true};
  }

  std::string summary = (mode == "start")
      ? DoStartScan(pat, pat_size, alignment, range_start, range_size)
      : DoFilterScan(pat, pat_size);

  std::string out = summary + "\n";
  if (g_scan_candidates.size() <= 50)
    out += FormatScanList(50);
  else
    out += FormatScanList(20) + "(use debug_scan_clear to reset)\n";

  mutexUnlock(&g_debug_mutex);
  return {out, false};
}

// ---- DebugScanClearTool -------------------------------------------------------

std::string DebugScanClearTool::Name() const { return "debug_scan_clear"; }

std::string DebugScanClearTool::Description() const {
  return "Discards all candidates stored by debug_value_scan. "
         "Call this to start a fresh scan workflow.";
}

nlohmann::json DebugScanClearTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", nlohmann::json::object()},
    {"required", nlohmann::json::array()}
  };
}

ToolResult DebugScanClearTool::Execute(const nlohmann::json&) {
  mutexLock(&g_debug_mutex);
  size_t count = g_scan_candidates.size();
  ClearScanList();
  mutexUnlock(&g_debug_mutex);
  char buf[64];
  snprintf(buf, sizeof(buf), "Cleared %zu candidate(s).", count);
  return {buf, false};
}

}  // namespace tools
