#include <calendar/calendar.h>
#include <calendar/calendar_config.h>
#include <calendar/calendar_renderer.h>
#include <calendar/host_runtime.h>
#include <calendar/icalendar_parser.h>
#include <calendar/mock_calendar_provider.h>
#include <calendar/settings.h>
#include <calendar/wake_reason.h>
#include <unity.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <cstdio>
#include <fstream>
#include <memory>

namespace {

calendar::CalendarEvent eventAt(const char *id, int64_t start, int64_t end, bool allDay = false) {
  calendar::CalendarEvent event = {};
  strncpy(event.id, id, sizeof(event.id) - 1);
  event.allDay = allDay;
  if (allDay) {
    event.allDayStartDate = calendar::calendarDateFromEpoch(start);
    event.allDayEndDate = calendar::calendarDateFromEpoch(end);
  } else {
    event.startEpoch = start;
    event.endEpoch = end;
  }
  return event;
}

void setLondonTimezoneForTest(void) {
#if defined(_WIN32)
  _putenv_s("TZ", calendar::kTimezoneRule);
  _tzset();
#else
  setenv("TZ", calendar::kTimezoneRule, 1);
  tzset();
#endif
}

} // namespace

void test_range_selection_sorts_events_by_start(void) {
  const calendar::CalendarRange range = {100, 500};
  const calendar::CalendarEvent events[] = {
    eventAt("later", 300, 340),
    eventAt("earlier", 120, 180),
    eventAt("middle", 200, 230),
  };
  calendar::CalendarEvent selected[3];

  const size_t count = calendar::selectEventsForRange(events, 3, range, selected, 3);

  TEST_ASSERT_EQUAL_UINT(3, count);
  TEST_ASSERT_EQUAL_STRING("earlier", selected[0].id);
  TEST_ASSERT_EQUAL_STRING("middle", selected[1].id);
  TEST_ASSERT_EQUAL_STRING("later", selected[2].id);
}

void test_range_selection_keeps_earliest_events_when_capacity_is_limited(void) {
  const calendar::CalendarRange range = {100, 500};
  const calendar::CalendarEvent events[] = {
    eventAt("latest", 400, 450),
    eventAt("earliest", 120, 150),
    eventAt("middle", 250, 300),
  };
  calendar::CalendarEvent selected[2];

  const size_t count = calendar::selectEventsForRange(events, 3, range, selected, 2);

  TEST_ASSERT_EQUAL_UINT(2, count);
  TEST_ASSERT_EQUAL_STRING("earliest", selected[0].id);
  TEST_ASSERT_EQUAL_STRING("middle", selected[1].id);
}

void test_today_range_includes_overlapping_events(void) {
  const calendar::CalendarDate today = {2026, 10, 1};
  const calendar::CalendarRange range = calendar::calendarTodayRange(today);
  const calendar::CalendarEvent events[] = {
    eventAt("previous-day", range.startEpoch - 3600, range.startEpoch + 3600),
    eventAt("today", range.startEpoch + 3600, range.startEpoch + 7200),
    eventAt("next-day", range.endEpoch, range.endEpoch + 3600),
  };
  calendar::CalendarEvent selected[3];

  const size_t count = calendar::selectEventsForRange(events, 3, range, selected, 3);

  TEST_ASSERT_EQUAL_UINT(2, count);
  TEST_ASSERT_EQUAL_STRING("previous-day", selected[0].id);
  TEST_ASSERT_EQUAL_STRING("today", selected[1].id);
}

