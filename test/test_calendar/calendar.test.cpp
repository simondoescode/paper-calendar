#include <calendar/calendar.h>
#include <calendar/calendar_config.h>
#include <calendar/calendar_renderer.h>
#include <calendar/host_runtime.h>
#include <calendar/icalendar_parser.h>
#include <calendar/mock_calendar_provider.h>
#include <calendar/settings.h>
#include <calendar/wake_reason.h>
#include <calendar/weather_icons.h>
#include <calendar/weather_client.h>
#include <unity.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>

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

std::string weatherFixture() {
  std::ifstream file("test/fixtures/open_meteo.json");
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
calendar::WeatherData parsedWeatherFixture() {
  const std::string json = weatherFixture();
  calendar::WeatherData weather = {};
  TEST_ASSERT_TRUE(calendar::parseWeatherResponse(json.c_str(), json.size(), weather, 123));
  return weather;
}
void test_weather_wmo_mapping_and_url() {
  using calendar::WeatherCondition;
  const int codes[] = {0,1,2,3,45,48,51,53,55,56,57,61,63,66,65,67,80,81,82,71,73,75,77,85,86,95,96,99,-1,1234};
  const WeatherCondition expected[] = {WeatherCondition::Clear,WeatherCondition::PartlyCloudy,WeatherCondition::PartlyCloudy,WeatherCondition::Cloudy,
    WeatherCondition::Fog,WeatherCondition::Fog,WeatherCondition::Drizzle,WeatherCondition::Drizzle,WeatherCondition::Drizzle,WeatherCondition::Drizzle,WeatherCondition::Drizzle,
    WeatherCondition::Rain,WeatherCondition::Rain,WeatherCondition::Rain,WeatherCondition::HeavyRain,WeatherCondition::HeavyRain,WeatherCondition::HeavyRain,WeatherCondition::HeavyRain,WeatherCondition::HeavyRain,
    WeatherCondition::Snow,WeatherCondition::Snow,WeatherCondition::Snow,WeatherCondition::Snow,WeatherCondition::Snow,WeatherCondition::Snow,
    WeatherCondition::Thunderstorm,WeatherCondition::Thunderstorm,WeatherCondition::Thunderstorm,WeatherCondition::Unknown,WeatherCondition::Unknown};
  for (size_t i = 0; i < sizeof(codes)/sizeof(codes[0]); ++i) TEST_ASSERT_EQUAL_INT(static_cast<int>(expected[i]), static_cast<int>(calendar::weatherConditionFromWmoCode(codes[i])));
  char url[512];
  TEST_ASSERT_TRUE(calendar::buildWeatherUrl(url, sizeof(url), 51.5074, -0.1278));
  TEST_ASSERT_NOT_NULL(strstr(url, "latitude=51.507400&longitude=-0.127800"));
  TEST_ASSERT_NOT_NULL(strstr(url, "timezone=auto&forecast_days=3"));
  TEST_ASSERT_FALSE(calendar::buildWeatherUrl(url, sizeof(url), 91, 0));
  TEST_ASSERT_FALSE(calendar::buildWeatherUrl(url, 10, 0, 0));
}
void test_weather_parser_preserves_cache_on_invalid_response() {
  auto weather = parsedWeatherFixture();
  TEST_ASSERT_TRUE(weather.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 14.2, weather.current.temperatureC);
  TEST_ASSERT_EQUAL_STRING("07:01", weather.forecast[0].sunrise);
  TEST_ASSERT_EQUAL_STRING("18:33", weather.forecast[2].sunset);
  TEST_ASSERT_EQUAL_UINT32(123, weather.fetchedAt);
  const auto before = weather;
  std::string json = weatherFixture();
  for (const char *bad : {"{", "{}", "{\"current\":null}", "[]"}) {
    TEST_ASSERT_FALSE(calendar::parseWeatherResponse(bad, strlen(bad), weather, 456));
    TEST_ASSERT_EQUAL_MEMORY(&before, &weather, sizeof(weather));
  }
  const char *fields[] = {"temperature_2m", "apparent_temperature", "wind_speed_10m", "weather_code", "is_day", "time", "temperature_2m_max", "temperature_2m_min", "precipitation_probability_max", "sunrise", "sunset"};
  for (const char *field : fields) {
    std::string bad = json;
    const std::string key = std::string("\"") + field + "\"";
    bad.replace(bad.find(key), key.size(), "\"missing\"");
    TEST_ASSERT_FALSE(calendar::parseWeatherResponse(bad.c_str(), bad.size(), weather, 456));
    TEST_ASSERT_EQUAL_MEMORY(&before, &weather, sizeof(weather));
  }
  TEST_ASSERT_FALSE(calendar::parseWeatherResponse(json.c_str(), json.find_last_of('}'), weather, 456));
  const std::string oversized(4097, ' ');
  TEST_ASSERT_FALSE(calendar::parseWeatherResponse(oversized.c_str(), oversized.size(), weather, 456));
  const char *original[] = {"\"is_day\":1", "\"temperature_2m\":14.2", "\"weather_code\":[2,61,0]",
                            "\"precipitation_probability_max\":[20,80,5]", "2026-10-01T07:01", "2026-10-03"};
  const char *replacement[] = {"\"is_day\":2", "\"temperature_2m\":null", "\"weather_code\":[2,null,0]",
                               "\"precipitation_probability_max\":[101,80,5]", "2026-10-01T25:01", "2026-02-30"};
  for (size_t i = 0; i < sizeof(original)/sizeof(original[0]); ++i) {
    std::string bad = json;
    bad.replace(bad.find(original[i]), strlen(original[i]), replacement[i]);
    TEST_ASSERT_FALSE(calendar::parseWeatherResponse(bad.c_str(), bad.size(), weather, 456));
    TEST_ASSERT_EQUAL_MEMORY(&before, &weather, sizeof(weather));
  }
  std::string unknown = json;
  unknown.replace(unknown.find("\"weather_code\":2"), 16, "\"weather_code\":1234");
  calendar::WeatherData unknownWeather = {};
  TEST_ASSERT_TRUE(calendar::parseWeatherResponse(unknown.c_str(), unknown.size(), unknownWeather, 456));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(calendar::WeatherCondition::Unknown), static_cast<int>(unknownWeather.current.condition));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(calendar::WeatherIcon::Cloudy), static_cast<int>(calendar::getWeatherIcon(unknownWeather.current.condition, true)));
  std::string night = json;
  night.replace(night.find("\"is_day\":1"), 10, "\"is_day\":0");
  TEST_ASSERT_TRUE(calendar::parseWeatherResponse(night.c_str(), night.size(), weather, 456));
  TEST_ASSERT_FALSE(weather.current.isDay);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(calendar::WeatherIcon::PartlyCloudyNight), static_cast<int>(calendar::getWeatherIcon(weather.current.condition, weather.current.isDay)));
}
void test_weather_hash_tracks_visible_values() {
  auto weather = parsedWeatherFixture();
  const auto hash = calendar::weatherDisplayHash(weather);
  weather.fetchedAt++;
  weather.current.windSpeedKmh++;
  TEST_ASSERT_EQUAL_UINT32(hash, calendar::weatherDisplayHash(weather));
  weather.current.isDay = false;
  TEST_ASSERT_NOT_EQUAL(hash, calendar::weatherDisplayHash(weather));
  weather.valid = false;
  TEST_ASSERT_NOT_EQUAL(hash, calendar::weatherDisplayHash(weather));
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
  class BoundsCheckedDisplay : public calendar::HostDisplayTarget {
  public:
    explicit BoundsCheckedDisplay(const char *filePath) : HostDisplayTarget(filePath) {}
    void bitmap(uint16_t x, uint16_t y, const uint8_t *data, uint16_t width, uint16_t height,
                calendar::DisplayColor color) override {
      TEST_ASSERT_TRUE(x + width <= 800);
      TEST_ASSERT_TRUE(y + height <= 480);
      HostDisplayTarget::bitmap(x, y, data, width, height, color);
      ++bitmapCount;
    }
    unsigned bitmapCount = 0;
    unsigned refreshCount = 0;
    bool refresh() override { ++refreshCount; return HostDisplayTarget::refresh(); }
  };
  BoundsCheckedDisplay display(path);
  TEST_ASSERT_TRUE(calendar::renderCalendar(display, events, count, today, "test", "calendar-host", -1, parsedWeatherFixture()));
  TEST_ASSERT_EQUAL_UINT(7, display.bitmapCount);
  TEST_ASSERT_EQUAL_UINT(1, display.refreshCount);
  TEST_ASSERT_TRUE(calendar::renderCalendar(display, events, count, today, "test", "calendar-host", -1, calendar::WeatherData{}));
  TEST_ASSERT_EQUAL_UINT(10, display.bitmapCount);
  TEST_ASSERT_EQUAL_UINT(2, display.refreshCount);
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

void test_weather_conditions_map_to_lucide_icons_with_cloud_fallback(void) {
  using calendar::WeatherCondition;
  using calendar::WeatherIcon;
  struct Mapping { WeatherCondition condition; WeatherIcon day; WeatherIcon night; };
  const Mapping cases[] = {
    {WeatherCondition::Clear, WeatherIcon::Sunny, WeatherIcon::ClearNight},
    {WeatherCondition::PartlyCloudy, WeatherIcon::PartlyCloudy, WeatherIcon::PartlyCloudyNight},
    {WeatherCondition::Cloudy, WeatherIcon::Cloudy, WeatherIcon::Cloudy},
    {WeatherCondition::Overcast, WeatherIcon::Cloudy, WeatherIcon::Cloudy},
    {WeatherCondition::Drizzle, WeatherIcon::Drizzle, WeatherIcon::Drizzle},
    {WeatherCondition::Rain, WeatherIcon::Rain, WeatherIcon::Rain},
    {WeatherCondition::HeavyRain, WeatherIcon::HeavyRain, WeatherIcon::HeavyRain},
    {WeatherCondition::Thunderstorm, WeatherIcon::Thunderstorm, WeatherIcon::Thunderstorm},
    {WeatherCondition::Snow, WeatherIcon::Snow, WeatherIcon::Snow},
    {WeatherCondition::Fog, WeatherIcon::Fog, WeatherIcon::Fog},
    {WeatherCondition::Mist, WeatherIcon::Fog, WeatherIcon::Fog},
    {WeatherCondition::Wind, WeatherIcon::Wind, WeatherIcon::Wind},
    {WeatherCondition::Unknown, WeatherIcon::Cloudy, WeatherIcon::Cloudy},
    {static_cast<WeatherCondition>(255), WeatherIcon::Cloudy, WeatherIcon::Cloudy},
  };
  for (const Mapping &mapping : cases) {
    TEST_ASSERT_EQUAL_UINT(static_cast<unsigned>(mapping.day),
        static_cast<unsigned>(calendar::getWeatherIcon(mapping.condition, true)));
    TEST_ASSERT_EQUAL_UINT(static_cast<unsigned>(mapping.night),
        static_cast<unsigned>(calendar::getWeatherIcon(mapping.condition, false)));
  }
  for (const uint8_t size : {calendar::WeatherIcons::FORECAST_SIZE, calendar::WeatherIcons::CURRENT_SIZE}) {
    for (unsigned index = 0; index <= static_cast<unsigned>(WeatherIcon::Wind); ++index) {
      const calendar::WeatherBitmap &asset = calendar::WeatherIcons::asset(static_cast<WeatherIcon>(index), size);
      TEST_ASSERT_NOT_NULL(asset.bitmap);
      TEST_ASSERT_EQUAL_UINT(size, asset.width);
      TEST_ASSERT_EQUAL_UINT(size, asset.height);
      const unsigned pitch = (size + 7) / 8;
      bool hasInk = false;
      for (unsigned byte = 0; byte < pitch * size; ++byte) hasInk |= asset.bitmap[byte] != 0;
      TEST_ASSERT_TRUE(hasInk);
      // All Lucide strokes retain whitespace inside their asset bounds.
      for (unsigned byte = 0; byte < pitch; ++byte) {
        TEST_ASSERT_EQUAL_UINT8(0, asset.bitmap[byte]);
        TEST_ASSERT_EQUAL_UINT8(0, asset.bitmap[(size - 1) * pitch + byte]);
      }
    }
    TEST_ASSERT_EQUAL_PTR(calendar::WeatherIcons::asset(WeatherIcon::Cloudy, size).bitmap,
        calendar::WeatherIcons::asset(static_cast<WeatherIcon>(255), size).bitmap);
  }
}

void test_host_provider_error_and_missing_configuration_are_clean(void) {
  calendar::CalendarSettings settings = calendar::defaultCalendarSettings();
  strncpy(settings.calendarUrl, "http://calendar.example/family.ics", sizeof(settings.calendarUrl) - 1);
  calendar::HostCalendarProvider remoteProvider(settings, {2026, 10, 1});
  calendar::CalendarEvent events[calendar::kMaxEvents] = {};
  TEST_ASSERT_EQUAL_UINT(0, remoteProvider.loadEvents(events, calendar::kMaxEvents));
  TEST_ASSERT_TRUE(strlen(remoteProvider.error()) > 0);

  settings.calendarUrl[0] = '\0';
  calendar::HostCalendarProvider missingProvider(settings, {2026, 10, 1});
  TEST_ASSERT_EQUAL_UINT(0, missingProvider.loadEvents(events, calendar::kMaxEvents));
  TEST_ASSERT_EQUAL_STRING("Calendar URL is not configured.", missingProvider.error());
}


void test_today_timed_glyph_bounds_are_centered() {
  class CheckedDisplay : public calendar::HostDisplayTarget {
  public:
    CheckedDisplay() : HostDisplayTarget(".dev/timed-alignment-preview.png") {}
    void roundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t r,
                   calendar::DisplayColor fill, calendar::DisplayColor border) override {
      if (x == 16 && h == 72) cardY = y;
      HostDisplayTarget::roundRect(x, y, w, h, r, fill, border);
    }
    void text(uint16_t x, uint16_t y, const char *value, uint8_t font,
              calendar::DisplayColor fg, calendar::DisplayColor bg) override {
      if ((x == 29 || x == 140) && y >= 128 && y < 448) {
        if (cardY == 292 && x == 140) {
          TEST_ASSERT_EQUAL_UINT(cardY + (font == calendar::kCalendarFontTitle ? 17 : 43) + fontHeight(font), y);
          ++metadataLines;
        } else {
          const auto bounds = textVerticalBounds(value, font);
          const int topGap = y + bounds.top - cardY;
          const int bottomGap = cardY + 72 - (y + bounds.top + bounds.height);
          TEST_ASSERT_INT_WITHIN(1, topGap, bottomGap);
          ++centeredItems;
        }
      }
      HostDisplayTarget::text(x, y, value, font, fg, bg);
    }
    uint16_t cardY = 0;
    unsigned centeredItems = 0, metadataLines = 0;
  } display;
  const calendar::CalendarDate date = {2026, 10, 2};
  const auto range = calendar::calendarTodayRange(date);
  calendar::CalendarEvent events[] = {
    eventAt("short", range.startEpoch + 45000, range.startEpoch + 46800),
    eventAt("long", range.startEpoch + 54900, range.startEpoch + 56700),
    eventAt("metadata", range.startEpoch + 60000, range.startEpoch + 61800),
  };
  snprintf(events[0].title, sizeof(events[0].title), "DUMP RUN");
  snprintf(events[1].title, sizeof(events[1].title), "Pick up Isla after school");
  snprintf(events[2].title, sizeof(events[2].title), "With metadata");
  snprintf(events[2].location, sizeof(events[2].location), "School entrance");
  TEST_ASSERT_TRUE(calendar::renderCalendar(display, events, 3, date, "test", "host", -1));
  TEST_ASSERT_EQUAL_UINT(5, display.centeredItems);
  TEST_ASSERT_EQUAL_UINT(2, display.metadataLines);
}

