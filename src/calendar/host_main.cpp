#if defined(CALENDAR_HOST)

#include <calendar/calendar_renderer.h>
#include <calendar/host_runtime.h>
#include <calendar/settings.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <memory>

namespace {

calendar::CalendarDate localToday() {
  calendar::CalendarDate today = {};
  unsigned hour = 0;
  unsigned minute = 0;
  if (!calendar::localTimeFromEpoch(static_cast<int64_t>(time(nullptr)), today, hour, minute)) {
    return {};
  }
  return today;
}

bool setHostTimezone(const char *timezone) {
#if defined(_WIN32)
  const char *rule = strcmp(timezone, "UTC") == 0 ? "UTC0" : "GMT0BST,M3.5.0/1,M10.5.0/2";
  if (_putenv_s("TZ", rule) != 0) return false;
  _tzset();
#else
  if (setenv("TZ", timezone, 1) != 0) return false;
  tzset();
#endif
  return true;
}

void runLifecycle(calendar::ConfigStore &store, calendar::HostDisplayTarget &display) {
  calendar::CalendarSettings settings = calendar::defaultCalendarSettings();
  if (!store.load(settings)) {
    fprintf(stderr, "Settings file is invalid; using safe defaults for this refresh.\n");
    settings = calendar::defaultCalendarSettings();
    display.writeStatus(calendar::DisplayStatus::CalendarFeedFailed,
                        "Settings invalid; edit and save in the local portal.");
    return;
  }
  if (!setHostTimezone(settings.timezone)) {
    fprintf(stderr, "Could not apply timezone %s; using Europe/London.\n", settings.timezone);
    setHostTimezone(calendar::kDefaultCalendarTimezone);
    settings = calendar::defaultCalendarSettings();
  }
  const calendar::CalendarDate today = localToday();
  calendar::HostCalendarProvider provider(settings, today);
  calendar::CalendarEvent events[calendar::kMaxEvents] = {};
  const size_t count = provider.loadEvents(events, calendar::kMaxEvents);
  if (provider.error()[0] != '\0') {
    fprintf(stderr, "Calendar provider error: %s\n", provider.error());
    display.writeStatus(calendar::DisplayStatus::CalendarFeedFailed, provider.error());
    return;
  }
  printf("Refresh complete: %u events; next configured interval %lu seconds.\n",
         static_cast<unsigned>(count), static_cast<unsigned long>(settings.refreshIntervalSeconds));
  if (!calendar::renderCalendar(display, events, count, today, "host-dev", "calendar-host", -1)) {
    fprintf(stderr, "Could not write calendar preview.\n");
  }
}

} // namespace

int main() {
  std::unique_ptr<calendar::ConfigStore> store(calendar::createHostConfigStore(".dev/calendar-settings.json"));
  calendar::HostDisplayTarget display(".dev/calendar-preview.png");
  printf("Calendar host emulator\n");
  printf("Portal: http://localhost:8080\n");
  printf("Settings: .dev/calendar-settings.json\n");
  printf("Display preview: .dev/calendar-preview.png\n");
  runLifecycle(*store, display);
  calendar::HostPortal portal(*store, display, [&store, &display]() { runLifecycle(*store, display); });
  return portal.run();
}

#endif
