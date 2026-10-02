#include <calendar/calendar_config.h>
#include <calendar/icalendar_parser.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace calendar {
  namespace {

    bool equalsIgnoreCase(const char *left, size_t leftLength, const char *right) {
      const size_t rightLength = strlen(right);
      if (leftLength != rightLength) {
        return false;
      }
      for (size_t i = 0; i < leftLength; ++i) {
        if (toupper(static_cast<unsigned char>(left[i])) != toupper(static_cast<unsigned char>(right[i]))) {
          return false;
        }
      }
      return true;
    }

    bool datesEqual(CalendarDate left, CalendarDate right) {
      return left.year == right.year && left.month == right.month && left.day == right.day;
    }

    int compareDates(CalendarDate left, CalendarDate right) {
      if (left.year != right.year) {
        return left.year < right.year ? -1 : 1;
      }
      if (left.month != right.month) {
        return left.month < right.month ? -1 : 1;
      }
      if (left.day != right.day) {
        return left.day < right.day ? -1 : 1;
      }
      return 0;
    }

    bool parseDigits(const char *text, size_t length, int &value) {
      value = 0;
      if (length == 0) {
        return false;
      }
      for (size_t i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') {
          return false;
        }
        value = value * 10 + text[i] - '0';
      }
      return true;
    }

    bool validDate(CalendarDate date) {
      if (date.month < 1 || date.month > 12 || date.day < 1 || date.day > 31) {
        return false;
      }
      const int64_t epoch = calendarEpoch(date);
      return datesEqual(calendarDateFromEpoch(epoch), date);
    }

    bool copyField(char *destination, size_t capacity, const char *source, size_t length) {
      if (capacity == 0 || length >= capacity) {
        return false;
      }
      memcpy(destination, source, length);
      destination[length] = '\0';
      return true;
    }

    bool parameterEquals(const char *parameters, size_t parameterLength, const char *name, const char *value) {
      if (parameters == nullptr) {
        return false;
      }
      size_t start = 0;
      while (start < parameterLength) {
        if (parameters[start] == ';') {
          ++start;
          continue;
        }
        size_t end = start;
        while (end < parameterLength && parameters[end] != ';') {
          ++end;
        }
        const char *equals = static_cast<const char *>(memchr(parameters + start, '=', end - start));
        if (equals != nullptr &&
            equalsIgnoreCase(parameters + start, static_cast<size_t>(equals - (parameters + start)), name)) {
          const char *parameterValue = equals + 1;
          size_t valueLength = end - static_cast<size_t>(parameterValue - parameters);
          if (valueLength >= 2 && parameterValue[0] == '"' && parameterValue[valueLength - 1] == '"') {
            ++parameterValue;
            valueLength -= 2;
          }
          return equalsIgnoreCase(parameterValue, valueLength, value);
        }
        start = end + 1;
      }
      return false;
    }

    void unescapeText(char *text) {
      size_t read = 0;
      size_t write = 0;
      while (text[read] != '\0') {
        if (text[read] == '\\' && text[read + 1] != '\0') {
          ++read;
          if (text[read] == 'n' || text[read] == 'N') {
            text[write++] = ' ';
          } else {
            text[write++] = text[read];
          }
          ++read;
        } else {
          text[write++] = text[read++];
        }
      }
      text[write] = '\0';
    }

    bool parseDateTimeProperty(const char *value, size_t valueLength, const char *parameters, size_t parameterLength,
                               bool &allDay, CalendarDate &date, int64_t &epoch, bool &unsupportedTimezone) {
      unsupportedTimezone = false;
      const bool valueIsDate = parameterEquals(parameters, parameterLength, "VALUE", "DATE");
      if (valueIsDate || valueLength == 8) {
        int year = 0;
        int month = 0;
        int day = 0;
        if (valueLength != 8 || !parseDigits(value, 4, year) || !parseDigits(value + 4, 2, month) ||
            !parseDigits(value + 6, 2, day)) {
          return false;
        }
        date = {year, static_cast<unsigned>(month), static_cast<unsigned>(day)};
        if (!validDate(date)) {
          return false;
        }
        allDay = true;
        epoch = 0;
        return true;
      }

      if (valueLength != 15 && valueLength != 16) {
        return false;
      }
      if (value[8] != 'T') {
        return false;
      }
      int year = 0;
      int month = 0;
      int day = 0;
      int hour = 0;
      int minute = 0;
      int second = 0;
      if (!parseDigits(value, 4, year) || !parseDigits(value + 4, 2, month) || !parseDigits(value + 6, 2, day) ||
          !parseDigits(value + 9, 2, hour) || !parseDigits(value + 11, 2, minute) ||
          !parseDigits(value + 13, 2, second) || second > 59) {
        return false;
      }
      date = {year, static_cast<unsigned>(month), static_cast<unsigned>(day)};
      if (!validDate(date) || hour > 23 || minute > 59) {
        return false;
      }

      const bool utcSuffix = valueLength == 16 && value[15] == 'Z';
      if (valueLength == 16 && !utcSuffix) {
        return false;
      }
      const bool londonTimezone = parameterEquals(parameters, parameterLength, "TZID", "Europe/London");
      const bool utcTimezone = parameterEquals(parameters, parameterLength, "TZID", "UTC") ||
                               parameterEquals(parameters, parameterLength, "TZID", "Etc/UTC");
      bool hasTimezone = false;
      if (parameters != nullptr) {
        size_t start = 0;
        while (start < parameterLength) {
          if (parameters[start] == ';') {
            ++start;
            continue;
          }
          size_t end = start;
          while (end < parameterLength && parameters[end] != ';') {
            ++end;
          }
          const char *equals = static_cast<const char *>(memchr(parameters + start, '=', end - start));
          if (equals != nullptr &&
              equalsIgnoreCase(parameters + start, static_cast<size_t>(equals - (parameters + start)), "TZID")) {
            hasTimezone = true;
            break;
          }
          start = end + 1;
        }
      }
      if (hasTimezone && !londonTimezone && !utcTimezone) {
        unsupportedTimezone = true;
        return false;
      }
      if (utcTimezone) {
        epoch = calendarEpoch(date, static_cast<unsigned>(hour), static_cast<unsigned>(minute)) + second;
        allDay = false;
        return true;
      }

      allDay = false;
      if (utcSuffix) {
        epoch = calendarEpoch(date, static_cast<unsigned>(hour), static_cast<unsigned>(minute)) + second;
        return true;
      }
      const int64_t localEpoch = localDateTimeEpoch(date, static_cast<unsigned>(hour), static_cast<unsigned>(minute));
      if (localEpoch < 0) {
        return false;
      }
      epoch = localEpoch + second;
      return true;
    }

  } // namespace

  IcalendarParser::IcalendarParser(CalendarRange range, CalendarEvent *events, size_t capacity)
      : _range(range), _events(events), _capacity(capacity), _eventCount(0), _bytesRead(0), _physicalLength(0),
        _logicalLength(0), _insideCalendar(false), _calendarStarted(false), _calendarEnded(false), _insideEvent(false),
        _finished(false), _hasStart(false), _hasEnd(false), _cancelled(false), _hasRecurrence(false), _currentEvent{},
        _physicalLine{}, _logicalLine{}, _error(IcalendarError::None) {}

  bool IcalendarParser::write(const uint8_t *data, size_t length) {
    if (_error != IcalendarError::None || _finished || data == nullptr || (_events == nullptr && _capacity != 0)) {
      _error = IcalendarError::InvalidInput;
      return false;
    }
    if (length > kMaximumCalendarFeedBytes - _bytesRead) {
      _error = IcalendarError::FeedTooLarge;
      return false;
    }
    _bytesRead += length;
    for (size_t i = 0; i < length; ++i) {
      const char ch = static_cast<char>(data[i]);
      if (ch == '\n') {
        if (!processPhysicalLine()) {
          return false;
        }
        _physicalLength = 0;
      } else {
        if (_physicalLength + 1 >= sizeof(_physicalLine)) {
          _error = IcalendarError::MalformedFeed;
          return false;
        }
        _physicalLine[_physicalLength++] = ch;
      }
    }
    return true;
  }

  bool IcalendarParser::processPhysicalLine() {
    if (_physicalLength > 0 && _physicalLine[_physicalLength - 1] == '\r') {
      --_physicalLength;
    }
    _physicalLine[_physicalLength] = '\0';
    if (_physicalLength == 0) {
      return true;
    }

    if (_physicalLine[0] == ' ' || _physicalLine[0] == '\t') {
      const size_t continuationLength = _physicalLength - 1;
      if (_logicalLength + continuationLength >= sizeof(_logicalLine)) {
        _error = IcalendarError::MalformedFeed;
        return false;
      }
      memcpy(_logicalLine + _logicalLength, _physicalLine + 1, continuationLength);
      _logicalLength += continuationLength;
      _logicalLine[_logicalLength] = '\0';
      return true;
    }

    if (_logicalLength > 0 && !processLogicalLine()) {
      return false;
    }
    if (_physicalLength >= sizeof(_logicalLine)) {
      _error = IcalendarError::MalformedFeed;
      return false;
    }
    memcpy(_logicalLine, _physicalLine, _physicalLength + 1);
    _logicalLength = _physicalLength;
    return true;
  }

  bool IcalendarParser::processLogicalLine() {
    if (_logicalLength == 0) {
      return true;
    }
    _logicalLine[_logicalLength] = '\0';
    char *colon = strchr(_logicalLine, ':');
    if (colon == nullptr) {
      _error = IcalendarError::MalformedFeed;
      return false;
    }
    const size_t propertyLength = static_cast<size_t>(colon - _logicalLine);
    const char *value = colon + 1;
    const size_t valueLength = _logicalLength - propertyLength - 1;
    char *semicolon = static_cast<char *>(memchr(_logicalLine, ';', propertyLength));
    const size_t nameLength = semicolon == nullptr ? propertyLength : static_cast<size_t>(semicolon - _logicalLine);

    if (equalsIgnoreCase(_logicalLine, nameLength, "BEGIN") && equalsIgnoreCase(value, valueLength, "VCALENDAR")) {
      if (_calendarStarted || _calendarEnded || _insideEvent) {
        _error = IcalendarError::MalformedFeed;
        return false;
      }
      _insideCalendar = true;
      _calendarStarted = true;
    } else if (equalsIgnoreCase(_logicalLine, nameLength, "BEGIN") && equalsIgnoreCase(value, valueLength, "VEVENT")) {
      if (!_insideCalendar || _insideEvent || _calendarEnded) {
        _error = IcalendarError::MalformedFeed;
        return false;
      }
      memset(&_currentEvent, 0, sizeof(_currentEvent));
      _insideEvent = true;
      _hasStart = _hasEnd = _cancelled = _hasRecurrence = false;
    } else if (equalsIgnoreCase(_logicalLine, nameLength, "END") && equalsIgnoreCase(value, valueLength, "VEVENT")) {
      if (!_insideEvent || !finishEvent()) {
        if (_error == IcalendarError::None) {
          _error = IcalendarError::MalformedFeed;
        }
        return false;
      }
      _insideEvent = false;
    } else if (equalsIgnoreCase(_logicalLine, nameLength, "END") && equalsIgnoreCase(value, valueLength, "VCALENDAR")) {
      if (!_insideCalendar || _insideEvent) {
        _error = IcalendarError::MalformedFeed;
        return false;
      }
      _insideCalendar = false;
      _calendarEnded = true;
    } else if (_insideEvent) {
      if (equalsIgnoreCase(_logicalLine, nameLength, "UID")) {
        if (!copyField(_currentEvent.id, sizeof(_currentEvent.id), value, valueLength)) {
          _error = IcalendarError::MalformedFeed;
          return false;
        }
      } else if (equalsIgnoreCase(_logicalLine, nameLength, "SUMMARY")) {
        const size_t count =
          valueLength < sizeof(_currentEvent.title) - 1 ? valueLength : sizeof(_currentEvent.title) - 1;
        memcpy(_currentEvent.title, value, count);
        _currentEvent.title[count] = '\0';
        unescapeText(_currentEvent.title);
      } else if (equalsIgnoreCase(_logicalLine, nameLength, "STATUS")) {
        _cancelled = equalsIgnoreCase(value, valueLength, "CANCELLED");
      } else if (equalsIgnoreCase(_logicalLine, nameLength, "RRULE") ||
                 equalsIgnoreCase(_logicalLine, nameLength, "RDATE") ||
                 equalsIgnoreCase(_logicalLine, nameLength, "EXDATE")) {
        _hasRecurrence = true;
      } else if (equalsIgnoreCase(_logicalLine, nameLength, "DTSTART") ||
                 equalsIgnoreCase(_logicalLine, nameLength, "DTEND")) {
        bool allDay = false;
        CalendarDate date = {};
        int64_t epoch = 0;
        bool unsupportedTimezone = false;
        const char *parameters = semicolon == nullptr ? nullptr : semicolon + 1;
        const size_t parametersLength =
          semicolon == nullptr ? 0 : propertyLength - static_cast<size_t>(parameters - _logicalLine);
        if (!parseDateTimeProperty(value, valueLength, parameters, parametersLength, allDay, date, epoch,
                                   unsupportedTimezone)) {
          if (unsupportedTimezone) {
            _error = IcalendarError::UnsupportedTimezone;
          } else {
            _error = IcalendarError::MalformedFeed;
          }
          return false;
        }
        if (_hasStart || _hasEnd) {
          if (_currentEvent.allDay != allDay) {
            _error = IcalendarError::MalformedFeed;
            return false;
          }
        } else {
          _currentEvent.allDay = allDay;
        }
        const bool isStart = equalsIgnoreCase(_logicalLine, nameLength, "DTSTART");
        if (isStart) {
          _hasStart = true;
          if (allDay) {
            _currentEvent.allDayStartDate = date;
          } else {
            _currentEvent.startEpoch = epoch;
          }
        } else {
          _hasEnd = true;
          if (allDay) {
            _currentEvent.allDayEndDate = date;
          } else {
            _currentEvent.endEpoch = epoch;
          }
        }
      }
    }

    _logicalLength = 0;
    _logicalLine[0] = '\0';
    return true;
  }

  bool IcalendarParser::finishEvent() {
    if (_cancelled) {
      return true;
    }
    if (_hasRecurrence) {
      _error = IcalendarError::UnsupportedRecurrence;
      return false;
    }
    if (!_hasStart || !_hasEnd || _currentEvent.id[0] == '\0') {
      _error = IcalendarError::MalformedFeed;
      return false;
    }
    if (_currentEvent.allDay) {
      if (compareDates(_currentEvent.allDayEndDate, _currentEvent.allDayStartDate) <= 0) {
        _error = IcalendarError::MalformedFeed;
        return false;
      }
    } else if (_currentEvent.endEpoch <= _currentEvent.startEpoch) {
      _error = IcalendarError::MalformedFeed;
      return false;
    }

    CalendarEvent selected[kMaxEvents];
    if (selectEventsForRange(&_currentEvent, 1, _range, selected, kMaxEvents) == 0) {
      return true;
    }
    if (_eventCount >= _capacity) {
      _error = IcalendarError::TooManyEvents;
      return false;
    }
    _events[_eventCount++] = _currentEvent;
    return true;
  }

  bool IcalendarParser::finish() {
    if (_finished) {
      return _error == IcalendarError::None;
    }
    _finished = true;
    if (_physicalLength > 0 && !processPhysicalLine()) {
      return false;
    }
    _physicalLength = 0;
    if (_logicalLength > 0 && !processLogicalLine()) {
      return false;
    }
    if (_error != IcalendarError::None || !_calendarStarted || !_calendarEnded || _insideCalendar || _insideEvent) {
      if (_error == IcalendarError::None) {
        _error = IcalendarError::MalformedFeed;
      }
      return false;
    }

    CalendarEvent sorted[kMaxEvents];
    const size_t sortedCount = selectEventsForRange(_events, _eventCount, _range, sorted, _capacity);
    memcpy(_events, sorted, sortedCount * sizeof(CalendarEvent));
    _eventCount = sortedCount;
    return true;
  }

  size_t IcalendarParser::eventCount() const { return _eventCount; }

  IcalendarError IcalendarParser::error() const { return _error; }

} // namespace calendar
