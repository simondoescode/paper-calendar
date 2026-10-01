#include <calendar/calendar_renderer.h>

#include <stdio.h>
#include <string.h>

namespace calendar {
namespace {

constexpr uint16_t kDisplayWidth = 800;
constexpr uint16_t kDisplayHeight = 480;
constexpr uint16_t kLeftColumnWidth = 560;
constexpr uint16_t kRightColumnWidth = kDisplayWidth - kLeftColumnWidth;
constexpr uint16_t kHeaderHeight = 112;
constexpr uint16_t kOuterPadding = 16;
constexpr uint16_t kCardGap = 12;
constexpr uint16_t kTodayCardHeight = 72;
constexpr uint8_t kCardRadius = 10;

const char *const kWeekdayNames[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char *const kFullWeekdayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
                                         "Saturday"};
const char *const kFullMonthNames[] = {"January", "February", "March", "April", "May", "June",
                                       "July", "August", "September", "October", "November", "December"};
const char *const kMonthNames[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                   "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

const char *ordinal(unsigned day) {
  if (day % 100 >= 11 && day % 100 <= 13) return "th";
  switch (day % 10) {
  case 1: return "st";
  case 2: return "nd";
  case 3: return "rd";
  default: return "th";
  }
}

void formatDate(CalendarDate date, char *output, size_t outputSize) {
  snprintf(output, outputSize, "%u%s %s", date.day, ordinal(date.day),
           date.month >= 1 && date.month <= 12 ? kFullMonthNames[date.month - 1] : "Unknown");
}

void formatForecastRange(CalendarDate date, char *output, size_t outputSize) {
  const CalendarDate first = calendarDateAddDays(date, 1);
  const CalendarDate last = calendarDateAddDays(date, 5);
  if (first.month == last.month) {
    snprintf(output, outputSize, "%u - %u %s", first.day, last.day, kMonthNames[last.month - 1]);
  } else {
    snprintf(output, outputSize, "%s %u %s - %s %u %s", kWeekdayNames[calendarWeekday(first)], first.day,
             kMonthNames[first.month - 1], kWeekdayNames[calendarWeekday(last)], last.day,
             kMonthNames[last.month - 1]);
  }
}

const char *secondaryText(const CalendarEvent &event) {
  return event.location[0] != '\0' ? event.location : event.source;
}

uint16_t baselineFromTop(DisplayTarget &display, uint16_t top, uint8_t font) {
  return static_cast<uint16_t>(top + display.fontHeight(font));
}

void fitText(DisplayTarget &display, const char *source, char *output, size_t outputSize,
             uint8_t font, uint16_t maxWidth) {
  if (outputSize == 0) return;
  snprintf(output, outputSize, "%s", source == nullptr ? "" : source);
  if (display.textWidth(output, font) <= maxWidth) return;
  size_t length = strlen(output);
  while (length > 0) {
    output[--length] = '\0';
    if (length + 4 > outputSize) continue;
    memcpy(output + length, "...", 4);
    if (display.textWidth(output, font) <= maxWidth) return;
    output[length] = '\0';
  }
}

void drawCurrentWeather(DisplayTarget &display, const CurrentWeather &weather) {
  WeatherIcons::draw(display, weather.icon, 407, 25, 58, DisplayColor::White);
  char temperature[12];
  char range[28];
  snprintf(temperature, sizeof(temperature), "%d", weather.temperature);
  snprintf(range, sizeof(range), "H: %d  L: %d", weather.high, weather.low);
  display.text(474, baselineFromTop(display, 29, kCalendarFontHeading), temperature, kCalendarFontHeading,
               DisplayColor::White, DisplayColor::Black);
  display.circle(static_cast<uint16_t>(479 + display.textWidth(temperature, kCalendarFontHeading)), 36, 3,
                 DisplayColor::White, false);
  display.text(474, baselineFromTop(display, 75, kCalendarFontFooter), range, kCalendarFontFooter,
               DisplayColor::White, DisplayColor::Black);
}

void drawTodayCard(DisplayTarget &display, uint16_t y, const CalendarEvent &event, int64_t dayStart) {
  display.roundRect(kOuterPadding, y, kLeftColumnWidth - 2 * kOuterPadding, kTodayCardHeight, kCardRadius,
                    DisplayColor::White, DisplayColor::Black);
  display.line(122, y + 12, 122, y + kTodayCardHeight - 12);
  char time[12];
  char title[kEventTitleLength];
  char detail[30];
  formatEventTime(event, dayStart, time, sizeof(time));
  fitText(display, event.title, title, sizeof(title), kCalendarFontTitle, 390);
  fitText(display, secondaryText(event), detail, sizeof(detail), kCalendarFontFooter, 390);
  display.text(29, static_cast<uint16_t>(y + (kTodayCardHeight + display.fontHeight(kCalendarFontMetadata)) / 2),
               time, kCalendarFontMetadata);
  display.text(140, baselineFromTop(display, y + 9, kCalendarFontTitle), title, kCalendarFontTitle);
  if (detail[0] != '\0') {
    display.text(140, baselineFromTop(display, y + 43, kCalendarFontFooter), detail, kCalendarFontFooter);
  }
}

void drawToday(DisplayTarget &display, const CalendarEvent *events, size_t count, CalendarDate date) {
  const CalendarRange range = calendarTodayRange(date);
  CalendarEvent today[kMaxEvents];
  const size_t todayCount = selectEventsForRange(events, count, range, today, kMaxEvents);
  if (todayCount == 0) {
    display.roundRect(kOuterPadding, 136, kLeftColumnWidth - 2 * kOuterPadding, 100, kCardRadius,
                      DisplayColor::White, DisplayColor::Black);
    display.text(40, 194, "No events today", kCalendarFontTitle);
    return;
  }
  const size_t visibleCount = todayCount < 5 ? todayCount : 4;
  uint16_t y = 128;
  for (size_t i = 0; i < visibleCount; ++i) {
    drawTodayCard(display, y, today[i], range.startEpoch);
    y = static_cast<uint16_t>(y + kTodayCardHeight + kCardGap);
  }
  if (todayCount > visibleCount) {
    const uint16_t labelWidth = display.textWidth("More events...", kCalendarFontFooter);
    display.text(static_cast<uint16_t>(kLeftColumnWidth - kOuterPadding - labelWidth), 469,
                 "More events...", kCalendarFontFooter);
  }
}

void drawForecast(DisplayTarget &display, const WeatherData &weather) {
  for (size_t i = 0; i < 5; ++i) {
    const uint16_t x = static_cast<uint16_t>(568 + i * 45);
    char weekday[4];
    snprintf(weekday, sizeof(weekday), "%s", weather.forecast[i].weekday);
    for (size_t letter = 1; letter < strlen(weekday); ++letter) {
      if (weekday[letter] >= 'A' && weekday[letter] <= 'Z') weekday[letter] += 'a' - 'A';
    }
    const uint16_t weekdayWidth = display.textWidth(weekday, kCalendarFontFooter);
    const uint16_t weekdayX = static_cast<uint16_t>(x + (45 - (weekdayWidth < 45 ? weekdayWidth : 45)) / 2);
    display.text(weekdayX, baselineFromTop(display, 71, kCalendarFontFooter), weekday,
                 kCalendarFontFooter,
                 DisplayColor::Black, DisplayColor::LightGrey);
    WeatherIcons::draw(display, weather.forecast[i].icon, x + 5, 92, 30);
    char temperature[10];
    snprintf(temperature, sizeof(temperature), "%d", weather.forecast[i].temperature);
    const uint16_t temperatureWidth = display.textWidth(temperature, kCalendarFontFooter);
    const uint16_t groupWidth = static_cast<uint16_t>(temperatureWidth + 7);
    const uint16_t temperatureX = static_cast<uint16_t>(x + (45 - (groupWidth < 45 ? groupWidth : 45)) / 2);
    display.text(temperatureX, baselineFromTop(display, 135, kCalendarFontFooter), temperature,
                 kCalendarFontFooter,
                 DisplayColor::Black, DisplayColor::LightGrey);
    display.circle(static_cast<uint16_t>(temperatureX + temperatureWidth + 3), 137, 2,
                   DisplayColor::Black, false);
  }
}

void drawUpcomingCard(DisplayTarget &display, uint16_t y, const CalendarEvent &event, CalendarDate eventDate) {
  display.roundRect(576, y, 208, 88, kCardRadius, DisplayColor::White, DisplayColor::Black);
  display.line(632, y + 10, 632, y + 78);
  char number[8];
  char title[kEventTitleLength];
  char time[12];
  snprintf(number, sizeof(number), "%u", eventDate.day);
  fitText(display, event.title, title, sizeof(title), kCalendarFontTitle, 128);
  formatEventTime(event, calendarTodayRange(eventDate).startEpoch, time, sizeof(time));
  display.text(588, baselineFromTop(display, y + 9, kCalendarFontFooter),
               kWeekdayNames[calendarWeekday(eventDate)], kCalendarFontFooter);
  display.text(590, baselineFromTop(display, y + 31, kCalendarFontTitle), number, kCalendarFontTitle);
  display.text(588, baselineFromTop(display, y + 63, kCalendarFontFooter),
               kMonthNames[eventDate.month - 1], kCalendarFontFooter);
  display.text(644, baselineFromTop(display, y + 11, kCalendarFontTitle), title, kCalendarFontTitle);
  display.text(644, baselineFromTop(display, y + 43, kCalendarFontFooter), time, kCalendarFontFooter);
  const char *detail = secondaryText(event);
  if (detail[0] != '\0') {
    char shortDetail[20];
    fitText(display, detail, shortDetail, sizeof(shortDetail), kCalendarFontFooter, 128);
    display.text(644, baselineFromTop(display, y + 63, kCalendarFontFooter), shortDetail,
                 kCalendarFontFooter);
  }
}

void drawLater(DisplayTarget &display, const CalendarEvent *events, size_t count, CalendarDate date,
               const WeatherData &weather) {
  display.fillRect(kLeftColumnWidth, 0, kRightColumnWidth, kDisplayHeight, DisplayColor::LightGrey);
  const uint8_t headingFont = display.textWidth("Later this week", kCalendarFontHeading) <= 208
      ? kCalendarFontHeading : kCalendarFontTitle;
  display.text(576, baselineFromTop(display, 10, headingFont), "Later this week", headingFont,
               DisplayColor::Black, DisplayColor::LightGrey);
  char range[52];
  formatForecastRange(date, range, sizeof(range));
  char fittedRange[52];
  fitText(display, range, fittedRange, sizeof(fittedRange), kCalendarFontFooter, 208);
  display.text(576, baselineFromTop(display, 46, kCalendarFontFooter), fittedRange, kCalendarFontFooter,
               DisplayColor::Black, DisplayColor::LightGrey);
  drawForecast(display, weather);

  const CalendarRange laterRange = calendarRestOfWeekRange(date);
  CalendarEvent later[kMaxEvents];
  const size_t laterCount = selectEventsForRange(events, count, laterRange, later, kMaxEvents);
  const size_t visibleCount = laterCount < 4 ? laterCount : 3;
  uint16_t y = 166;
  for (size_t i = 0; i < visibleCount; ++i) {
    CalendarDate eventDate = later[i].allDay ? later[i].allDayStartDate : calendarDateFromEpoch(later[i].startEpoch);
    if (!later[i].allDay) {
      unsigned hour = 0;
      unsigned minute = 0;
      localTimeFromEpoch(later[i].startEpoch, eventDate, hour, minute);
    }
    drawUpcomingCard(display, y, later[i], eventDate);
    y = static_cast<uint16_t>(y + 98);
  }
  if (laterCount == 0) display.text(592, 211, "No upcoming events", kCalendarFontFooter,
                                    DisplayColor::Black, DisplayColor::LightGrey);
  if (laterCount > visibleCount) display.text(706, 469, "More...", kCalendarFontFooter,
                                               DisplayColor::Black, DisplayColor::LightGrey);
}

const char *statusHeading(DisplayStatus status) {
  switch (status) {
  case DisplayStatus::WifiSetupRequired: return "Wi-Fi setup required";
  case DisplayStatus::WifiConnectionFailed: return "Unable to connect to Wi-Fi";
  case DisplayStatus::TimeSyncFailed: return "Unable to synchronize time";
  case DisplayStatus::CalendarFeedSetupRequired: return "Calendar feed setup required";
  case DisplayStatus::CalendarFeedFailed: return "Unable to load calendar feed";
  case DisplayStatus::Calendar: return "E-Ink Calendar";
  }
  return "Calendar error";
}

} // namespace

bool renderCalendar(DisplayTarget &display, const CalendarEvent *events, size_t eventCount, CalendarDate date,
                    const char *firmwareVersion, const char *deviceModel, int16_t batteryTenthsVolts) {
  return renderCalendar(display, events, eventCount, date, firmwareVersion, deviceModel, batteryTenthsVolts,
                        defaultWeatherData());
}

bool renderCalendar(DisplayTarget &display, const CalendarEvent *events, size_t eventCount, CalendarDate date,
                    const char *firmwareVersion, const char *deviceModel, int16_t batteryTenthsVolts,
                    const WeatherData &weather) {
  (void)firmwareVersion;
  (void)deviceModel;
  (void)batteryTenthsVolts;
  if (!display.begin()) return false;
  display.fillRect(0, 0, kLeftColumnWidth, kHeaderHeight, DisplayColor::Black);
  char dateLabel[36];
  formatDate(date, dateLabel, sizeof(dateLabel));
  display.text(20, baselineFromTop(display, 10, kCalendarFontMetadata), dateLabel, kCalendarFontMetadata,
               DisplayColor::White, DisplayColor::Black);
  const char *weekday = kFullWeekdayNames[calendarWeekday(date)];
  char shortWeekday[4];
  if (display.textWidth(weekday, kCalendarFontMain) > 370) {
    snprintf(shortWeekday, sizeof(shortWeekday), "%.3s", weekday);
    weekday = shortWeekday;
  }
  display.text(20, baselineFromTop(display, 37, kCalendarFontMain), weekday, kCalendarFontMain,
               DisplayColor::White, DisplayColor::Black);
  drawCurrentWeather(display, weather.current);
  drawToday(display, events, eventCount, date);
  drawLater(display, events, eventCount, date, weather);
  return display.refresh();
}

bool renderStatus(DisplayTarget &display, DisplayStatus status, const char *message) {
  if (!display.begin()) return false;
  display.text(32, baselineFromTop(display, 5, kCalendarFontMain), "E-Ink Calendar", kCalendarFontMain);
  display.line(32, 76, 768, 76);
  display.text(32, baselineFromTop(display, 113, kCalendarFontHeading), statusHeading(status),
               kCalendarFontHeading);
  if (message != nullptr && message[0] != '\0') {
    display.text(32, baselineFromTop(display, 170, kCalendarFontTitle), message, kCalendarFontTitle);
  }
  display.text(32, baselineFromTop(display, 456, kCalendarFontFooter),
               "The device will retry automatically after sleep.", kCalendarFontFooter);
  return display.refresh();
}

} // namespace calendar