void test_rest_of_week_excludes_today_and_ends_at_next_monday(void) {
  const calendar::CalendarDate thursday = {2026, 10, 1};
  const calendar::CalendarRange today = calendar::calendarTodayRange(thursday);
  const calendar::CalendarRange week = calendar::calendarRestOfWeekRange(thursday);
  const calendar::CalendarEvent events[] = {
    eventAt("today", today.startEpoch + 3600, today.startEpoch + 7200),
    eventAt("friday", today.startEpoch + calendar::kSecondsPerDay, today.startEpoch + calendar::kSecondsPerDay + 3600),
    eventAt("sunday", week.endEpoch - calendar::kSecondsPerDay, week.endEpoch - 60),
    eventAt("monday", week.endEpoch, week.endEpoch + 3600),
  };
  calendar::CalendarEvent selected[4];

  const size_t count = calendar::selectEventsForRange(events, 4, week, selected, 4);

  TEST_ASSERT_EQUAL_UINT(2, count);
  TEST_ASSERT_EQUAL_STRING("friday", selected[0].id);
  TEST_ASSERT_EQUAL_STRING("sunday", selected[1].id);
  TEST_ASSERT_EQUAL_INT64(calendar::localDateTimeEpoch({2026, 10, 5}), week.endEpoch);
}

void test_calendar_dates_around_london_dst_change_are_consistent(void) {
  const calendar::CalendarDate beforeSpringChange = {2026, 3, 28};
  const calendar::CalendarDate springChange = {2026, 3, 29};
  const calendar::CalendarDate beforeAutumnChange = {2026, 10, 24};
  const calendar::CalendarDate autumnChange = {2026, 10, 25};

  TEST_ASSERT_EQUAL_INT64(calendar::kSecondsPerDay,
                          calendar::calendarEpoch(springChange) - calendar::calendarEpoch(beforeSpringChange));
  TEST_ASSERT_EQUAL_INT64(calendar::kSecondsPerDay,
                          calendar::calendarEpoch(autumnChange) - calendar::calendarEpoch(beforeAutumnChange));
  TEST_ASSERT_EQUAL_UINT(0, calendar::calendarWeekday(springChange));
  TEST_ASSERT_EQUAL_UINT(0, calendar::calendarWeekday(autumnChange));
  TEST_ASSERT_EQUAL_UINT(29, calendar::calendarDateFromEpoch(calendar::calendarEpoch({2028, 2, 29})).day);
}

void test_saturday_and_sunday_week_boundaries(void) {
  const calendar::CalendarRange saturdayRange = calendar::calendarRestOfWeekRange({2026, 10, 3});
  const calendar::CalendarRange sundayRange = calendar::calendarRestOfWeekRange({2026, 10, 4});

  TEST_ASSERT_EQUAL_INT64(calendar::localDateTimeEpoch({2026, 10, 4}), saturdayRange.startEpoch);
  TEST_ASSERT_EQUAL_INT64(calendar::localDateTimeEpoch({2026, 10, 5}), saturdayRange.endEpoch);
  TEST_ASSERT_EQUAL_INT64(sundayRange.startEpoch, sundayRange.endEpoch);
}

void test_month_year_and_leap_day_boundaries(void) {
  TEST_ASSERT_EQUAL_UINT(1,
                         calendar::calendarDateFromEpoch(calendar::calendarEpoch({2026, 1, 31}) +
                                                         calendar::kSecondsPerDay)
                           .day);
  TEST_ASSERT_EQUAL_UINT(2,
                         calendar::calendarDateFromEpoch(calendar::calendarEpoch({2026, 1, 31}) +
                                                         calendar::kSecondsPerDay)
                           .month);
  const calendar::CalendarDate newYear =
    calendar::calendarDateFromEpoch(calendar::calendarEpoch({2026, 12, 31}) + calendar::kSecondsPerDay);
  TEST_ASSERT_EQUAL_INT(2027, newYear.year);
  TEST_ASSERT_EQUAL_UINT(1, newYear.month);
  TEST_ASSERT_EQUAL_UINT(1, newYear.day);
  TEST_ASSERT_EQUAL_UINT(29,
                         calendar::calendarDateFromEpoch(calendar::calendarEpoch({2024, 2, 28}) +
                                                         calendar::kSecondsPerDay)
                           .day);
  TEST_ASSERT_EQUAL_UINT(3,
                         calendar::calendarDateFromEpoch(calendar::calendarEpoch({2023, 2, 28}) +
                                                         calendar::kSecondsPerDay)
                           .month);
}

