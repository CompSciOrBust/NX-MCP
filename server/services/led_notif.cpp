#include "led_notif.hpp"
#include <switch.h>
#include <atomic>

namespace led {

// 500ms minimum on-time so fast tools produce a visible flash.
static constexpr u64 kMinOnNs = 500'000'000ULL;

static std::atomic<int> g_active_tools{0};
static std::atomic<u64> g_led_off_at{0};  // tick timestamp; 0 = no pending off

static Thread g_thread;
static volatile bool g_thread_running = false;

static constexpr HidNpadIdType kNpadTypes[] = {
  HidNpadIdType_Handheld,
  HidNpadIdType_No1, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4,
  HidNpadIdType_No5, HidNpadIdType_No6, HidNpadIdType_No7,
};

static void ApplyLed(bool on) {
  HidsysNotificationLedPattern pattern = {};
  if (on) {
    pattern.baseMiniCycleDuration           = 0x0F;
    pattern.startIntensity                  = 0x0F;
    pattern.miniCycles[0].ledIntensity      = 0x0F;
    pattern.miniCycles[0].transitionSteps   = 0x0F;
    pattern.miniCycles[0].finalStepDuration = 0x0F;
  }

  for (auto npadType : kNpadTypes) {
    HidsysUniquePadId pads[8];
    s32 total = 0;
    if (R_FAILED(hidsysGetUniquePadsFromNpad(npadType, pads, 8, &total)))
      continue;
    for (s32 i = 0; i < total; i++)
      hidsysSetNotificationLedPattern(&pattern, pads[i]);
  }
}

void LedOn()  { ApplyLed(true);  }
void LedOff() { ApplyLed(false); }

static void LedThreadMain(void*) {
  while (g_thread_running) {
    u64 off_at = g_led_off_at.load();
    if (off_at != 0 && g_active_tools.load() == 0) {
      if (armGetSystemTick() >= off_at) {
        // Re-check count hasn't risen since the load above.
        if (g_active_tools.load() == 0) {
          LedOff();
          g_led_off_at.store(0);
        }
      }
    }
    svcSleepThread(10'000'000LL);  // poll every 10ms
  }
  threadExit();
}

void Initialize() {
  g_thread_running = true;
  threadCreate(&g_thread, LedThreadMain, nullptr, nullptr, 0x4000, 0x26, 3);
  threadStart(&g_thread);
}

void Exit() {
  g_thread_running = false;
  threadWaitForExit(&g_thread);
  threadClose(&g_thread);
  LedOff();
}

LedGuard::LedGuard() {
  if (g_active_tools.fetch_add(1) == 0) {
    g_led_off_at.store(0);  // cancel any pending off
    LedOn();
  }
}

LedGuard::~LedGuard() {
  if (g_active_tools.fetch_sub(1) == 1) {
    // Schedule off after minimum on-time; extend if already further out.
    u64 desired = armGetSystemTick() + armNsToTicks(kMinOnNs);
    u64 current = g_led_off_at.load();
    while (desired > current &&
           !g_led_off_at.compare_exchange_weak(current, desired))
      ;
  }
}

}  // namespace led
