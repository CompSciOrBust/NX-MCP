#include <switch.h>
#include <memory>
#include "app.hpp"
#include "socket_server.hpp"
#include "ipc_service.hpp"

#define INNER_HEAP_SIZE 0x400000

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

  rc = socketInitialize(NULL);
  if (R_FAILED(rc))
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

  rc = fsInitialize();
  if (R_FAILED(rc))
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));
  fsdevMountSdmc();
}

void __appExit(void) {
  fsdevUnmountAll();
  fsExit();
  socketExit();
  smExit();
}

#ifdef __cplusplus
}
#endif

int main(int, char*[]) {
  ipc::StartIpcThread();
  http::App app(std::make_unique<http::SocketServer>());
  app.Run(57331);
  return 0;
}
