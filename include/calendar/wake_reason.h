#pragma once

namespace calendar {

enum class WakeReason {
  ColdBootOrReset,
  Timer,
  Key3,
};

inline WakeReason classifyWakeReason(bool timerWake, bool gpioWake) {
  if (gpioWake) {
    return WakeReason::Key3;
  }
  if (timerWake) {
    return WakeReason::Timer;
  }
  return WakeReason::ColdBootOrReset;
}

} // namespace calendar
