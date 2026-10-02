#pragma once

#include <calendar/calendar.h>
#include <calendar/icalendar_parser.h>

namespace calendar {

  class IcalendarFeedProvider : public CalendarProvider {
  public:
    IcalendarFeedProvider(const char *url, CalendarRange range);

    size_t loadEvents(CalendarEvent *events, size_t capacity) const override;
    IcalendarError error() const;

  private:
    char _url[512];
    CalendarRange _range;
    mutable IcalendarError _error;
  };

  const char *icalendarErrorMessage(IcalendarError error);

} // namespace calendar
