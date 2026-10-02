#include <calendar/calendar_renderer.h>
#if defined(ARDUINO) && !defined(CALENDAR_HOST)
#include <Arduino.h>
#endif
#include <assets/status/status_bitmaps.h>

#include <stdio.h>
#include <math.h>
#include <string.h>

namespace calendar {
namespace {

constexpr uint16_t kDisplayWidth = 800;
constexpr uint16_t kDisplayHeight = 480;
constexpr uint16_t kHeaderHeight = 112;
constexpr uint16_t kOuterPadding = 16;
constexpr uint16_t kColumnGutter = 2 * kOuterPadding;
constexpr uint16_t kUsableContentWidth = kDisplayWidth - 2 * kOuterPadding;
constexpr uint16_t ratioWidth(uint16_t width, uint16_t permille) {
  return static_cast<uint16_t>((static_cast<uint32_t>(width) * permille + 500) / 1000);
}
constexpr uint16_t kMainContentWidth = ratioWidth(kUsableContentWidth - kColumnGutter, 600);
constexpr uint16_t kSidebarContentWidth = kUsableContentWidth - kColumnGutter - kMainContentWidth;
constexpr uint16_t kSidebarX = kOuterPadding + kMainContentWidth + kColumnGutter;
// Full-bleed backgrounds meet halfway through the padded gutter.
constexpr uint16_t kLeftColumnWidth = kSidebarX - kColumnGutter / 2;
constexpr uint16_t kRightColumnWidth = kDisplayWidth - kLeftColumnWidth;
constexpr uint16_t kUpcomingDayBlockWidth = 44;
constexpr uint16_t kUpcomingCardHeight = 52;
constexpr uint8_t kUpcomingTitleFont = kCalendarFontMetadata;
constexpr uint16_t kUpcomingCardGap = 8;
constexpr uint16_t kUpcomingTextPadding = 12;
constexpr uint16_t kTodayTextX = 140;
constexpr uint16_t kTodayTextWidth = kOuterPadding + kMainContentWidth - kTodayTextX - 14;
constexpr uint16_t kUpcomingDividerX = kSidebarX + kUpcomingDayBlockWidth;
constexpr uint16_t kUpcomingTextX = kUpcomingDividerX + kUpcomingTextPadding;
constexpr uint16_t kUpcomingTextWidth = kSidebarX + kSidebarContentWidth - kUpcomingTextX - kUpcomingTextPadding;
constexpr uint16_t kCurrentWeatherWidth = 138;
constexpr uint16_t kCurrentWeatherX = kLeftColumnWidth - kOuterPadding - kCurrentWeatherWidth;
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
    display.text(kCurrentWeatherX, baselineFromTop(display, 40, kCalendarFontFooter), "Weather unavailable",
                 kCalendarFontFooter, CalendarColors::Background, CalendarColors::Foreground);
    return;
  }
  WeatherIcons::draw(display, getWeatherIcon(weather.current.condition, weather.current.isDay),
                     kCurrentWeatherX + 6, 22, WeatherIcons::CURRENT_SIZE, CalendarColors::Background);
  char temperature[12];
  char range[40];
  char fittedRange[40];
  snprintf(temperature, sizeof(temperature), "%ld", lroundf(weather.current.temperatureC));
  snprintf(range, sizeof(range), "H:%ld L:%ld Rain:%d%%", lroundf(weather.forecast[0].temperatureMaxC),
           lroundf(weather.forecast[0].temperatureMinC), weather.forecast[0].precipitationProbability);
  display.text(kCurrentWeatherX + 68, baselineFromTop(display, 32, kCalendarFontHeading), temperature, kCalendarFontHeading,
               CalendarColors::Background, CalendarColors::Foreground);
  display.circle(static_cast<uint16_t>(kCurrentWeatherX + 73 + display.textWidth(temperature, kCalendarFontHeading)), 33, 3,
                 CalendarColors::Background, false);
  char label[24];
  fitText(display, conditionLabel(weather.current.condition), label, sizeof(label), kCalendarFontFooter, 138);
  display.text(kCurrentWeatherX, baselineFromTop(display, 75, kCalendarFontFooter), label, kCalendarFontFooter,
               CalendarColors::Background, CalendarColors::Foreground);
  fitText(display, range, fittedRange, sizeof(fittedRange), kCalendarFontFooter, 138);
  display.text(kCurrentWeatherX, baselineFromTop(display, 94, kCalendarFontFooter), fittedRange, kCalendarFontFooter,
               CalendarColors::Background, CalendarColors::Foreground);
}

