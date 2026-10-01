#pragma once

#include <stddef.h>
#include <stdint.h>

namespace calendar {

constexpr char kTimezoneRule[] = "GMT0BST,M3.5.0/1,M10.5.0/2";
constexpr char kNtpServerPrimary[] = "time.google.com";
constexpr char kNtpServerSecondary[] = "pool.ntp.org";
constexpr uint32_t kNtpSyncTimeoutMs = 30000;
constexpr uint32_t kWifiConnectionTimeoutMs = 15000;
constexpr uint32_t kCalendarFeedTimeoutMs = 20000;
constexpr uint32_t AUTO_REFRESH_INTERVAL_SECONDS = 12U * 60U * 60U;
constexpr size_t kMaximumCalendarFeedBytes = 48 * 1024;

} // namespace calendar
