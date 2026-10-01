#pragma once

#include <stddef.h>
#include <stdint.h>

namespace calendar {

constexpr size_t kEventIdLength = 64;
constexpr size_t kEventTitleLength = 80;
constexpr size_t kEventSourceLength = 24;
constexpr size_t kEventLocationLength = 48;
constexpr size_t kMaxEvents = 16;
constexpr int64_t kSecondsPerDay = 86400;

struct CalendarDate {
  int year;
  unsigned month;
  unsigned day;
};

struct CalendarEvent {
  char id[kEventIdLength];
  char title[kEventTitleLength];
  int64_t startEpoch;
  int64_t endEpoch;
  bool allDay;
  CalendarDate allDayStartDate;
  CalendarDate allDayEndDate;
  char source[kEventSourceLength];
  char location[kEventLocationLength];
};

struct CalendarRange {
  int64_t startEpoch;
  int64_t endEpoch;
  CalendarDate startDate;
  CalendarDate endDate;
};

enum class DisplayStatus : uint8_t {
  Calendar,
  WifiSetupRequired,
  WifiConnectionFailed,
  TimeSyncFailed,
  CalendarFeedSetupRequired,
  CalendarFeedFailed,
};

class CalendarProvider {
public:
  virtual ~CalendarProvider() = default;
  virtual size_t loadEvents(CalendarEvent *events, size_t capacity) const = 0;
};

int64_t calendarEpoch(CalendarDate date, unsigned hour = 0, unsigned minute = 0);
int64_t localDateTimeEpoch(CalendarDate date, unsigned hour = 0, unsigned minute = 0);
CalendarDate calendarDateFromEpoch(int64_t epoch);
unsigned calendarWeekday(CalendarDate date);
bool localTimeFromEpoch(int64_t epoch, CalendarDate &date, unsigned &hour, unsigned &minute);
CalendarDate calendarDateAddDays(CalendarDate date, int days);
CalendarRange calendarTodayRange(CalendarDate date);
CalendarRange calendarRestOfWeekRange(CalendarDate date);
size_t selectEventsForRange(const CalendarEvent *events, size_t eventCount, CalendarRange range,
                            CalendarEvent *selected, size_t capacity);
void formatEventTime(const CalendarEvent &event, int64_t dayStartEpoch, char *output, size_t outputSize);
void truncateTitle(const char *title, char *output, size_t outputSize, size_t maxCharacters);
uint32_t displayStateHash(DisplayStatus status, CalendarDate date, const CalendarEvent *events, size_t eventCount,
                          int16_t batteryTenthsVolts);

} // namespace calendar
