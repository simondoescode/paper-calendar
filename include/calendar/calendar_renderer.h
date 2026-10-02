#pragma once

#include <calendar/calendar.h>
#include <calendar/display_target.h>
#include <calendar/footer_status.h>
#include <calendar/weather_icons.h>

namespace calendar {

  bool renderCalendar(DisplayTarget &display, const CalendarEvent *events, size_t eventCount, CalendarDate date,
                      const char *firmwareVersion, const char *deviceModel, int16_t batteryTenthsVolts);
  bool renderCalendar(DisplayTarget &display, const CalendarEvent *events, size_t eventCount, CalendarDate date,
                      const char *firmwareVersion, const char *deviceModel, int16_t batteryTenthsVolts,
                      const WeatherData &weather);
  bool renderCalendar(DisplayTarget &display, const CalendarEvent *events, size_t eventCount, CalendarDate date,
                      const char *firmwareVersion, const char *deviceModel, int16_t batteryTenthsVolts,
                      const WeatherData &weather, const FooterStatus &footer);
  bool renderStatus(DisplayTarget &display, DisplayStatus status, const char *message);

} // namespace calendar
