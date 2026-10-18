#include "ipc_service.hpp"
#include "mcp_versions.hpp"
#include <cstring>
#include <switch.h>

namespace ipc {

// CMIF magic values (SFCI / SFCO in little-endian)
static constexpr u32 kCmifInMagic  = 0x49434653u;
static constexpr u32 kCmifOutMagic = 0x4F434653u;

struct CmifInHeader {
  u32 magic_;
  u32 version_;
  u32 command_id_;
  u32 token_;
};

struct CmifOutHeader {
  u32 magic_;
  u32 version_;
  Result result_;
  u32 token_;
};

// Command IDs
static constexpr u32 kCmdGetVersion = 0;

// CMIF data starts at the first 16-byte-aligned offset after the 8-byte HIPC header.
// With no descriptors: HIPC header = TLS[0..1], padding = TLS[2..3], CMIF data = TLS[4+].
static constexpr u32 kHipcHeaderWords  = 2;
static constexpr u32 kPaddingWords     = 2;
static constexpr u32 kCmifDataOffset   = kHipcHeaderWords + kPaddingWords; // TLS[4]

static void ProcessCommand(u32* tls) {
  auto* in_hdr = reinterpret_cast<CmifInHeader*>(tls + kCmifDataOffset);

  Result rc = MAKERESULT(11, 403); // default: unknown command
  mcp::Version resp = {};

  if (in_hdr->magic_ == kCmifInMagic && in_hdr->command_id_ == kCmdGetVersion) {
    resp = mcp::kServerVersionInfo;
    rc = 0;
  }

  // Build CMIF response:
  //   TLS[0]            = HIPC type 4 (Request/Response), no descriptors
  //   TLS[1]            = num_data_words
  //   TLS[2..3]         = padding (0)
  //   TLS[4..7]         = CmifOutHeader
  //   TLS[8]            = mcp::Version (only present on success)
  constexpr u32 kSuccessDataWords =
    kPaddingWords + sizeof(CmifOutHeader) / 4 + sizeof(mcp::Version) / 4;
  constexpr u32 kErrorDataWords =
    kPaddingWords + sizeof(CmifOutHeader) / 4;

  u32 num_data_words = R_SUCCEEDED(rc) ? kSuccessDataWords : kErrorDataWords;
  size_t clear_bytes = (kHipcHeaderWords + num_data_words) * 4;

  memset(tls, 0, clear_bytes);
  tls[0] = 4; // HIPC message type
  tls[1] = num_data_words;

  auto* out_hdr = reinterpret_cast<CmifOutHeader*>(tls + kCmifDataOffset);
  out_hdr->magic_   = kCmifOutMagic;
  out_hdr->version_ = 1;
  out_hdr->result_  = rc;
  out_hdr->token_   = 0;

  if (R_SUCCEEDED(rc)) {
    memcpy(out_hdr + 1, &resp, sizeof(resp));
  }
}

static constexpr int    kMaxSessions   = 4;
static constexpr size_t kThreadStackSz = 0x4000;

static Thread  g_thread;
alignas(0x1000) static u8 g_stack[kThreadStackSz];

static void IpcThread(void*) {
  Handle port;
  Result rc = smRegisterService(&port, smEncodeName("nx:mcp"), false, kMaxSessions);
  if (R_FAILED(rc)) return;

  Handle sessions[kMaxSessions];
  int    num_sessions = 0;
  Handle reply_target = INVALID_HANDLE;

  while (true) {
    Handle wait_handles[kMaxSessions + 1];
    wait_handles[0] = port;
    for (int i = 0; i < num_sessions; ++i)
      wait_handles[1 + i] = sessions[i];

    s32 idx = 0;
    rc = svcReplyAndReceive(&idx, wait_handles, 1 + num_sessions, reply_target, UINT64_MAX);
    reply_target = INVALID_HANDLE;

    if (R_FAILED(rc)) {
      // A session was closed by the client (0xF601) or other error
      if (idx >= 1 && idx <= num_sessions) {
        svcCloseHandle(sessions[idx - 1]);
        sessions[idx - 1] = sessions[--num_sessions];
      }
      continue;
    }

    if (idx == 0) {
      // New client connecting to the port
      if (num_sessions < kMaxSessions) {
        Handle session;
        svcAcceptSession(&session, port);
        sessions[num_sessions++] = session;
      }
    } else {
      Handle session = sessions[idx - 1];
      u32*   tls     = static_cast<u32*>(armGetTls());

      // Type 2 = close session; no reply needed
      if ((tls[0] & 0xFFFF) == 2) {
        svcCloseHandle(session);
        sessions[idx - 1] = sessions[--num_sessions];
        continue;
      }

      ProcessCommand(tls);
      reply_target = session;
    }
  }
}

void StartIpcThread() {
  threadCreate(&g_thread, IpcThread, nullptr, g_stack, kThreadStackSz, 44, 3);
  threadStart(&g_thread);
}

}  // namespace ipc
