#include <calendar/calendar_renderer.h>

#include <stdio.h>
#include <math.h>
#include <string.h>

namespace calendar {
namespace {

constexpr uint16_t kDisplayWidth = 800;
constexpr uint16_t kDisplayHeight = 480;
constexpr uint16_t kLeftColumnWidth = 560;
constexpr uint16_t kRightColumnWidth = kDisplayWidth - kLeftColumnWidth;
constexpr uint16_t kHeaderHeight = 112;
constexpr uint16_t kOuterPadding = 16;
constexpr uint16_t kCardGap = 10;
constexpr uint16_t kTodayCardHeight = 72;
constexpr uint8_t kCardRadius = 10;
constexpr uint16_t kFooterHeight = 32;
constexpr uint16_t kFooterTop = kDisplayHeight - kFooterHeight;
constexpr uint16_t kFooterPadding = 16;
constexpr uint16_t kFooterIconSize = 16;
constexpr uint16_t kFooterIconGap = 7;
constexpr uint16_t kFooterSeparatorGap = 12;
constexpr uint16_t kFooterGroupGap = 24;
constexpr uint8_t kFooterFont = kCalendarFontStatus;

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
  output[outputSize - 1] = '\0'; // Also terminate on native CRTs that truncate without a NUL.
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

void drawCurrentWeather(DisplayTarget &display, const WeatherData &weather) {
  if (!weather.valid) {
    display.text(406, baselineFromTop(display, 40, kCalendarFontFooter), "Weather unavailable",
                 kCalendarFontFooter, DisplayColor::White, DisplayColor::Black);
    return;
  }
  WeatherIcons::draw(display, getWeatherIcon(weather.current.condition, weather.current.isDay),
                     412, 22, WeatherIcons::CURRENT_SIZE, DisplayColor::White);
  char temperature[12];
  char range[40];
  char fittedRange[40];
  snprintf(temperature, sizeof(temperature), "%ld", lroundf(weather.current.temperatureC));
  snprintf(range, sizeof(range), "H:%ld L:%ld Rain:%d%%", lroundf(weather.forecast[0].temperatureMaxC),
           lroundf(weather.forecast[0].temperatureMinC), weather.forecast[0].precipitationProbability);
  display.text(474, baselineFromTop(display, 32, kCalendarFontHeading), temperature, kCalendarFontHeading,
               DisplayColor::White, DisplayColor::Black);
  display.circle(static_cast<uint16_t>(479 + display.textWidth(temperature, kCalendarFontHeading)), 33, 3,
                 DisplayColor::White, false);
  char label[24];
  fitText(display, conditionLabel(weather.current.condition), label, sizeof(label), kCalendarFontFooter, 138);
  display.text(406, baselineFromTop(display, 75, kCalendarFontFooter), label, kCalendarFontFooter,
               DisplayColor::White, DisplayColor::Black);
  fitText(display, range, fittedRange, sizeof(fittedRange), kCalendarFontFooter, 138);
  display.text(406, baselineFromTop(display, 94, kCalendarFontFooter), fittedRange, kCalendarFontFooter,
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
  display.text(140, baselineFromTop(display, y + 17, kCalendarFontTitle), title, kCalendarFontTitle);
  if (detail[0] != '\0') {
    display.text(140, baselineFromTop(display, y + 43, kCalendarFontFooter), detail, kCalendarFontFooter);
  }
}

size_t drawToday(DisplayTarget &display, const CalendarEvent *events, size_t count, CalendarDate date) {
  const CalendarRange range = calendarTodayRange(date);
  CalendarEvent today[kMaxEvents];
  const size_t todayCount = selectEventsForRange(events, count, range, today, kMaxEvents);
  if (todayCount == 0) {
    display.roundRect(kOuterPadding, 136, kLeftColumnWidth - 2 * kOuterPadding, 100, kCardRadius,
                      DisplayColor::White, DisplayColor::Black);
    display.text(40, 194, "No events today", kCalendarFontTitle);
    return 0;
  }
  const size_t visibleCount = todayCount < 5 ? todayCount : 4;
  uint16_t y = 128;
  for (size_t i = 0; i < visibleCount; ++i) {
    drawTodayCard(display, y, today[i], range.startEpoch);
    y = static_cast<uint16_t>(y + kTodayCardHeight + kCardGap);
  }
  if (todayCount > visibleCount) {
    const uint16_t labelWidth = display.textWidth("More events...", kCalendarFontFooter);
    display.text(static_cast<uint16_t>(kLeftColumnWidth - kOuterPadding - labelWidth),
                 baselineFromTop(display, 113, kCalendarFontFooter),
                 "More events...", kCalendarFontFooter);
  }
  return todayCount;
}

void drawForecast(DisplayTarget &display, const WeatherData &weather) {
  if (!weather.valid) return;
  for (size_t i = 0; i < kWeatherForecastDays; ++i) {
    const uint16_t x = static_cast<uint16_t>(580 + i * 68);
    int year = 0; unsigned month = 0, day = 0;
    sscanf(weather.forecast[i].date, "%d-%u-%u", &year, &month, &day);
    const CalendarDate date = {year, month, day};
    char weekday[4];
    snprintf(weekday, sizeof(weekday), "%s", kWeekdayNames[calendarWeekday(date)]);
    for (size_t letter = 1; letter < 3; ++letter) weekday[letter] += 'a' - 'A';
    display.text(x + 10, baselineFromTop(display, 71, kCalendarFontFooter), weekday,
                 kCalendarFontFooter, DisplayColor::Black, DisplayColor::LightGrey);
    WeatherIcons::draw(display, getWeatherIcon(weather.forecast[i].condition, true),
                       x + 10, 92, WeatherIcons::FORECAST_SIZE);
    char temperature[12];
    snprintf(temperature, sizeof(temperature), "%ld/%ld", lroundf(weather.forecast[i].temperatureMaxC),
             lroundf(weather.forecast[i].temperatureMinC));
    display.text(x + 4, baselineFromTop(display, 135, kCalendarFontFooter), temperature,
                 kCalendarFontFooter, DisplayColor::Black, DisplayColor::LightGrey);
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
  display.text(644, baselineFromTop(display, y + 15, kCalendarFontTitle), title, kCalendarFontTitle);
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
  display.text(576, baselineFromTop(display, 17, headingFont), "Later this week", headingFont,
               DisplayColor::Black, DisplayColor::LightGrey);
  char range[52];
  if (weather.valid) snprintf(range, sizeof(range), "%s to %s", weather.forecast[0].date + 5, weather.forecast[2].date + 5);
  else formatForecastRange(date, range, sizeof(range));
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
    y = static_cast<uint16_t>(y + 96);
  }
  if (laterCount == 0) display.text(592, 211, "No upcoming events", kCalendarFontFooter,
                                    DisplayColor::Black, DisplayColor::LightGrey);
  if (laterCount > visibleCount) display.text(706, baselineFromTop(display, 150, kCalendarFontFooter), "More...", kCalendarFontFooter,
                                               DisplayColor::Black, DisplayColor::LightGrey);
}

void drawWifiIcon(DisplayTarget &display, uint16_t x, uint16_t y, bool connected) {
  const uint16_t outerX[] = {1, 4, 8, 12, 15};
  const uint16_t outerY[] = {4, 2, 1, 2, 4};
  const uint16_t innerX[] = {4, 6, 10, 12};
  const uint16_t innerY[] = {8, 6, 6, 8};
  for (size_t i = 1; i < 5; ++i) display.line(x + outerX[i-1], y + outerY[i-1], x + outerX[i], y + outerY[i]);
  for (size_t i = 1; i < 4; ++i) display.line(x + innerX[i-1], y + innerY[i-1], x + innerX[i], y + innerY[i]);
  display.circle(x + 8, y + 13, 1, DisplayColor::Black, true);
  if (!connected) display.line(x, y, x + 15, y + 15);
}

void drawCalendarIcon(DisplayTarget &display, uint16_t x, uint16_t y) {
  display.roundRect(x, y + 2, kFooterIconSize, 14, 1, DisplayColor::White, DisplayColor::Black);
  display.line(x, y + 6, x + 15, y + 6);
  display.line(x + 4, y, x + 4, y + 4);
  display.line(x + 11, y, x + 11, y + 4);
  display.fillRect(x + 4, y + 9, 2, 2, DisplayColor::Black);
  display.fillRect(x + 9, y + 9, 2, 2, DisplayColor::Black);
}

void drawBatteryIcon(DisplayTarget &display, uint16_t x, uint16_t y, const FooterStatus &status) {
  display.roundRect(x, y + 3, 14, 10, 1, DisplayColor::White, DisplayColor::Black);
  display.fillRect(x + 14, y + 6, 2, 4, DisplayColor::Black);
  if (status.batteryPercent >= 0) {
    const int percent = status.batteryPercent > 100 ? 100 : status.batteryPercent;
    const uint16_t fill = static_cast<uint16_t>(percent / 10);
    if (fill) display.fillRect(x + 2, y + 5, fill, 6, DisplayColor::Black);
  }
}

void drawFooter(DisplayTarget &display, const FooterStatus &status) {
  display.fillRect(0, kFooterTop, kDisplayWidth, kFooterHeight, DisplayColor::White);
  display.line(0, kFooterTop, kDisplayWidth - 1, kFooterTop);
  const uint16_t centerY = kFooterTop + kFooterHeight / 2;
  const uint16_t baseline = centerY + display.fontHeight(kFooterFont) / 2;
  const uint16_t iconY = centerY - kFooterIconSize / 2;
  char eventLabel[32];
  char batteryLabel[16];
  char updateLabel[32];
  snprintf(eventLabel, sizeof(eventLabel), "%d event%s today", status.todayEventCount,
           status.todayEventCount == 1 ? "" : "s");
  if (status.batteryPercent >= 0) snprintf(batteryLabel, sizeof(batteryLabel), "%d%%", status.batteryPercent > 100 ? 100 : status.batteryPercent);
  else if (status.batteryTenthsVolts >= 0) snprintf(batteryLabel, sizeof(batteryLabel), "%d.%dV",
                                                  status.batteryTenthsVolts / 10, status.batteryTenthsVolts % 10);
  else snprintf(batteryLabel, sizeof(batteryLabel), "--%%");
  snprintf(updateLabel, sizeof(updateLabel), "Last updated: %.5s", status.lastUpdated ? status.lastUpdated : "--:--");
  const uint16_t eventWidth = display.textWidth(eventLabel, kFooterFont);
  const uint16_t batteryWidth = display.textWidth(batteryLabel, kFooterFont);
  const uint16_t separatorWidth = kFooterSeparatorGap * 2;
  const uint16_t rightWidth = 2 * (kFooterIconSize + kFooterIconGap) + eventWidth + separatorWidth + batteryWidth;
  const uint16_t rightX = kDisplayWidth - kFooterPadding - rightWidth;
  uint16_t x = rightX;
  drawCalendarIcon(display, x, iconY);
  x += kFooterIconSize + kFooterIconGap;
  display.text(x, baseline, eventLabel, kFooterFont);
  x += eventWidth + kFooterSeparatorGap;
  display.circle(x, centerY, 1, DisplayColor::Black, true);
  x += kFooterSeparatorGap;
  drawBatteryIcon(display, x, iconY, status);
  display.text(x + kFooterIconSize + kFooterIconGap, baseline, batteryLabel, kFooterFont);

  x = kFooterPadding;
  drawWifiIcon(display, x, iconY, status.wifiConnected);
  x += kFooterIconSize + kFooterIconGap;
  const uint16_t updateWidth = display.textWidth(updateLabel, kFooterFont);
  const uint16_t networkBudget = rightX - kFooterGroupGap - x - separatorWidth - updateWidth;
  char networkLabel[40];
  fitText(display, status.wifiLabel, networkLabel, sizeof(networkLabel), kFooterFont, networkBudget);
  display.text(x, baseline, networkLabel, kFooterFont);
  x += display.textWidth(networkLabel, kFooterFont) + kFooterSeparatorGap;
  display.circle(x, centerY, 1, DisplayColor::Black, true);
  display.text(x + kFooterSeparatorGap, baseline, updateLabel, kFooterFont);
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

uint32_t footerDisplayHash(const FooterStatus &status) {
  uint32_t hash = 2166136261UL;
  const auto add = [&hash](const char *text, size_t limit) {
    for (size_t i = 0; text && i < limit && text[i]; ++i) { hash ^= static_cast<uint8_t>(text[i]); hash *= 16777619UL; }
    hash ^= 0; hash *= 16777619UL;
  };
  add(status.wifiLabel, 39);
  add(status.lastUpdated, 5);
  char values[64];
  snprintf(values, sizeof(values), "%u/%d/%d/%d", status.wifiConnected, status.todayEventCount,
           status.batteryPercent, status.batteryTenthsVolts);
  add(values, sizeof(values));
  return hash;
}

bool renderCalendar(DisplayTarget &display, const CalendarEvent *events, size_t eventCount, CalendarDate date,
                    const char *firmwareVersion, const char *deviceModel, int16_t batteryTenthsVolts) {
  return renderCalendar(display, events, eventCount, date, firmwareVersion, deviceModel, batteryTenthsVolts,
                        WeatherData{});
}

bool renderCalendar(DisplayTarget &display, const CalendarEvent *events, size_t eventCount, CalendarDate date,
                    const char *firmwareVersion, const char *deviceModel, int16_t batteryTenthsVolts,
                    const WeatherData &weather) {
  FooterStatus footer;
  footer.batteryTenthsVolts = batteryTenthsVolts;
  return renderCalendar(display, events, eventCount, date, firmwareVersion, deviceModel, batteryTenthsVolts,
                        weather, footer);
}

bool renderCalendar(DisplayTarget &display, const CalendarEvent *events, size_t eventCount, CalendarDate date,
                    const char *firmwareVersion, const char *deviceModel, int16_t batteryTenthsVolts,
                    const WeatherData &weather, const FooterStatus &footer) {
  (void)firmwareVersion;
  (void)deviceModel;
  (void)batteryTenthsVolts;
  if (!display.begin()) return false;
  display.fillRect(0, 0, kLeftColumnWidth, kHeaderHeight, DisplayColor::Black);
  char dateLabel[36];
  formatDate(date, dateLabel, sizeof(dateLabel));
  display.text(20, baselineFromTop(display, 18, kCalendarFontMetadata), dateLabel, kCalendarFontMetadata,
               DisplayColor::White, DisplayColor::Black);
  const char *weekday = kFullWeekdayNames[calendarWeekday(date)];
  char shortWeekday[4];
  if (display.textWidth(weekday, kCalendarFontMain) > 370) {
    snprintf(shortWeekday, sizeof(shortWeekday), "%.3s", weekday);
    weekday = shortWeekday;
  }
  display.text(20, baselineFromTop(display, 46, kCalendarFontMain), weekday, kCalendarFontMain,
               DisplayColor::White, DisplayColor::Black);
  drawCurrentWeather(display, weather);
  const size_t todayCount = drawToday(display, events, eventCount, date);
  drawLater(display, events, eventCount, date, weather);
  FooterStatus resolvedFooter = footer;
  if (resolvedFooter.todayEventCount < 0) resolvedFooter.todayEventCount = static_cast<int>(todayCount);
  drawFooter(display, resolvedFooter);
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