void test_london_timezone_uses_bst_and_gmt_transition_rules(void) {
  setLondonTimezoneForTest();
  calendar::CalendarDate localDate = {};
  unsigned hour = 0;
  unsigned minute = 0;

  TEST_ASSERT_TRUE(calendar::localTimeFromEpoch(calendar::calendarEpoch({2026, 3, 29}, 0, 59), localDate, hour,
                                                minute));
  TEST_ASSERT_EQUAL_UINT(0, hour);
  TEST_ASSERT_EQUAL_UINT(59, minute);
  TEST_ASSERT_TRUE(calendar::localTimeFromEpoch(calendar::calendarEpoch({2026, 3, 29}, 1, 0), localDate, hour,
                                                minute));
  TEST_ASSERT_EQUAL_UINT(2, hour);
  TEST_ASSERT_EQUAL_UINT(0, minute);
  TEST_ASSERT_TRUE(calendar::localTimeFromEpoch(calendar::calendarEpoch({2026, 10, 25}, 0, 59), localDate, hour,
                                                minute));
  TEST_ASSERT_EQUAL_UINT(1, hour);
  TEST_ASSERT_EQUAL_UINT(59, minute);
  TEST_ASSERT_TRUE(calendar::localTimeFromEpoch(calendar::calendarEpoch({2026, 10, 25}, 1, 0), localDate, hour,
                                                minute));
  TEST_ASSERT_EQUAL_UINT(1, hour);
  TEST_ASSERT_EQUAL_UINT(0, minute);
  TEST_ASSERT_EQUAL_INT64(-1, calendar::localDateTimeEpoch({2026, 3, 29}, 1, 30));
  TEST_ASSERT_EQUAL_INT64(-1, calendar::localDateTimeEpoch({2026, 10, 25}, 1, 30));
}

void test_mock_events_are_generated_relative_to_injected_date(void) {
  const calendar::CalendarDate injectedDate = {2031, 12, 30};
  const int64_t injectedDay = calendar::localDateTimeEpoch(injectedDate);
  calendar::MockCalendarProvider provider(injectedDate);
  calendar::CalendarEvent events[calendar::kMaxEvents] = {};

  const size_t count = provider.loadEvents(events, calendar::kMaxEvents);

  TEST_ASSERT_EQUAL_UINT(10, count);
  TEST_ASSERT_EQUAL_INT64(injectedDay + 9 * 3600, events[0].startEpoch);
  TEST_ASSERT_EQUAL_INT64(calendar::localDateTimeEpoch({2031, 12, 31}, 10), events[4].startEpoch);
  TEST_ASSERT_EQUAL_INT64(calendar::localDateTimeEpoch({2032, 1, 3}, 11), events[6].startEpoch);
  TEST_ASSERT_TRUE(events[7].allDay);
  TEST_ASSERT_EQUAL_INT(2031, events[7].allDayStartDate.year);
  TEST_ASSERT_EQUAL_UINT(12, events[7].allDayStartDate.month);
  TEST_ASSERT_EQUAL_UINT(31, events[7].allDayStartDate.day);
}

