#pragma once

#include <calendar/calendar.h>
#include <calendar/display_target.h>

namespace calendar {

bool renderCalendar(DisplayTarget &display, const CalendarEvent *events, size_t eventCount, CalendarDate date,
                    const char *firmwareVersion, const char *deviceModel, int16_t batteryTenthsVolts);
bool renderStatus(DisplayTarget &display, DisplayStatus status, const char *message);

} // namespace calendar
