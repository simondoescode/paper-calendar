#include <Arduino.h>
#include <calendar/calendar_config.h>
#include <calendar/clock_service.h>
#include <esp_sntp.h>
#include <string.h>
#include <time.h>

namespace calendar {
  namespace {

    constexpr int64_t kEarliestValidEpoch = 1704067200; // 2024-01-01 UTC
    volatile bool gTimeSynchronized = false;

    void onTimeSynchronized(struct timeval *timeValue) {
      (void)timeValue;
      gTimeSynchronized = true;
    }

  } // namespace

  bool ClockService::synchronize(const char *timezone) {
    gTimeSynchronized = false;
    sntp_set_time_sync_notification_cb(onTimeSynchronized);
    const char *timezoneRule = timezone != nullptr && strcmp(timezone, "UTC") == 0 ? "UTC0" : kTimezoneRule;
    configTzTime(timezoneRule, kNtpServerPrimary, kNtpServerSecondary);

    const uint32_t startedAt = millis();
    while (!gTimeSynchronized && millis() - startedAt < kNtpSyncTimeoutMs) {
      delay(100);
    }

    int64_t currentEpoch = 0;
    CalendarDate date = {};
    unsigned hour = 0;
    unsigned minute = 0;
    return gTimeSynchronized && now(currentEpoch, date, hour, minute);
  }

  bool ClockService::now(int64_t &epoch, CalendarDate &localDate, unsigned &hour, unsigned &minute) const {
    const time_t current = time(nullptr);
    epoch = static_cast<int64_t>(current);
    if (epoch < kEarliestValidEpoch || static_cast<time_t>(epoch) != current) {
      return false;
    }
    return localTimeFromEpoch(epoch, localDate, hour, minute);
  }

} // namespace calendar
