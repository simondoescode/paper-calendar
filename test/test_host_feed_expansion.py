"""Run with .dev/calendar-tools/Scripts/python.exe -m unittest discover -s test -p test_host_feed_expansion.py."""
import sys
import unittest
from datetime import date, datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from expand_host_calendar import normalize
from icalendar import Calendar


def feed(events):
    return ("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n" + events + "END:VCALENDAR\r\n").encode()


class ExpansionTests(unittest.TestCase):
    def events(self, data, first=date(2026, 10, 1), end=date(2026, 10, 5)):
        return Calendar.from_ical(normalize(data, first, end)).walk("VEVENT")

    def test_recurrence_exclusion_and_moved_override(self):
        data = feed("""BEGIN:VEVENT
UID:series
SUMMARY:Weekly
DTSTART;TZID=Europe/London:20260924T100000
DTEND;TZID=Europe/London:20260924T110000
RRULE:FREQ=DAILY;COUNT=14
EXDATE;TZID=Europe/London:20261002T100000
END:VEVENT
BEGIN:VEVENT
UID:series
RECURRENCE-ID;TZID=Europe/London:20261003T100000
DTSTART;TZID=Europe/London:20261003T140000
DTEND;TZID=Europe/London:20261003T150000
SUMMARY:Moved
END:VEVENT
""")
        events = self.events(data)
        self.assertEqual(3, len(events))
        moved = next(event for event in events if event["SUMMARY"] == "Moved")
        self.assertEqual(datetime(2026, 10, 3, 13, tzinfo=timezone.utc), moved.decoded("DTSTART"))
        self.assertEqual(3, len({str(event["UID"]) for event in events}))
        self.assertTrue(all("RRULE" not in event and "EXDATE" not in event for event in events))

    def test_all_day_overlap_and_exclusive_end(self):
        events = self.events(feed("""BEGIN:VEVENT
UID:overnight
SUMMARY:Trip
DTSTART;VALUE=DATE:20260930
DTEND;VALUE=DATE:20261002
END:VEVENT
BEGIN:VEVENT
UID:next-week
DTSTART;VALUE=DATE:20261005
DTEND;VALUE=DATE:20261006
END:VEVENT
"""))
        self.assertEqual(1, len(events))
        self.assertEqual(date(2026, 9, 30), events[0].decoded("DTSTART"))
        self.assertEqual(date(2026, 10, 2), events[0].decoded("DTEND"))

    def test_dst_duration_and_empty_calendar(self):
        events = self.events(feed("""BEGIN:VEVENT
UID:clock-change
DTSTART;TZID=Europe/London:20261024T100000
DURATION:PT1H
RRULE:FREQ=DAILY;COUNT=2
END:VEVENT
"""), date(2026, 10, 24), date(2026, 10, 26))
        self.assertEqual([9, 10], [event.decoded("DTSTART").hour for event in events])
        self.assertTrue(all((event.decoded("DTEND") - event.decoded("DTSTART")).total_seconds() == 3600 for event in events))
        self.assertEqual([], self.events(feed("")))

    def test_capacity_and_download_bounds(self):
        events = "".join(f"BEGIN:VEVENT\nUID:{i}\nDTSTART;VALUE=DATE:20261002\nDTEND;VALUE=DATE:20261003\nEND:VEVENT\n" for i in range(17))
        with self.assertRaises(ValueError):
            self.events(feed(events))
        with self.assertRaises(ValueError):
            self.events(b" " * (2 * 1024 * 1024 + 1))


if __name__ == "__main__":
    unittest.main()