void test_display_state_hash_is_stable_and_tracks_visible_content(void) {
  const calendar::CalendarDate date = {2026, 10, 1};
  const int64_t today = calendar::localDateTimeEpoch(date);
  calendar::CalendarEvent events[] = {
    eventAt("today-event", today + 9 * 3600, today + 10 * 3600),
    eventAt("next-week", calendar::localDateTimeEpoch({2026, 10, 5}),
            calendar::localDateTimeEpoch({2026, 10, 5}) + 3600),
  };
  const uint32_t original =
    calendar::displayStateHash(calendar::DisplayStatus::Calendar, date, events, 2, 41);

  TEST_ASSERT_EQUAL_UINT32(original,
                           calendar::displayStateHash(calendar::DisplayStatus::Calendar, date, events, 2, 41));
  events[1].title[0] = 'X';
  TEST_ASSERT_EQUAL_UINT32(original,
                           calendar::displayStateHash(calendar::DisplayStatus::Calendar, date, events, 2, 41));
  events[0].title[0] = 'X';
  TEST_ASSERT_NOT_EQUAL(original,
                        calendar::displayStateHash(calendar::DisplayStatus::Calendar, date, events, 2, 41));
  TEST_ASSERT_NOT_EQUAL(original,
                        calendar::displayStateHash(calendar::DisplayStatus::Calendar, {2026, 10, 2}, events, 2, 41));
  TEST_ASSERT_NOT_EQUAL(original,
                        calendar::displayStateHash(calendar::DisplayStatus::WifiConnectionFailed, date, nullptr, 0, -1));
}

void test_wake_reason_classifies_manual_timer_and_cold_boot(void) {
  TEST_ASSERT_TRUE(calendar::classifyWakeReason(false, true) == calendar::WakeReason::Key3);
  TEST_ASSERT_TRUE(calendar::classifyWakeReason(true, false) == calendar::WakeReason::Timer);
  TEST_ASSERT_TRUE(calendar::classifyWakeReason(false, false) == calendar::WakeReason::ColdBootOrReset);
}

void test_manual_refresh_reloads_events_without_forcing_unchanged_redraw(void) {
  const calendar::CalendarDate date = {2026, 10, 1};
  calendar::MockCalendarProvider provider(date);
  calendar::CalendarEvent firstLoad[calendar::kMaxEvents] = {};
  calendar::CalendarEvent secondLoad[calendar::kMaxEvents] = {};

  TEST_ASSERT_TRUE(calendar::classifyWakeReason(false, true) == calendar::WakeReason::Key3);
  const size_t firstCount = provider.loadEvents(firstLoad, calendar::kMaxEvents);
  const size_t secondCount = provider.loadEvents(secondLoad, calendar::kMaxEvents);
  TEST_ASSERT_EQUAL_UINT(firstCount, secondCount);
  TEST_ASSERT_EQUAL_UINT32(
    calendar::displayStateHash(calendar::DisplayStatus::Calendar, date, firstLoad, firstCount, 41),
    calendar::displayStateHash(calendar::DisplayStatus::Calendar, date, secondLoad, secondCount, 41));
}

void test_all_day_and_timed_events_format_correctly(void) {
  const int64_t dayStart = calendar::calendarEpoch({2026, 10, 1});
  const calendar::CalendarEvent allDay = eventAt("all-day", dayStart, dayStart + calendar::kSecondsPerDay, true);
  const calendar::CalendarEvent timed =
    eventAt("timed", calendar::localDateTimeEpoch({2026, 10, 1}, 9), calendar::localDateTimeEpoch({2026, 10, 1}, 10));
  char formatted[12];

  calendar::formatEventTime(allDay, calendar::localDateTimeEpoch({2026, 10, 1}), formatted, sizeof(formatted));
  TEST_ASSERT_EQUAL_STRING("ALL DAY", formatted);
  calendar::formatEventTime(timed, calendar::localDateTimeEpoch({2026, 10, 1}), formatted, sizeof(formatted));
  TEST_ASSERT_EQUAL_STRING("09:00", formatted);
}

void test_multiday_event_is_marked_as_continuing_on_later_day(void) {
  const int64_t dayStart = calendar::localDateTimeEpoch({2026, 10, 2});
  const calendar::CalendarEvent event =
    eventAt("overnight", calendar::localDateTimeEpoch({2026, 10, 1}, 23), dayStart + 3600);
  char formatted[12];

  calendar::formatEventTime(event, dayStart, formatted, sizeof(formatted));

  TEST_ASSERT_EQUAL_STRING("CONT.", formatted);
}

