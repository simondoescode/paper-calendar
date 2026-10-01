#include <calendar/mock_calendar_provider.h>

#include <string.h>

namespace calendar {
namespace {

void setText(char *destination, size_t destinationSize, const char *source) {
  if (destinationSize == 0) {
    return;
  }
  strncpy(destination, source, destinationSize - 1);
  destination[destinationSize - 1] = '\0';
}

CalendarEvent makeEvent(const char *id, const char *title, CalendarDate date, unsigned startHour,
                        unsigned startMinute, unsigned durationMinutes, bool allDay, const char *location = "") {
  CalendarEvent event = {};
  setText(event.id, sizeof(event.id), id);
  setText(event.title, sizeof(event.title), title);
  event.allDay = allDay;
  if (allDay) {
    event.allDayStartDate = date;
    event.allDayEndDate = calendarDateAddDays(date, 1);
  } else {
    event.startEpoch = localDateTimeEpoch(date, startHour, startMinute);
    event.endEpoch = event.startEpoch + static_cast<int64_t>(durationMinutes) * 60;
  }
  setText(event.source, sizeof(event.source), "Family");
  setText(event.location, sizeof(event.location), location);
  return event;
}

} // namespace

MockCalendarProvider::MockCalendarProvider(CalendarDate date) : _date(date) {}

size_t MockCalendarProvider::loadEvents(CalendarEvent *events, size_t capacity) const {
  if (events == nullptr || capacity == 0) {
    return 0;
  }

  const CalendarDate today = _date;
  const CalendarDate friday = calendarDateAddDays(_date, 1);
  const CalendarDate saturday = calendarDateAddDays(_date, 2);
  const CalendarDate monday = calendarDateAddDays(_date, 4);
  const CalendarEvent sample[] = {
    makeEvent("school-day", "School drop-off", today, 9, 0, 30, false),
    makeEvent("lunch", "Lunch", today, 12, 30, 60, false),
    makeEvent("isla-pickup", "Pick up Isla", today, 15, 15, 30, false),
    makeEvent("swimming", "Swimming", today, 18, 0, 60, false),
    makeEvent("dentist", "Dentist", friday, 10, 0, 60, false),
    makeEvent("birthday", "Birthday party with friends and family", saturday, 14, 0, 120, false),
    makeEvent("family-lunch", "Family lunch", monday, 11, 0, 90, false),
    makeEvent("school-closed", "School holiday", friday, 0, 0, 1440, true),
    makeEvent("trip", "Family trip (continues)", today, 20, 0, 1080, false),
    makeEvent("early-school-run", "Early school run", today, 9, 15, 45, false),
  };

  const size_t eventCount = sizeof(sample) / sizeof(sample[0]);
  const size_t count = eventCount < capacity ? eventCount : capacity;
  for (size_t i = 0; i < count; ++i) {
    events[i] = sample[i];
  }
  return count;
}

} // namespace calendar
