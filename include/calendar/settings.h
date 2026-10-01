#pragma once

#include <stddef.h>
#include <stdint.h>

namespace calendar {

constexpr uint32_t kDefaultRefreshIntervalSeconds = 12U * 60U * 60U;
constexpr uint32_t kMinimumRefreshIntervalSeconds = 60;
constexpr uint32_t kMaximumRefreshIntervalSeconds = 7U * 24U * 60U * 60U;
constexpr char kDefaultCalendarTimezone[] = "Europe/London";
constexpr char kDefaultFixtureUrl[] = "fixture://default";

struct CalendarSettings {
  char calendarUrl[512];
  uint32_t refreshIntervalSeconds;
  char timezone[64];
};

class ConfigStore {
public:
  virtual ~ConfigStore() = default;
  virtual bool load(CalendarSettings &settings) = 0;
  virtual bool save(const CalendarSettings &settings) = 0;
};

CalendarSettings defaultCalendarSettings();
bool validateCalendarSettings(const CalendarSettings &settings, char *error, size_t errorSize);
bool applySettingsForm(const char *body, CalendarSettings &settings, char *error, size_t errorSize);
ConfigStore *createHostConfigStore(const char *path);
ConfigStore *createEspConfigStore();

} // namespace calendar