void test_long_titles_are_truncated_without_overflow(void) {
  char title[16];

  calendar::truncateTitle("A very long family calendar event", title, sizeof(title), 12);

  TEST_ASSERT_EQUAL_STRING("A very lo...", title);
  TEST_ASSERT_EQUAL_UINT(12, strlen(title));
}

void test_icalendar_parser_keeps_all_day_dates_without_epoch_conversion(void) {
  const calendar::CalendarDate today = {2026, 10, 1};
  const calendar::CalendarRange range = calendar::calendarTodayRange(today);
  calendar::CalendarEvent events[calendar::kMaxEvents] = {};
  calendar::IcalendarParser parser(range, events, calendar::kMaxEvents);
  const char feed[] =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:holiday-uid-1234567890123456789012345678901234567890\r\n"
    "DTSTART;VALUE=DATE:20261001\r\nDTEND;VALUE=DATE:20261003\r\nSUMMARY:School holiday\r\nEND:VEVENT\r\n"
    "END:VCALENDAR\r\n";

  TEST_ASSERT_TRUE(parser.write(reinterpret_cast<const uint8_t *>(feed), sizeof(feed) - 1));
  TEST_ASSERT_TRUE(parser.finish());
  TEST_ASSERT_EQUAL_UINT(1, parser.eventCount());
  TEST_ASSERT_TRUE(events[0].allDay);
  TEST_ASSERT_EQUAL_INT(2026, events[0].allDayStartDate.year);
  TEST_ASSERT_EQUAL_UINT(10, events[0].allDayStartDate.month);
  TEST_ASSERT_EQUAL_UINT(1, events[0].allDayStartDate.day);
  TEST_ASSERT_EQUAL_UINT(3, events[0].allDayEndDate.day);
  TEST_ASSERT_EQUAL_INT64(0, events[0].startEpoch);
}

void test_icalendar_parser_unfolds_lines_and_converts_london_wall_time(void) {
  const calendar::CalendarDate today = {2026, 10, 1};
  const calendar::CalendarRange range = calendar::calendarTodayRange(today);
  calendar::CalendarEvent events[calendar::kMaxEvents] = {};
  calendar::IcalendarParser parser(range, events, calendar::kMaxEvents);
  const char feed[] =
    "BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:school-pickup-uid\n"
    "DTSTART;TZID=Europe/London:20261001T151500\n"
    "DTEND;TZID=Europe/London:20261001T154500\n"
    "SUMMARY:School pickup with an exceptionally long\n title\nEND:VEVENT\nEND:VCALENDAR\n";

  TEST_ASSERT_TRUE(parser.write(reinterpret_cast<const uint8_t *>(feed), sizeof(feed) - 1));
  TEST_ASSERT_TRUE(parser.finish());
  TEST_ASSERT_EQUAL_UINT(1, parser.eventCount());
  TEST_ASSERT_EQUAL_STRING("School pickup with an exceptionally longtitle", events[0].title);
  TEST_ASSERT_EQUAL_INT64(calendar::localDateTimeEpoch(today, 15, 15), events[0].startEpoch);
  TEST_ASSERT_EQUAL_INT64(calendar::localDateTimeEpoch(today, 15, 45), events[0].endEpoch);
}

void test_icalendar_parser_converts_utc_events_to_london_display_time(void) {
  const calendar::CalendarDate today = {2026, 10, 1};
  const calendar::CalendarRange range = calendar::calendarTodayRange(today);
  calendar::CalendarEvent events[calendar::kMaxEvents] = {};
  calendar::IcalendarParser parser(range, events, calendar::kMaxEvents);
  const char feed[] =
    "BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:utc-event\nDTSTART:20261001T141500Z\n"
    "DTEND:20261001T144500Z\nSUMMARY:UTC source event\nEND:VEVENT\nEND:VCALENDAR\n";

  TEST_ASSERT_TRUE(parser.write(reinterpret_cast<const uint8_t *>(feed), sizeof(feed) - 1));
  TEST_ASSERT_TRUE(parser.finish());
  TEST_ASSERT_EQUAL_UINT(1, parser.eventCount());
  TEST_ASSERT_EQUAL_INT64(calendar::calendarEpoch(today, 14, 15), events[0].startEpoch);
  char formatted[12];
  calendar::formatEventTime(events[0], range.startEpoch, formatted, sizeof(formatted));
  TEST_ASSERT_EQUAL_STRING("15:15", formatted);
}

