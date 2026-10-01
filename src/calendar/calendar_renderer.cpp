#include <calendar/calendar_renderer.h>

#include <stdio.h>

namespace calendar {
namespace {

const char *const kWeekdayNames[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char *const kMonthNames[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                   "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
const char *const kFullWeekdayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
                                         "Saturday"};
const char *const kFullMonthNames[] = {"January", "February", "March", "April", "May", "June",
                                       "July", "August", "September", "October", "November", "December"};

void formatLongDate(CalendarDate date, char *output, size_t outputSize) {
  snprintf(output, outputSize, "%s %u %s", kFullWeekdayNames[calendarWeekday(date)], date.day,
           date.month >= 1 && date.month <= 12 ? kFullMonthNames[date.month - 1] : "Unknown");
}

void drawEvent(DisplayTarget &display, uint16_t timeX, uint16_t titleX, uint16_t y, const CalendarEvent &event,
               int64_t dayStart, size_t titleLimit) {
  char time[12];
  char title[kEventTitleLength];
  formatEventTime(event, dayStart, time, sizeof(time));
  truncateTitle(event.title, title, sizeof(title), titleLimit);
  display.text(timeX, y, time, 18);
  display.text(titleX, y, title, 18);
}

void drawToday(DisplayTarget &display, const CalendarEvent *events, size_t count, CalendarDate date) {
  const CalendarRange range = calendarTodayRange(date);
  CalendarEvent today[kMaxEvents];
  const size_t todayCount = selectEventsForRange(events, count, range, today, kMaxEvents);
  char formattedDate[32];
  formatLongDate(date, formattedDate, sizeof(formattedDate));
  display.text(32, 91, "TODAY", 24);
  display.text(32, 128, formattedDate, 18);
  if (todayCount == 0) {
    display.text(32, 184, "No events today", 18);
    return;
  }
  uint16_t y = 177;
  const size_t visibleCount = todayCount < 6 ? todayCount : 5;
  for (size_t i = 0; i < visibleCount; ++i) {
    drawEvent(display, 34, 154, y, today[i], range.startEpoch, 36);
    y = static_cast<uint16_t>(y + 48);
  }
  if (todayCount > visibleCount) {
    display.text(154, y, "More events...", 18);
  }
}

void drawWeek(DisplayTarget &display, const CalendarEvent *events, size_t count, CalendarDate date) {
  const CalendarRange weekRange = calendarRestOfWeekRange(date);
  display.text(548, 91, "THIS WEEK", 24);
  uint16_t y = 132;
  for (CalendarDate weekDate = weekRange.startDate;
       calendarEpoch(weekDate) < calendarEpoch(weekRange.endDate);
       weekDate = calendarDateAddDays(weekDate, 1)) {
    if (y >= 423) {
      break;
    }
    char heading[32];
    snprintf(heading, sizeof(heading), "%s %u %s", kWeekdayNames[calendarWeekday(weekDate)], weekDate.day,
             kMonthNames[weekDate.month - 1]);
    display.text(548, y, heading, 18);
    y = static_cast<uint16_t>(y + 25);
    CalendarEvent dayEvents[kMaxEvents];
    const CalendarRange dayRange = calendarTodayRange(weekDate);
    const size_t dayEventCount = selectEventsForRange(events, count, dayRange, dayEvents, kMaxEvents);
    if (dayEventCount == 0) {
      display.text(560, y, "No events", 18);
      y = static_cast<uint16_t>(y + 25);
    } else {
      const size_t visibleCount = dayEventCount < 4 ? dayEventCount : 3;
      for (size_t i = 0; i < visibleCount && y < 423; ++i) {
        drawEvent(display, 552, 626, y, dayEvents[i], dayRange.startEpoch, 15);
        y = static_cast<uint16_t>(y + 24);
      }
      if (dayEventCount > visibleCount && y < 423) {
        display.text(626, y, "More...", 18);
        y = static_cast<uint16_t>(y + 24);
      }
    }
    if (calendarEpoch(weekDate) < calendarEpoch(calendarDateAddDays(weekRange.endDate, -1))) {
      display.line(548, y, 768, y);
      y = static_cast<uint16_t>(y + 9);
    }
  }
}

const char *statusHeading(DisplayStatus status) {
  switch (status) {
  case DisplayStatus::WifiSetupRequired:
    return "Wi-Fi setup required";
  case DisplayStatus::WifiConnectionFailed:
    return "Unable to connect to Wi-Fi";
  case DisplayStatus::TimeSyncFailed:
    return "Unable to synchronize time";
  case DisplayStatus::CalendarFeedSetupRequired:
    return "Calendar feed setup required";
  case DisplayStatus::CalendarFeedFailed:
    return "Unable to load calendar feed";
  case DisplayStatus::Calendar:
    return "E-Ink Calendar";
  }
  return "Calendar error";
}

} // namespace

bool renderCalendar(DisplayTarget &display, const CalendarEvent *events, size_t eventCount, CalendarDate date,
                    const char *firmwareVersion, const char *deviceModel, int16_t batteryTenthsVolts) {
  if (!display.begin()) {
    return false;
  }
  char dateLabel[40];
  char footer[112];
  char resolution[20];
  formatLongDate(date, dateLabel, sizeof(dateLabel));
  snprintf(resolution, sizeof(resolution), "%ux%u", 800U, 480U);
  display.text(32, 20, "E-Ink Calendar", 24);
  display.text(32, 54, dateLabel, 18);
  display.line(32, 79, 768, 79);
  display.line(528, 91, 528, 432);
  drawToday(display, events, eventCount, date);
  drawWeek(display, events, eventCount, date);
  if (batteryTenthsVolts >= 0) {
    snprintf(footer, sizeof(footer), "FW %s  |  %s  |  %s  |  Wi-Fi CONNECTED  |  Battery %d.%d V",
             firmwareVersion, deviceModel, resolution, batteryTenthsVolts / 10,
             batteryTenthsVolts % 10 < 0 ? -(batteryTenthsVolts % 10) : batteryTenthsVolts % 10);
  } else {
    snprintf(footer, sizeof(footer), "FW %s  |  %s  |  %s  |  Wi-Fi CONNECTED  |  Battery unavailable",
             firmwareVersion, deviceModel, resolution);
  }
  display.text(32, 455, footer, 8);
  return display.refresh();
}

bool renderStatus(DisplayTarget &display, DisplayStatus status, const char *message) {
  if (!display.begin()) {
    return false;
  }
  display.text(32, 24, "E-Ink Calendar", 24);
  display.line(32, 76, 768, 76);
  display.text(32, 124, statusHeading(status), 24);
  if (message != nullptr && message[0] != '\0') {
    display.text(32, 175, message, 18);
  }
  display.text(32, 455, "The device will retry automatically after sleep.", 8);
  return display.refresh();
}

} // namespace calendar
