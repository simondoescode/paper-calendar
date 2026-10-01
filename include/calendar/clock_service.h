#pragma once

#include <calendar/calendar.h>

#include <stdint.h>

namespace calendar {

class ClockService {
public:
  bool synchronize();
  bool now(int64_t &epoch, CalendarDate &localDate, unsigned &hour, unsigned &minute) const;
};

} // namespace calendar
