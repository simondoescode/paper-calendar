#pragma once

#include <calendar/calendar.h>

namespace calendar {

  enum class IcalendarError {
    None,
    InvalidInput,
    Transport,
    HttpStatus,
    FeedTooLarge,
    MalformedFeed,
    UnsupportedTimezone,
    UnsupportedRecurrence,
    TooManyEvents,
  };

  class IcalendarParser {
  public:
    IcalendarParser(CalendarRange range, CalendarEvent *events, size_t capacity);

    bool write(const uint8_t *data, size_t length);
    bool finish();
    size_t eventCount() const;
    IcalendarError error() const;

  private:
    bool processPhysicalLine();
    bool processLogicalLine();
    bool finishEvent();

    CalendarRange _range;
    CalendarEvent *_events;
    size_t _capacity;
    size_t _eventCount;
    size_t _bytesRead;
    size_t _physicalLength;
    size_t _logicalLength;
    bool _insideCalendar;
    bool _calendarStarted;
    bool _calendarEnded;
    bool _insideEvent;
    bool _finished;
    bool _hasStart;
    bool _hasEnd;
    bool _cancelled;
    bool _hasRecurrence;
    CalendarEvent _currentEvent;
    char _physicalLine[512];
    char _logicalLine[768];
    IcalendarError _error;
  };

} // namespace calendar
