#pragma once

#include <calendar/calendar.h>

namespace calendar {

class MockCalendarProvider : public CalendarProvider {
public:
  explicit MockCalendarProvider(CalendarDate date);

  size_t loadEvents(CalendarEvent *events, size_t capacity) const override;

private:
  CalendarDate _date;
};

} // namespace calendar