void test_today_groups_all_day_cards_and_preserves_text_backgrounds() {
  class CheckedDisplay : public calendar::HostDisplayTarget {
  public:
    CheckedDisplay() : HostDisplayTarget("today-preview-test.png") {}
    void roundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t r,
                   calendar::DisplayColor fill, calendar::DisplayColor border) override {
      if (x == 16 && (h == 72 || h == 48)) {
        TEST_ASSERT_EQUAL_UINT(fills.empty() ? 128 : cardY + cardHeight + 10, y);
        TEST_ASSERT_EQUAL_UINT(fill == calendar::CalendarPatterns::Sidebar ? 48 : 72, h);
        cardY = y;
        cardHeight = h;
        TEST_ASSERT_EQUAL_UINT(calendar::CalendarColors::Border, border);
        fills.push_back(fill);
      }
      HostDisplayTarget::roundRect(x, y, w, h, r, fill, border);
    }
    void text(uint16_t x, uint16_t y, const char *value, uint8_t font,
              calendar::DisplayColor fg, calendar::DisplayColor bg) override {
      if (x < 458 && y >= 128 && y < 448) {
        TEST_ASSERT_EQUAL_UINT(calendar::CalendarColors::Foreground, fg);
        TEST_ASSERT_EQUAL_UINT(fills.back(), bg);
        if (x == 29) {
          if (cardHeight == 48) {
            TEST_ASSERT_EQUAL_UINT(cardY + (cardHeight + fontHeight(font)) / 2, y);
          } else {
            const auto bounds = textVerticalBounds(value, font);
            const int topGap = y + bounds.top - cardY;
            const int bottomGap = cardY + cardHeight - (y + bounds.top + bounds.height);
            TEST_ASSERT_INT_WITHIN(1, topGap, bottomGap);
          }
          times.emplace_back(value);
        }
        if (x == 140 && font == calendar::kCalendarFontTitle) {
          if (cardHeight == 48) {
            TEST_ASSERT_EQUAL_UINT(cardY + (cardHeight + fontHeight(font)) / 2, y);
          } else {
            const auto bounds = textVerticalBounds(value, font);
            const int topGap = y + bounds.top - cardY;
            const int bottomGap = cardY + cardHeight - (y + bounds.top + bounds.height);
            TEST_ASSERT_INT_WITHIN(1, topGap, bottomGap);
          }
          titles.emplace_back(value);
        }
      }
      if (y >= 448 && strstr(value, "events today")) footer = value;
      HostDisplayTarget::text(x, y, value, font, fg, bg);
    }
    std::vector<calendar::DisplayColor> fills;
    uint16_t cardY = 0, cardHeight = 0;
    std::vector<std::string> times, titles;
    std::string footer;
  } display;
  const calendar::CalendarDate date = {2026, 10, 1};
  const auto range = calendar::calendarTodayRange(date);
  calendar::CalendarEvent events[] = {
    eventAt("late", range.startEpoch + 3600, range.startEpoch + 7200),
    eventAt("day-first", calendar::calendarEpoch(date), calendar::calendarEpoch({2026, 10, 2}), true),
    eventAt("overnight", range.startEpoch - 3600, range.startEpoch + 1800),
    eventAt("day-second", calendar::calendarEpoch(date), calendar::calendarEpoch({2026, 10, 2}), true),
    eventAt("multi-day", calendar::calendarEpoch({2026, 9, 30}), calendar::calendarEpoch({2026, 10, 2}), true),
  };
  calendar::CalendarEvent selected[5];
  TEST_ASSERT_EQUAL_UINT(5, calendar::selectEventsForRange(events, 5, range, selected, 5));
  calendar::orderTodayEvents(selected, 5);
  const char *expected[] = {"multi-day", "day-first", "day-second", "overnight", "late"};
  for (size_t i = 0; i < 5; ++i) {
    TEST_ASSERT_EQUAL_STRING(expected[i], selected[i].id);
    snprintf(events[i].title, sizeof(events[i].title), "%s", events[i].id);
  }
  TEST_ASSERT_TRUE(calendar::renderCalendar(display, events, 5, date, "test", "host", -1));
  TEST_ASSERT_EQUAL_UINT(4, display.fills.size());
  for (size_t i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL_STRING(expected[i], display.titles[i].c_str());
    TEST_ASSERT_EQUAL_UINT(i < 3 ? calendar::CalendarPatterns::Sidebar : calendar::CalendarColors::Background,
                           display.fills[i]);
    if (i < 3) TEST_ASSERT_EQUAL_STRING("ALL DAY", display.times[i].c_str());
  }
  TEST_ASSERT_EQUAL_STRING("5 events today", display.footer.c_str());
  // A single all-day card still precedes an overnight event and ordinary times.
  events[1] = eventAt("noon", range.startEpoch + 12 * 3600, range.startEpoch + 13 * 3600);
  events[3] = eventAt("morning", range.startEpoch + 9 * 3600, range.startEpoch + 10 * 3600);
  for (auto &event : events) snprintf(event.title, sizeof(event.title), "%s", event.id);
  display.fills.clear();
  display.times.clear();
  display.titles.clear();
  TEST_ASSERT_TRUE(calendar::renderCalendar(display, events, 5, date, "test", "host", -1));
  const char *singleDayExpected[] = {"multi-day", "overnight", "late", "morning"};
  for (size_t i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL_STRING(singleDayExpected[i], display.titles[i].c_str());
    TEST_ASSERT_EQUAL_UINT(i == 0 ? calendar::CalendarPatterns::Sidebar : calendar::CalendarColors::Background,
                           display.fills[i]);
  }
  TEST_ASSERT_EQUAL_STRING("ALL DAY", display.times[0].c_str());
  TEST_ASSERT_EQUAL_STRING("01:00", display.times[2].c_str());
  TEST_ASSERT_EQUAL_STRING("09:00", display.times[3].c_str());
  TEST_ASSERT_EQUAL_STRING("5 events today", display.footer.c_str());
  remove("today-preview-test.png");
}

