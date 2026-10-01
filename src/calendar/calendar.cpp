#include <calendar/calendar.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32) && defined(CALENDAR_HOST)
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>
#include <stdlib.h>
#endif
namespace calendar {
namespace {

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned shiftedMonth = month > 2 ? month - 3 : month + 9;
  const unsigned dayOfYear = (153 * shiftedMonth + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
}

void copyEvent(CalendarEvent &destination, const CalendarEvent &source) {
  destination = source;
}

int compareDates(CalendarDate left, CalendarDate right) {
  if (left.year != right.year) {
    return left.year < right.year ? -1 : 1;
  }
  if (left.month != right.month) {
    return left.month < right.month ? -1 : 1;
  }
  if (left.day != right.day) {
    return left.day < right.day ? -1 : 1;
  }
  return 0;
}

int64_t eventSortKey(const CalendarEvent &event) {
  return event.allDay ? localDateTimeEpoch(event.allDayStartDate) : event.startEpoch;
}

#if defined(_WIN32) && defined(CALENDAR_HOST)
bool hostUsesUtc() {
  const char *timezone = getenv("TZ");
  return timezone != nullptr && (strcmp(timezone, "UTC0") == 0 || strcmp(timezone, "UTC") == 0);
}

bool londonWindowsTimeZone(DYNAMIC_TIME_ZONE_INFORMATION &timezone) {
  for (DWORD index = 0; EnumDynamicTimeZoneInformation(index, &timezone) == ERROR_SUCCESS; ++index) {
    if (wcscmp(timezone.TimeZoneKeyName, L"GMT Standard Time") == 0) {
      return true;
    }
  }
  return false;
}

bool epochToSystemTime(int64_t epoch, SYSTEMTIME &systemTime) {
  const time_t timestamp = static_cast<time_t>(epoch);
  if (static_cast<int64_t>(timestamp) != epoch) {
    return false;
  }
  struct tm utc = {};
  if (gmtime_s(&utc, &timestamp) != 0) {
    return false;
  }
  systemTime = {};
  systemTime.wYear = static_cast<WORD>(utc.tm_year + 1900);
  systemTime.wMonth = static_cast<WORD>(utc.tm_mon + 1);
  systemTime.wDay = static_cast<WORD>(utc.tm_mday);
  systemTime.wDayOfWeek = static_cast<WORD>(utc.tm_wday);
  systemTime.wHour = static_cast<WORD>(utc.tm_hour);
  systemTime.wMinute = static_cast<WORD>(utc.tm_min);
  systemTime.wSecond = static_cast<WORD>(utc.tm_sec);
  return true;
}

bool sameWallTime(const SYSTEMTIME &left, const SYSTEMTIME &right) {
  return left.wYear == right.wYear && left.wMonth == right.wMonth && left.wDay == right.wDay &&
         left.wHour == right.wHour && left.wMinute == right.wMinute;
}

bool validLondonLocalTime(const DYNAMIC_TIME_ZONE_INFORMATION &timezone, CalendarDate date, unsigned hour,
                          unsigned minute, int64_t candidateEpoch) {
  SYSTEMTIME utc = {};
  SYSTEMTIME local = {};
  SYSTEMTIME expected = {};
  expected.wYear = static_cast<WORD>(date.year);
  expected.wMonth = static_cast<WORD>(date.month);
  expected.wDay = static_cast<WORD>(date.day);
  expected.wHour = static_cast<WORD>(hour);
  expected.wMinute = static_cast<WORD>(minute);
  return epochToSystemTime(candidateEpoch, utc) &&
         SystemTimeToTzSpecificLocalTimeEx(&timezone, &utc, &local) != 0 && sameWallTime(local, expected);
}
#endif
} // namespace

int64_t calendarEpoch(CalendarDate date, unsigned hour, unsigned minute) {
  return daysFromCivil(date.year, date.month, date.day) * kSecondsPerDay +
         static_cast<int64_t>(hour) * 3600 + static_cast<int64_t>(minute) * 60;
}

int64_t localDateTimeEpoch(CalendarDate date, unsigned hour, unsigned minute) {
  if (date.month < 1 || date.month > 12 || date.day < 1 || date.day > 31 || hour > 23 || minute > 59) {
    return -1;
  }
#if defined(_WIN32) && defined(CALENDAR_HOST)
  if (hostUsesUtc()) {
    if (compareDates(calendarDateFromEpoch(calendarEpoch(date)), date) != 0) {
      return -1;
    }
    return calendarEpoch(date, hour, minute);
  }
  DYNAMIC_TIME_ZONE_INFORMATION timezone = {};
  TIME_ZONE_INFORMATION yearlyRules = {};
  if (!londonWindowsTimeZone(timezone) ||
      !GetTimeZoneInformationForYear(static_cast<USHORT>(date.year), &timezone, &yearlyRules)) {
    return -1;
  }
  const int biases[] = {yearlyRules.Bias + yearlyRules.StandardBias,
                        yearlyRules.Bias + yearlyRules.DaylightBias};
  int64_t validCandidates[2] = {-1, -1};
  size_t validCount = 0;
  for (size_t i = 0; i < 2; ++i) {
    const int64_t candidate = calendarEpoch(date, hour, minute) +
                              static_cast<int64_t>(biases[i]) * 60;
    if (validLondonLocalTime(timezone, date, hour, minute, candidate) &&
        (validCount == 0 || validCandidates[0] != candidate)) {
      validCandidates[validCount++] = candidate;
    }
  }
  return validCount == 1 ? validCandidates[0] : -1;
#else
  struct tm local = {};
  local.tm_year = date.year - 1900;
  local.tm_mon = static_cast<int>(date.month) - 1;
  local.tm_mday = static_cast<int>(date.day);
  local.tm_hour = static_cast<int>(hour);
  local.tm_min = static_cast<int>(minute);
  local.tm_isdst = -1;
  const time_t timestamp = mktime(&local);
  if (timestamp == static_cast<time_t>(-1)) {
    return -1;
  }

  CalendarDate normalizedDate = {};
  unsigned normalizedHour = 0;
  unsigned normalizedMinute = 0;
  if (!localTimeFromEpoch(static_cast<int64_t>(timestamp), normalizedDate, normalizedHour, normalizedMinute) ||
      compareDates(normalizedDate, date) != 0 || normalizedHour != hour || normalizedMinute != minute) {
    return -1;
  }

  time_t daylightCandidates[2] = {static_cast<time_t>(-1), static_cast<time_t>(-1)};
  for (int daylight = 0; daylight <= 1; ++daylight) {
    struct tm candidate = {};
    candidate.tm_year = date.year - 1900;
    candidate.tm_mon = static_cast<int>(date.month) - 1;
    candidate.tm_mday = static_cast<int>(date.day);
    candidate.tm_hour = static_cast<int>(hour);
    candidate.tm_min = static_cast<int>(minute);
    candidate.tm_isdst = daylight;
    const time_t candidateEpoch = mktime(&candidate);
    if (candidateEpoch == static_cast<time_t>(-1)) {
      continue;
    }
    CalendarDate candidateDate = {};
    unsigned candidateHour = 0;
    unsigned candidateMinute = 0;
    if (localTimeFromEpoch(static_cast<int64_t>(candidateEpoch), candidateDate, candidateHour, candidateMinute) &&
        compareDates(candidateDate, date) == 0 && candidateHour == hour && candidateMinute == minute) {
      daylightCandidates[daylight] = candidateEpoch;
    }
  }
  if (daylightCandidates[0] != static_cast<time_t>(-1) && daylightCandidates[1] != static_cast<time_t>(-1) &&
      daylightCandidates[0] != daylightCandidates[1]) {
    return -1;
  }
  return static_cast<int64_t>(timestamp);
#endif
}

CalendarDate calendarDateFromEpoch(int64_t epoch) {
  int64_t days = epoch / kSecondsPerDay;
  if (epoch < 0 && epoch % kSecondsPerDay != 0) {
    --days;
  }

  int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned dayOfEra = static_cast<unsigned>(z - era * 146097);
  const unsigned yearOfEra =
    (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  int year = static_cast<int>(yearOfEra) + static_cast<int>(era * 400);
  const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const unsigned monthPrime = (5 * dayOfYear + 2) / 153;
  const unsigned day = dayOfYear - (153 * monthPrime + 2) / 5 + 1;
  const unsigned month = monthPrime < 10 ? monthPrime + 3 : monthPrime - 9;
  year += month <= 2;
  return {year, month, day};
}

unsigned calendarWeekday(CalendarDate date) {
  int64_t weekday = (daysFromCivil(date.year, date.month, date.day) + 4) % 7;
  if (weekday < 0) {
    weekday += 7;
  }
  return static_cast<unsigned>(weekday);
}

bool localTimeFromEpoch(int64_t epoch, CalendarDate &date, unsigned &hour, unsigned &minute) {
#if defined(_WIN32) && defined(CALENDAR_HOST)
  SYSTEMTIME utc = {};
  SYSTEMTIME local = {};
  if (!epochToSystemTime(epoch, utc)) {
    return false;
  }
  if (hostUsesUtc()) {
    local = utc;
  } else {
    DYNAMIC_TIME_ZONE_INFORMATION timezone = {};
    if (!londonWindowsTimeZone(timezone) ||
        !SystemTimeToTzSpecificLocalTimeEx(&timezone, &utc, &local)) {
      return false;
    }
  }
  date = {local.wYear, local.wMonth, local.wDay};
  hour = local.wHour;
  minute = local.wMinute;
  return true;
#else
  const time_t timestamp = static_cast<time_t>(epoch);
  if (static_cast<int64_t>(timestamp) != epoch) {
    return false;
  }

  struct tm localTime = {};
#if defined(_WIN32)
  if (localtime_s(&localTime, &timestamp) != 0) {
    return false;
  }
#else
  if (localtime_r(&timestamp, &localTime) == nullptr) {
    return false;
  }
#endif
  date = {localTime.tm_year + 1900, static_cast<unsigned>(localTime.tm_mon + 1),
          static_cast<unsigned>(localTime.tm_mday)};
  hour = static_cast<unsigned>(localTime.tm_hour);
  minute = static_cast<unsigned>(localTime.tm_min);
  return true;
#endif
}

CalendarDate calendarDateAddDays(CalendarDate date, int days) {
  return calendarDateFromEpoch(calendarEpoch(date) + static_cast<int64_t>(days) * kSecondsPerDay);
}

CalendarRange calendarTodayRange(CalendarDate date) {
  const CalendarDate nextDate = calendarDateAddDays(date, 1);
  return {localDateTimeEpoch(date), localDateTimeEpoch(nextDate), date, nextDate};
}

CalendarRange calendarRestOfWeekRange(CalendarDate date) {
  const unsigned mondayBasedWeekday = (calendarWeekday(date) + 6) % 7;
  const unsigned daysUntilNextMonday = 7 - mondayBasedWeekday;
  const CalendarDate startDate = calendarDateAddDays(date, 1);
  const CalendarDate endDate = calendarDateAddDays(date, static_cast<int>(daysUntilNextMonday));
  return {localDateTimeEpoch(startDate), localDateTimeEpoch(endDate), startDate, endDate};
}

size_t selectEventsForRange(const CalendarEvent *events, size_t eventCount, CalendarRange range,
                            CalendarEvent *selected, size_t capacity) {
  if (events == nullptr || selected == nullptr || capacity == 0 || range.endEpoch <= range.startEpoch) {
    return 0;
  }

  size_t selectedCount = 0;
  for (size_t i = 0; i < eventCount; ++i) {
    const CalendarEvent &event = events[i];
    bool overlaps = false;
    if (event.allDay) {
      overlaps = compareDates(event.allDayEndDate, event.allDayStartDate) > 0 &&
                  compareDates(event.allDayStartDate, range.endDate) < 0 &&
                  compareDates(event.allDayEndDate, range.startDate) > 0;
    } else {
      overlaps = event.endEpoch > event.startEpoch && event.startEpoch < range.endEpoch &&
                 event.endEpoch > range.startEpoch;
    }
    if (!overlaps) {
      continue;
    }

    const int64_t sortKey = eventSortKey(event);
    if (selectedCount == capacity && sortKey >= eventSortKey(selected[selectedCount - 1])) {
      continue;
    }

    size_t insertion = selectedCount < capacity ? selectedCount : selectedCount - 1;
    while (insertion > 0 && eventSortKey(selected[insertion - 1]) > sortKey) {
      if (insertion < capacity) {
        copyEvent(selected[insertion], selected[insertion - 1]);
      }
      --insertion;
    }
    copyEvent(selected[insertion], event);
    if (selectedCount < capacity) {
      ++selectedCount;
    }
  }
  return selectedCount;
}

void formatEventTime(const CalendarEvent &event, int64_t dayStartEpoch, char *output, size_t outputSize) {
  if (output == nullptr || outputSize == 0) {
    return;
  }
  if (event.allDay) {
    snprintf(output, outputSize, "ALL DAY");
    return;
  }
  if (event.startEpoch < dayStartEpoch) {
    snprintf(output, outputSize, "CONT.");
    return;
  }

  CalendarDate localDate = {};
  unsigned hour = 0;
  unsigned minute = 0;
  if (!localTimeFromEpoch(event.startEpoch, localDate, hour, minute)) {
    output[0] = '\0';
    return;
  }
  snprintf(output, outputSize, "%02u:%02u", hour, minute);
}

void truncateTitle(const char *title, char *output, size_t outputSize, size_t maxCharacters) {
  if (output == nullptr || outputSize == 0) {
    return;
  }
  output[0] = '\0';
  if (title == nullptr) {
    return;
  }

  const size_t titleLength = strlen(title);
  const size_t allowed = maxCharacters < outputSize - 1 ? maxCharacters : outputSize - 1;
  if (titleLength <= allowed) {
    memcpy(output, title, titleLength);
    output[titleLength] = '\0';
    return;
  }
  if (allowed <= 3) {
    memcpy(output, title, allowed);
    output[allowed] = '\0';
    return;
  }
  const size_t prefixLength = allowed - 3;
  memcpy(output, title, prefixLength);
  memcpy(output + prefixLength, "...", 3);
  output[allowed] = '\0';
}

uint32_t displayStateHash(DisplayStatus status, CalendarDate date, const CalendarEvent *events, size_t eventCount,
                          int16_t batteryTenthsVolts) {
  constexpr uint32_t kFnvOffsetBasis = 2166136261u;
  constexpr uint32_t kFnvPrime = 16777619u;
  uint32_t hash = kFnvOffsetBasis;
  const auto addByte = [&hash](uint8_t value) {
    hash ^= value;
    hash *= kFnvPrime;
  };
  const auto addInteger = [&addByte](uint64_t value, size_t byteCount) {
    for (size_t i = 0; i < byteCount; ++i) {
      addByte(static_cast<uint8_t>(value & 0xff));
      value >>= 8;
    }
  };
  const auto addText = [&addByte](const char *text, size_t maximumLength) {
    if (text == nullptr) {
      addByte(0);
      return;
    }
    size_t i = 0;
    for (; i < maximumLength && text[i] != '\0'; ++i) {
      addByte(static_cast<uint8_t>(text[i]));
    }
    addByte(0);
  };

  addByte(static_cast<uint8_t>(status));
  if (status != DisplayStatus::Calendar) {
    return hash;
  }

  addInteger(static_cast<uint32_t>(date.year), sizeof(uint32_t));
  addInteger(date.month, sizeof(date.month));
  addInteger(date.day, sizeof(date.day));
  addInteger(static_cast<uint16_t>(batteryTenthsVolts), sizeof(uint16_t));

  const CalendarRange visibleRange = {calendarTodayRange(date).startEpoch, calendarRestOfWeekRange(date).endEpoch,
                                      date, calendarRestOfWeekRange(date).endDate};
  CalendarEvent visibleEvents[kMaxEvents];
  const size_t visibleCount =
    selectEventsForRange(events, eventCount, visibleRange, visibleEvents, kMaxEvents);
  addInteger(visibleCount, sizeof(visibleCount));
  for (size_t i = 0; i < visibleCount; ++i) {
    const CalendarEvent &event = visibleEvents[i];
    addText(event.id, sizeof(event.id));
    addText(event.title, sizeof(event.title));
    addByte(event.allDay ? 1 : 0);
    if (event.allDay) {
      addInteger(static_cast<uint32_t>(event.allDayStartDate.year), sizeof(uint32_t));
      addInteger(event.allDayStartDate.month, sizeof(event.allDayStartDate.month));
      addInteger(event.allDayStartDate.day, sizeof(event.allDayStartDate.day));
      addInteger(static_cast<uint32_t>(event.allDayEndDate.year), sizeof(uint32_t));
      addInteger(event.allDayEndDate.month, sizeof(event.allDayEndDate.month));
      addInteger(event.allDayEndDate.day, sizeof(event.allDayEndDate.day));
    } else {
      addInteger(static_cast<uint64_t>(event.startEpoch), sizeof(event.startEpoch));
      addInteger(static_cast<uint64_t>(event.endEpoch), sizeof(event.endEpoch));
    }
  }
  return hash;
}

} // namespace calendar