void test_icalendar_parser_rejects_unsupported_timezone_and_recurrence_explicitly(void) {
  const calendar::CalendarRange range = calendar::calendarTodayRange({2026, 10, 1});
  const char *feeds[] = {
    "BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:timezone\nDTSTART;TZID=America/New_York:20261001T100000\n"
    "DTEND;TZID=America/New_York:20261001T110000\nSUMMARY:Unsupported zone\nEND:VEVENT\nEND:VCALENDAR\n",
    "BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:repeating\nDTSTART;TZID=Europe/London:20261001T100000\n"
    "DTEND;TZID=Europe/London:20261001T110000\nRRULE:FREQ=WEEKLY\nSUMMARY:Repeating\n"
    "END:VEVENT\nEND:VCALENDAR\n",
  };
  const calendar::IcalendarError expected[] = {
    calendar::IcalendarError::UnsupportedTimezone,
    calendar::IcalendarError::UnsupportedRecurrence,
  };

  for (size_t i = 0; i < 2; ++i) {
    calendar::CalendarEvent events[calendar::kMaxEvents] = {};
    calendar::IcalendarParser parser(range, events, calendar::kMaxEvents);
    const size_t length = strlen(feeds[i]);
    parser.write(reinterpret_cast<const uint8_t *>(feeds[i]), length);
    TEST_ASSERT_FALSE(parser.finish());
    TEST_ASSERT_TRUE(parser.error() == expected[i]);
  }
}

void test_icalendar_parser_rejects_over_capacity_visible_events(void) {
  const calendar::CalendarDate today = {2026, 10, 1};
  const calendar::CalendarRange range = calendar::calendarTodayRange(today);
  calendar::CalendarEvent events[1] = {};
  calendar::IcalendarParser parser(range, events, 1);
  const char feed[] =
    "BEGIN:VCALENDAR\n"
    "BEGIN:VEVENT\nUID:first\nDTSTART;TZID=Europe/London:20261001T090000\nDTEND;TZID=Europe/London:20261001T093000\n"
    "SUMMARY:First\nEND:VEVENT\n"
    "BEGIN:VEVENT\nUID:second\nDTSTART;TZID=Europe/London:20261001T100000\nDTEND;TZID=Europe/London:20261001T103000\n"
    "SUMMARY:Second\nEND:VEVENT\nEND:VCALENDAR\n";

  parser.write(reinterpret_cast<const uint8_t *>(feed), sizeof(feed) - 1);
  TEST_ASSERT_FALSE(parser.finish());
  TEST_ASSERT_TRUE(parser.error() == calendar::IcalendarError::TooManyEvents);
}

void test_host_settings_default_and_persistence_round_trip(void) {
  const char *path = "calendar-settings-test.json";
  remove(path);
  std::unique_ptr<calendar::ConfigStore> store(calendar::createHostConfigStore(path));
  calendar::CalendarSettings settings = {};
  TEST_ASSERT_TRUE(store->load(settings));
  TEST_ASSERT_EQUAL_STRING(calendar::kDefaultFixtureUrl, settings.calendarUrl);
  TEST_ASSERT_EQUAL_UINT(calendar::kDefaultRefreshIntervalSeconds, settings.refreshIntervalSeconds);
  TEST_ASSERT_EQUAL_STRING("Europe/London", settings.timezone);

  strncpy(settings.calendarUrl, "file:///tmp/family.ics", sizeof(settings.calendarUrl) - 1);
  settings.refreshIntervalSeconds = 300;
  strncpy(settings.timezone, "UTC", sizeof(settings.timezone) - 1);
  TEST_ASSERT_TRUE(store->save(settings));
  store.reset(calendar::createHostConfigStore(path));
  calendar::CalendarSettings reloaded = {};
  TEST_ASSERT_TRUE(store->load(reloaded));
  TEST_ASSERT_EQUAL_STRING("file:///tmp/family.ics", reloaded.calendarUrl);
  TEST_ASSERT_EQUAL_UINT(300, reloaded.refreshIntervalSeconds);
  TEST_ASSERT_EQUAL_STRING("UTC", reloaded.timezone);
  remove(path);
}