void test_footer_fits_long_labels_and_keeps_cards_above_status_bar() {
  class CheckedDisplay : public calendar::HostDisplayTarget {
  public:
    CheckedDisplay() : HostDisplayTarget("footer-preview-test.png") {}
    void text(uint16_t x, uint16_t y, const char *value, uint8_t font,
              calendar::DisplayColor fg, calendar::DisplayColor bg) override {
      if (y >= 448) {
        TEST_ASSERT_EQUAL_UINT(calendar::kCalendarFontStatus, font);
        TEST_ASSERT_TRUE(x >= 16 && x + textWidth(value, font) <= 784);
        TEST_ASSERT_TRUE(y - fontHeight(font) > 448 && y + 5 < 480);
        labels.emplace_back(value);
      }
      HostDisplayTarget::text(x, y, value, font, fg, bg);
    }
    void roundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t r,
                   calendar::DisplayColor fill, calendar::DisplayColor border) override {
      if (y < 448) TEST_ASSERT_TRUE(y + h <= 448);
      HostDisplayTarget::roundRect(x, y, w, h, r, fill, border);
    }
    bool refresh() override { ++refreshes; return HostDisplayTarget::refresh(); }
    std::vector<std::string> labels;
    unsigned refreshes = 0;
  } display;
  const calendar::CalendarDate today = {2026, 10, 1};
  calendar::MockCalendarProvider provider(today);
  calendar::CalendarEvent events[calendar::kMaxEvents] = {};
  const size_t count = provider.loadEvents(events, calendar::kMaxEvents);
  calendar::FooterStatus footer;
  footer.wifiLabel = "A very long home network name that must be truncated before reaching the right group";
  footer.wifiConnected = true;
  footer.lastUpdated = "18:42";
  footer.batteryPercent = 72;
  TEST_ASSERT_TRUE(calendar::renderCalendar(display, events, count, today, "test", "host", -1, parsedWeatherFixture(), footer));
  TEST_ASSERT_EQUAL_UINT(1, display.refreshes);
  TEST_ASSERT_EQUAL_UINT(4, display.labels.size());
  TEST_ASSERT_EQUAL_STRING("6 events today", display.labels[0].c_str());
  TEST_ASSERT_EQUAL_STRING("72%", display.labels[3].c_str());
  TEST_ASSERT_NOT_NULL(strstr(display.labels[2].c_str(), "..."));
  TEST_ASSERT_EQUAL_STRING("Last updated: 18:42", display.labels[1].c_str());
  display.labels.clear();
  footer.wifiLabel = nullptr;
  footer.lastUpdated = nullptr;
  footer.batteryPercent = -1;
  footer.batteryTenthsVolts = 39;
  TEST_ASSERT_TRUE(calendar::renderCalendar(display, nullptr, 0, today, "test", "host", 39, calendar::WeatherData{}, footer));
  TEST_ASSERT_EQUAL_STRING("0 events today", display.labels[0].c_str());
  TEST_ASSERT_EQUAL_STRING("3.9V", display.labels[3].c_str());
  TEST_ASSERT_EQUAL_STRING("Last updated: --:--", display.labels[1].c_str());
  display.labels.clear();
  footer.todayEventCount = 1;
  footer.batteryPercent = 200;
  TEST_ASSERT_TRUE(calendar::renderCalendar(display, nullptr, 0, today, "test", "host", -1, calendar::WeatherData{}, footer));
  TEST_ASSERT_EQUAL_STRING("1 event today", display.labels[0].c_str());
  TEST_ASSERT_EQUAL_STRING("100%", display.labels[3].c_str());
  remove("footer-preview-test.png");
}

