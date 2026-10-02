"""Normalize a private host feed into the bounded shared parser's visible window.

Input is an ignored temporary file; stdout contains only normalized ICS.
Never include feed contents or exception details in diagnostics.
"""
import hashlib
import subprocess
import sys
from datetime import datetime, time, timedelta, timezone
from pathlib import Path
from zoneinfo import ZoneInfo

import icalendar
import recurring_ical_events

MAX_INPUT = 2 * 1024 * 1024
MAX_OUTPUT = 48 * 1024
MAX_EVENTS = 16
LONDON = ZoneInfo("Europe/London")


def instant(value):
    if isinstance(value, datetime):
        return (value if value.tzinfo else value.replace(tzinfo=LONDON)).astimezone(timezone.utc)
    return datetime.combine(value, time(), LONDON).astimezone(timezone.utc)


def normalize(data, first_day, end_day):
    if len(data) > MAX_INPUT:
        raise ValueError("Feed exceeds host 2 MiB limit.")
    source = icalendar.Calendar.from_ical(data)
    # Preserve floating London semantics before recurrence expansion.
    source["X-WR-TIMEZONE"] = "Europe/London"
    output = icalendar.Calendar()
    output.add("version", "2.0")
    output.add("prodid", "-//Paper Calendar Host//EN")
    first = instant(first_day)
    end = instant(end_day)
    count = 0
    for event in source.walk("VEVENT"):
        rule = event.get("RRULE", {})
        if any(str(freq).upper() in ("SECONDLY", "MINUTELY") for freq in rule.get("FREQ", [])):
            raise ValueError("Sub-hourly recurrence is unsupported in host mode.")
    for event in recurring_ical_events.of(source).between(first, end):
        start = event.decoded("DTSTART")
        if instant(start) >= end:
            continue
        if str(event.get("STATUS", "")).upper() == "CANCELLED":
            continue
        finish = event.decoded("DTEND", None)
        if finish is None:
            finish = start + event.decoded("DURATION", timedelta(days=1) if not isinstance(start, datetime) else timedelta())
        if instant(finish) <= first or instant(finish) <= instant(start):
            continue
        count += 1
        if count > MAX_EVENTS:
            raise ValueError("More than 16 events in the visible week.")
        normalized = icalendar.Event()
        # Stable, short identity for each occurrence, including moved exceptions.
        identity = str(event.get("UID", "")) + "|" + str(event.get("RECURRENCE-ID", start))
        normalized.add("uid", hashlib.sha256(identity.encode()).hexdigest()[:32])
        normalized.add("summary", str(event.get("SUMMARY", ""))[:128])
        normalized.add("dtstart", instant(start) if isinstance(start, datetime) else start)
        normalized.add("dtend", instant(finish) if isinstance(finish, datetime) else finish)
        output.add_component(normalized)
    result = output.to_ical()
    if len(result) > MAX_OUTPUT:
        raise ValueError("Normalized feed exceeds 48 KiB limit.")
    return result


def main():
    try:
        if sys.argv[1] == "--worker":
            result = normalize(sys.stdin.buffer.read(MAX_INPUT + 1),
                               datetime.fromtimestamp(int(sys.argv[2]), timezone.utc),
                               datetime.fromtimestamp(int(sys.argv[3]), timezone.utc))
            sys.stdout.buffer.write(result)
            return 0
        with Path(sys.argv[1]).open("rb") as feed:
            data = feed.read(MAX_INPUT + 1)
        worker = subprocess.run([sys.executable, __file__, "--worker", sys.argv[2], sys.argv[3]],
                                input=data, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                timeout=10, check=True)
        sys.stdout.buffer.write(worker.stdout)
        return 0
    except Exception:
        # Exception messages from third-party parsers can contain private events.
        return 1


if __name__ == "__main__":
    sys.exit(main())
