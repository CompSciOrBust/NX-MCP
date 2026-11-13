#pragma once
#include <functional>

namespace power {

// Registers a PSC power-management module and runs a thread that reports
// sleep/wake transitions. on_sleep runs before the console sleeps or shuts
// down, on_wake once it is awake again. PSC waits for each callback to return
// before it continues the transition, so both must finish promptly and must
// not call into other system services (they may already be asleep).
//
// Returns false if PSC registration failed; the caller then has no sleep
// notifications and must recover from dead sockets on its own.
bool Start(std::function<void()> on_sleep, std::function<void()> on_wake);

}  // namespace power
