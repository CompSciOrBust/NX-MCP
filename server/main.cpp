#include <switch.h>
#include <memory>
#include "app.hpp"
#include "socket_server.hpp"
#include "ipc_service.hpp"
#include "led_notif.hpp"

#define INNER_HEAP_SIZE 0x400000

static constexpr u64 kQlaunchProgramId  = 0x0100000000001000;
static constexpr s64 kBootPollNs        = 500000000LL;
static constexpr s64 kSocketInitRetryNs = 1000000000LL;

#ifdef __cplusplus
extern "C" {
#endif

u32 __nx_applet_type = AppletType_None;

void __libnx_initheap(void) {
  static u8 inner_heap[INNER_HEAP_SIZE];
  extern void* fake_heap_start;
  extern void* fake_heap_end;
  fake_heap_start = inner_heap;
  fake_heap_end   = inner_heap + sizeof(inner_heap);
}

void __appInit(void) {
  Result rc = smInitialize();
  if (R_FAILED(rc))
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

  rc = setsysInitialize();
  if (R_SUCCEEDED(rc)) {
    SetSysFirmwareVersion fw;
    if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
      hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
    setsysExit();
  }

  // bsd is brought up in main() once the system has booted (see
  // WaitForHomeMenu()); hid:sys is opened lazily by led_notif.
  rc = fsInitialize();
  if (R_FAILED(rc))
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

  fsdevMountSdmc();
}

void __appExit(void) {
  led::Exit();
  fsdevUnmountAll();
  fsExit();
  socketExit();
  smExit();
}

#ifdef __cplusplus
}
#endif

// boot2 starts SD sysmodules right after Nintendo's own, while AM and the HOME
// menu are still coming up. Bringing up bsd/hid that early was observed to
// stall the boot sequence by about a minute, so wait for qlaunch first.
// Returns immediately when the module is (re)started later.
static void WaitForHomeMenu() {
  if (R_FAILED(pmdmntInitialize())) return;
  u64 pid = 0;
  while (R_FAILED(pmdmntGetProcessId(&pid, kQlaunchProgramId)))
    svcSleepThread(kBootPollNs);
  pmdmntExit();
}

int main(int, char*[]) {
  ipc::StartIpcThread();
  WaitForHomeMenu();

  while (R_FAILED(socketInitialize(NULL)))
    svcSleepThread(kSocketInitRetryNs);

  led::Initialize();

  http::App app(std::make_unique<http::SocketServer>());
  app.Run(57331);
  return 0;
}
