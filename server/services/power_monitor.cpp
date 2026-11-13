#include "power_monitor.hpp"
#include <switch.h>
#include <utility>

namespace power {

// Arbitrary PM module id that no Nintendo module uses ("MC").
static constexpr PscPmModuleId kModuleId = static_cast<PscPmModuleId>(0x4D43);

// Depend on a module that always exists, as other working sysmodules do;
// referencing modules that might not be registered can break PSC's dispatch.
static constexpr u32 kDependencies[] = {PscPmModuleId_Fs};

static constexpr size_t kThreadStackSize = 0x4000;
static constexpr int    kThreadPriority  = 0x2C;
static constexpr int    kThreadCore      = 3;

static PscPmModule           g_module;
static Thread                g_thread;
static std::function<void()> g_on_sleep;
static std::function<void()> g_on_wake;

static void MonitorThread(void*) {
  while (true) {
    if (R_FAILED(eventWait(&g_module.event, UINT64_MAX))) continue;

    PscPmState state;
    u32 flags;
    if (R_FAILED(pscPmModuleGetRequest(&g_module, &state, &flags))) continue;

    // libnx's names don't match the order PSC uses them in: going to sleep
    // sends 1, 2 (sleep ready), 3 (essential services sleep ready); waking
    // sends 4, 1 (minimum awake), 0 (fully awake). Stages 3 and 4 run while
    // services such as fs and bsd are suspended, so the callbacks must not
    // touch them; and nothing at all may block here, or the transition never
    // completes and the console can't wake up.
    // Waking for background work during sleep stops at 1, with the network
    // up but the screen off, so resume there already. On the way down, 1
    // arrives while running and on_wake is a no-op.
    switch (state) {
      case PscPmState_ReadySleep:
      case PscPmState_ReadyShutdown:
        g_on_sleep();
        break;
      case PscPmState_ReadyAwaken:
      case PscPmState_Awake:
        g_on_wake();
        break;
      default:
        break;
    }
    pscPmModuleAcknowledge(&g_module, state);
  }
}

bool Start(std::function<void()> on_sleep, std::function<void()> on_wake) {
  g_on_sleep = std::move(on_sleep);
  g_on_wake  = std::move(on_wake);

  if (R_FAILED(pscmInitialize())) return false;
  if (R_FAILED(pscmGetPmModule(&g_module, kModuleId, kDependencies,
                               sizeof(kDependencies) / sizeof(kDependencies[0]),
                               true))) {
    pscmExit();
    return false;
  }

  if (R_FAILED(threadCreate(&g_thread, MonitorThread, nullptr, nullptr,
                            kThreadStackSize, kThreadPriority, kThreadCore)) ||
      R_FAILED(threadStart(&g_thread))) {
    pscPmModuleFinalize(&g_module);
    pscPmModuleClose(&g_module);
    pscmExit();
    return false;
  }
  return true;
}

}  // namespace power
