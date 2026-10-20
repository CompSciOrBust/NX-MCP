#pragma once
#include <atomic>

namespace led {

void Initialize();
void Exit();

void LedOn();
void LedOff();

struct LedGuard {
  LedGuard();
  ~LedGuard();
  LedGuard(const LedGuard&) = delete;
  LedGuard& operator=(const LedGuard&) = delete;
};

}  // namespace led