void test_host_settings_form_validates_and_persists_portal_submission(void) {
  const char *path = "calendar-settings-form-test.json";
  remove(path);
  std::unique_ptr<calendar::ConfigStore> store(calendar::createHostConfigStore(path));
  calendar::CalendarSettings settings = calendar::defaultCalendarSettings();
  char error[160];
  const char form[] = "calendar_url=fixture%3A%2F%2Fdefault&refresh_interval=180&timezone=UTC";
  TEST_ASSERT_TRUE(calendar::applySettingsForm(form, settings, error, sizeof(error)));
  TEST_ASSERT_TRUE(store->save(settings));

  calendar::CalendarSettings invalid = settings;
  const char invalidForm[] = "calendar_url=http%3A%2F%2Fexample.com%2Ffeed.ics&refresh_interval=30&timezone=Mars%2FOlympus";
  TEST_ASSERT_FALSE(calendar::applySettingsForm(invalidForm, invalid, error, sizeof(error)));
  TEST_ASSERT_TRUE(strlen(error) > 0);
  calendar::CalendarSettings reloaded = {};
  TEST_ASSERT_TRUE(store->load(reloaded));
  TEST_ASSERT_EQUAL_UINT(180, reloaded.refreshIntervalSeconds);
  TEST_ASSERT_EQUAL_STRING("UTC", reloaded.timezone);
  remove(path);
}

void test_host_settings_malformed_file_fails_with_safe_defaults(void) {
  const char *path = "calendar-settings-corrupt-test.json";
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "{ definitely not json";
  }
  std::unique_ptr<calendar::ConfigStore> store(calendar::createHostConfigStore(path));
  calendar::CalendarSettings settings = {};
  TEST_ASSERT_FALSE(store->load(settings));
  TEST_ASSERT_EQUAL_STRING("", settings.calendarUrl);
  TEST_ASSERT_EQUAL_UINT(calendar::kDefaultRefreshIntervalSeconds, settings.refreshIntervalSeconds);
  remove(path);
}

void test_host_fixture_provider_renders_shared_800_by_480_png(void) {
  const calendar::CalendarDate today = {2026, 10, 1};
  calendar::CalendarSettings settings = calendar::defaultCalendarSettings();
  strncpy(settings.calendarUrl, calendar::kDefaultFixtureUrl, sizeof(settings.calendarUrl) - 1);
  calendar::HostCalendarProvider provider(settings, today);
  calendar::CalendarEvent events[calendar::kMaxEvents] = {};
  const size_t count = provider.loadEvents(events, calendar::kMaxEvents);
  TEST_ASSERT_EQUAL_STRING("", provider.error());
  TEST_ASSERT_TRUE(count > 0);

  const char *path = "calendar-preview-test.png";
  calendar::HostDisplayTarget display(path);
  TEST_ASSERT_TRUE(calendar::renderCalendar(display, events, count, today, "test", "calendar-host", -1));
  std::ifstream file(path, std::ios::binary);
  unsigned char header[25] = {};
  file.read(reinterpret_cast<char *>(header), sizeof(header));
  TEST_ASSERT_TRUE(file.good() || file.eof());
  TEST_ASSERT_EQUAL_UINT8(137, header[0]);
  TEST_ASSERT_EQUAL_UINT8('P', header[1]);
  TEST_ASSERT_EQUAL_UINT8('N', header[2]);
  TEST_ASSERT_EQUAL_UINT8('G', header[3]);
  TEST_ASSERT_EQUAL_UINT(800, (static_cast<uint32_t>(header[16]) << 24) |
                                (static_cast<uint32_t>(header[17]) << 16) |
                                (static_cast<uint32_t>(header[18]) << 8) | header[19]);
  TEST_ASSERT_EQUAL_UINT(480, (static_cast<uint32_t>(header[20]) << 24) |
                                (static_cast<uint32_t>(header[21]) << 16) |
                                (static_cast<uint32_t>(header[22]) << 8) | header[23]);
  TEST_ASSERT_EQUAL_UINT8(1, header[24]);
  file.close();
  remove(path);
}

