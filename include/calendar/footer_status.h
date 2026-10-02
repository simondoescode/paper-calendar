#pragma once
#include <stdint.h>

namespace calendar {
// Values are supplied by lifecycle adapters; drawing never queries hardware.
  struct FooterStatus {
    const char *wifiLabel = "Wi-Fi";
    bool wifiConnected = false;
    const char *lastUpdated = "--:--";
    int todayEventCount = -1; // Negative: derive from current-day calendar events.
    int batteryPercent = -1; // Negative: use voltage or unavailable, never invent charge.
    int16_t batteryTenthsVolts = -1;
    bool batteryCharging = false;
  };
  uint32_t footerDisplayHash(const FooterStatus &status);
} // namespace calendar