void test_footer_display_hash_tracks_status_changes() {
  calendar::FooterStatus footer;
  const auto hash = calendar::footerDisplayHash(footer);
  TEST_ASSERT_EQUAL_UINT32(hash, calendar::footerDisplayHash(footer));
  footer.lastUpdated = "18:42";
  TEST_ASSERT_NOT_EQUAL(hash, calendar::footerDisplayHash(footer));
  const auto updatedHash = calendar::footerDisplayHash(footer);
  footer.wifiConnected = true;
  TEST_ASSERT_NOT_EQUAL(updatedHash, calendar::footerDisplayHash(footer));
}

void setUp(void) { setLondonTimezoneForTest(); }
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_range_selection_sorts_events_by_start);
  RUN_TEST(test_today_groups_all_day_cards_and_preserves_text_backgrounds);
  RUN_TEST(test_today_timed_glyph_bounds_are_centered);
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
  RUN_TEST(test_weather_wmo_mapping_and_url);
  RUN_TEST(test_weather_parser_preserves_cache_on_invalid_response);
  RUN_TEST(test_weather_hash_tracks_visible_values);
  RUN_TEST(test_weather_conditions_map_to_lucide_icons_with_cloud_fallback);
  RUN_TEST(test_host_provider_error_and_missing_configuration_are_clean);
  RUN_TEST(test_footer_fits_long_labels_and_keeps_cards_above_status_bar);
  RUN_TEST(test_footer_display_hash_tracks_status_changes);
  return UNITY_END();
}