void test_host_provider_error_and_missing_configuration_are_clean(void) {
  calendar::CalendarSettings settings = calendar::defaultCalendarSettings();
  strncpy(settings.calendarUrl, "https://calendar.example/family.ics", sizeof(settings.calendarUrl) - 1);
  calendar::HostCalendarProvider remoteProvider(settings, {2026, 10, 1});
  calendar::CalendarEvent events[calendar::kMaxEvents] = {};
  TEST_ASSERT_EQUAL_UINT(0, remoteProvider.loadEvents(events, calendar::kMaxEvents));
  TEST_ASSERT_TRUE(strlen(remoteProvider.error()) > 0);

  settings.calendarUrl[0] = '\0';
  calendar::HostCalendarProvider missingProvider(settings, {2026, 10, 1});
  TEST_ASSERT_EQUAL_UINT(0, missingProvider.loadEvents(events, calendar::kMaxEvents));
  TEST_ASSERT_EQUAL_STRING("Calendar URL is not configured.", missingProvider.error());
}

void setUp(void) { setLondonTimezoneForTest(); }
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_range_selection_sorts_events_by_start);
  RUN_TEST(test_range_selection_keeps_earliest_events_when_capacity_is_limited);
  RUN_TEST(test_today_range_includes_overlapping_events);
  RUN_TEST(test_rest_of_week_excludes_today_and_ends_at_next_monday);
  RUN_TEST(test_calendar_dates_around_london_dst_change_are_consistent);
  RUN_TEST(test_saturday_and_sunday_week_boundaries);
  RUN_TEST(test_month_year_and_leap_day_boundaries);
  RUN_TEST(test_london_timezone_uses_bst_and_gmt_transition_rules);
  RUN_TEST(test_mock_events_are_generated_relative_to_injected_date);
  RUN_TEST(test_display_state_hash_is_stable_and_tracks_visible_content);
  RUN_TEST(test_wake_reason_classifies_manual_timer_and_cold_boot);
  RUN_TEST(test_manual_refresh_reloads_events_without_forcing_unchanged_redraw);
  RUN_TEST(test_all_day_and_timed_events_format_correctly);
  RUN_TEST(test_multiday_event_is_marked_as_continuing_on_later_day);
  RUN_TEST(test_long_titles_are_truncated_without_overflow);
  RUN_TEST(test_icalendar_parser_keeps_all_day_dates_without_epoch_conversion);
  RUN_TEST(test_icalendar_parser_unfolds_lines_and_converts_london_wall_time);
  RUN_TEST(test_icalendar_parser_converts_utc_events_to_london_display_time);
  RUN_TEST(test_icalendar_parser_rejects_unsupported_timezone_and_recurrence_explicitly);
  RUN_TEST(test_icalendar_parser_rejects_over_capacity_visible_events);
  RUN_TEST(test_host_settings_default_and_persistence_round_trip);
  RUN_TEST(test_host_settings_form_validates_and_persists_portal_submission);
  RUN_TEST(test_host_settings_malformed_file_fails_with_safe_defaults);
  RUN_TEST(test_host_fixture_provider_renders_shared_800_by_480_png);
  RUN_TEST(test_host_provider_error_and_missing_configuration_are_clean);
  return UNITY_END();
}