void drawTodayCard(DisplayTarget &display, uint16_t y, const CalendarEvent &event, int64_t dayStart) {
  display.roundRect(kOuterPadding, y, kMainContentWidth, kTodayCardHeight, kCardRadius,
                    CalendarColors::Background, CalendarColors::Foreground);
  display.line(122, y + 12, 122, y + kTodayCardHeight - 12);
  char time[12];
  char title[kEventTitleLength];
  char detail[30];
  formatEventTime(event, dayStart, time, sizeof(time));
  fitText(display, event.title, title, sizeof(title), kCalendarFontTitle, kTodayTextWidth);
  fitText(display, secondaryText(event), detail, sizeof(detail), kCalendarFontFooter, kTodayTextWidth);
  display.text(29, static_cast<uint16_t>(y + (kTodayCardHeight + display.fontHeight(kCalendarFontMetadata)) / 2),
               time, kCalendarFontMetadata);
  display.text(kTodayTextX, baselineFromTop(display, y + 17, kCalendarFontTitle), title, kCalendarFontTitle);
  if (detail[0] != '\0') {
    display.text(kTodayTextX, baselineFromTop(display, y + 43, kCalendarFontFooter), detail, kCalendarFontFooter);
  }
}

size_t drawToday(DisplayTarget &display, const CalendarEvent *events, size_t count, CalendarDate date) {
  const CalendarRange range = calendarTodayRange(date);
  CalendarEvent today[kMaxEvents];
  const size_t todayCount = selectEventsForRange(events, count, range, today, kMaxEvents);
  if (todayCount == 0) {
    display.roundRect(kOuterPadding, 136, kMainContentWidth, 100, kCardRadius,
                      CalendarColors::Background, CalendarColors::Foreground);
    display.text(40, 194, "No events today", kCalendarFontTitle);
    return 0;
  }
  const size_t visibleCount = todayCount < 5 ? todayCount : 4;
  uint16_t y = 128;
  for (size_t i = 0; i < visibleCount; ++i) {
    drawTodayCard(display, y, today[i], range.startEpoch);
    y = static_cast<uint16_t>(y + kTodayCardHeight + kCardGap);
  }
  return todayCount;
}

void drawForecast(DisplayTarget &display, const WeatherData &weather) {
  if (!weather.valid) return;
  for (size_t i = 0; i < kWeatherForecastDays; ++i) {
    const uint16_t cellLeft = kSidebarX + i * kSidebarContentWidth / kWeatherForecastDays;
    const uint16_t cellRight = kSidebarX + (i + 1) * kSidebarContentWidth / kWeatherForecastDays;
    const uint16_t centerX = (cellLeft + cellRight) / 2;
    int year = 0; unsigned month = 0, day = 0;
    sscanf(weather.forecast[i].date, "%d-%u-%u", &year, &month, &day);
    const CalendarDate date = {year, month, day};
    char weekday[4];
    snprintf(weekday, sizeof(weekday), "%s", kWeekdayNames[calendarWeekday(date)]);
    for (size_t letter = 1; letter < 3; ++letter) weekday[letter] += 'a' - 'A';
    display.text(centerX - display.textWidth(weekday, kCalendarFontFooter) / 2, baselineFromTop(display, 51, kCalendarFontFooter), weekday,
                 kCalendarFontFooter, CalendarColors::Foreground, CalendarPatterns::Sidebar);
    WeatherIcons::draw(display, getWeatherIcon(weather.forecast[i].condition, true),
                       centerX - WeatherIcons::FORECAST_SIZE / 2, 72, WeatherIcons::FORECAST_SIZE);
  }
}

void drawUpcomingCard(DisplayTarget &display, uint16_t y, const CalendarEvent &event, CalendarDate eventDate) {
  display.roundRect(kSidebarX, y, kSidebarContentWidth, kUpcomingCardHeight, kCardRadius, CalendarColors::Background, CalendarColors::Foreground);
  display.line(kUpcomingDividerX, y + 10, kUpcomingDividerX, y + kUpcomingCardHeight - 10);
  char title[kEventTitleLength];
  fitText(display, event.title, title, sizeof(title), kUpcomingTitleFont, kUpcomingTextWidth);
  const char *weekday = kWeekdayNames[calendarWeekday(eventDate)];
  display.text(kSidebarX + (kUpcomingDayBlockWidth - display.textWidth(weekday, kCalendarFontFooter)) / 2,
               y + (kUpcomingCardHeight + display.fontHeight(kCalendarFontFooter)) / 2,
               weekday, kCalendarFontFooter);
  display.text(kUpcomingTextX, y + (kUpcomingCardHeight + display.fontHeight(kUpcomingTitleFont)) / 2,
               title, kUpcomingTitleFont);
}

void drawLater(DisplayTarget &display, const CalendarEvent *events, size_t count, CalendarDate date,
               const WeatherData &weather) {
  display.fillRect(kLeftColumnWidth, 0, kRightColumnWidth, kDisplayHeight, CalendarPatterns::Sidebar);
  const uint8_t headingFont = display.textWidth("Later this week", kCalendarFontHeading) <= kSidebarContentWidth
      ? kCalendarFontHeading : kCalendarFontTitle;
  display.text(kSidebarX, baselineFromTop(display, 17, headingFont), "Later this week", headingFont,
               CalendarColors::Foreground, CalendarPatterns::Sidebar);
  drawForecast(display, weather);

  const CalendarRange laterRange = calendarRestOfWeekRange(date);
  CalendarEvent later[kMaxEvents];
  const size_t laterCount = selectEventsForRange(events, count, laterRange, later, kMaxEvents);
  const size_t visibleCount = laterCount < 4 ? laterCount : 3;
  uint16_t y = 124;
  for (size_t i = 0; i < visibleCount; ++i) {
    CalendarDate eventDate = later[i].allDay ? later[i].allDayStartDate : calendarDateFromEpoch(later[i].startEpoch);
    if (!later[i].allDay) {
      unsigned hour = 0;
      unsigned minute = 0;
      localTimeFromEpoch(later[i].startEpoch, eventDate, hour, minute);
    }
    drawUpcomingCard(display, y, later[i], eventDate);
    y = static_cast<uint16_t>(y + kUpcomingCardHeight + kUpcomingCardGap);
  }
  if (laterCount == 0) display.text(kSidebarX + 16, 155, "No upcoming events", kCalendarFontFooter,
                                    CalendarColors::Foreground, CalendarPatterns::Sidebar);
}

void drawWifiIcon(DisplayTarget &display, uint16_t x, uint16_t y, bool connected) {
  display.bitmap(x, y, connected ? StatusIcons::k_wifi : StatusIcons::k_wifi_off,
                 kFooterIconSize, kFooterIconSize, CalendarColors::Foreground);
}

void drawCalendarIcon(DisplayTarget &display, uint16_t x, uint16_t y) {
  display.bitmap(x, y, StatusIcons::k_calendar, kFooterIconSize, kFooterIconSize, CalendarColors::Foreground);
}

void drawBatteryIcon(DisplayTarget &display, uint16_t x, uint16_t y, const FooterStatus &status) {
  display.bitmap(x, y, StatusIcons::k_battery, kFooterIconSize, kFooterIconSize, CalendarColors::Foreground);
  if (status.batteryPercent >= 0) {
    const int percent = status.batteryPercent > 100 ? 100 : status.batteryPercent;
    const uint16_t fill = static_cast<uint16_t>(percent * 8 / 100);
    if (fill) display.fillRect(x + 3, y + 6, fill, 4, CalendarColors::Foreground);
  }
}

void drawFooter(DisplayTarget &display, const FooterStatus &status) {
  display.fillRect(0, kFooterTop, kDisplayWidth, kFooterHeight, CalendarColors::Background);
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
  const uint16_t updateWidth = display.textWidth(updateLabel, kFooterFont);
  const uint16_t leftWidth = kFooterIconSize + kFooterIconGap + eventWidth + separatorWidth + updateWidth;
  const uint16_t rightFixedWidth = 2 * (kFooterIconSize + kFooterIconGap) + separatorWidth + batteryWidth;
  const uint16_t networkBudget = kDisplayWidth - 2 * kFooterPadding - leftWidth - kFooterGroupGap - rightFixedWidth;
  char networkLabel[40];
  fitText(display, status.wifiLabel, networkLabel, sizeof(networkLabel), kFooterFont, networkBudget);
  const uint16_t networkWidth = display.textWidth(networkLabel, kFooterFont);
  const uint16_t rightX = kDisplayWidth - kFooterPadding - rightFixedWidth - networkWidth;

  uint16_t x = kFooterPadding;
  drawCalendarIcon(display, x, iconY);
  x += kFooterIconSize + kFooterIconGap;
  display.text(x, baseline, eventLabel, kFooterFont);
  x += eventWidth + kFooterSeparatorGap;
  display.circle(x, centerY, 1, CalendarColors::Foreground, true);
  display.text(x + kFooterSeparatorGap, baseline, updateLabel, kFooterFont);

  x = rightX;
  drawWifiIcon(display, x, iconY, status.wifiConnected);
  x += kFooterIconSize + kFooterIconGap;
  display.text(x, baseline, networkLabel, kFooterFont);
  x += networkWidth + kFooterSeparatorGap;
  display.circle(x, centerY, 1, CalendarColors::Foreground, true);
  x += kFooterSeparatorGap;
  drawBatteryIcon(display, x, iconY, status);
  display.text(x + kFooterIconSize + kFooterIconGap, baseline, batteryLabel, kFooterFont);
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
  display.fillRect(0, 0, kLeftColumnWidth, kHeaderHeight, CalendarColors::Foreground);
  char dateLabel[36];
  formatDate(date, dateLabel, sizeof(dateLabel));
  display.text(20, baselineFromTop(display, 18, kCalendarFontMetadata), dateLabel, kCalendarFontMetadata,
               CalendarColors::Background, CalendarColors::Foreground);
  const char *weekday = kFullWeekdayNames[calendarWeekday(date)];
  char shortWeekday[4];
  if (display.textWidth(weekday, kCalendarFontMain) > kCurrentWeatherX - 20 - kOuterPadding) {
    snprintf(shortWeekday, sizeof(shortWeekday), "%.3s", weekday);
    weekday = shortWeekday;
  }
  display.text(20, baselineFromTop(display, 46, kCalendarFontMain), weekday, kCalendarFontMain,
               CalendarColors::Background, CalendarColors::Foreground);
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
