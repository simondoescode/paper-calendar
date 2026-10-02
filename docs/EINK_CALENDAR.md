# Self-hosted E-Ink Family Calendar

## Goal and hardware

This fork is being developed into a self-hosted family calendar for the TRMNL
BYOD 7.5-inch OG DIY Kit:

- Seeed XIAO ESP32-S3 PLUS
- 7.5-inch monochrome e-paper panel, 800 x 480 pixels
- TRMNL/Seeed display driver PCB
- 2000 mAh rechargeable battery and Wi-Fi

The PlatformIO target is `TRMNL_7inch5_OG_DIY_Kit`. The target selects the
existing `seeed_xiao_esp32s3` board, Arduino framework, and
`BOARD_XIAO_EPAPER_DISPLAY`; `DEVICE_MODEL` is
`xiao_epaper_display`. The corresponding upstream device entry in
`src/display.cpp` selects `EPD_75` and the existing `bb_epaper` driver.
Its configured display wiring is SCK 7, MOSI 9, CS 44, reset 38, DC 10,
busy 4. Battery measurement uses ADC pin 1 and enable pin 6. These are
upstream definitions and must not be duplicated or changed for this calendar.

## Existing firmware and reuse boundary

The normal OG startup in `src/main.cpp` runs the QA check and then calls
`bl_init()` from `src/bl.cpp`. That business-logic path initializes the TRMNL
application, shows its branding, provisions/connects Wi-Fi, calls the TRMNL
setup/display services, and enters the existing sleep path. The display code
does not itself require the TRMNL cloud: the cloud behavior is in the
business-logic/services path.

| Area | Existing implementation | Calendar decision |
| --- | --- | --- |
| Board target, pins, panel profile | `platformio.ini`, `src/display.cpp` device table | Reuse unchanged; do not reimplement the driver or pin map. |
| Display init, text/shape drawing, panel refresh and display sleep | `display_init()`, `display_show_image()`, `display_sleep()` and `bb_epaper` | Reuse the driver and panel profile. Add only a narrow drawing bridge for calendar content so rendering uses the driver's buffer and refresh path. |
| Wi-Fi provisioning and saved credentials | `WifiCaptivePortal` in `lib/wificaptive`, credentials in its `wificaptive` Preferences namespace, and `connectWithSavedCredentials()` in `src/wifi_network.cpp` | Reused by the calendar `WifiManager`. The portal already handles multiple networks and its timeout is overridden so calendar startup retains control of sleep. |
| Preferences / NVS | Arduino `Preferences` in upstream services and the Wi-Fi portal | Reused for existing Wi-Fi settings; calendar preferences use the existing `"data"` namespace and do not duplicate credentials. |
| NTP and time | `Clock::setTimeFromNTP()` and `Clock::getTime()` in `src/misc/clock/clock.cpp` | The shared clock's 24-hour skip and UTC-only configuration do not meet the calendar's timezone and fresh-sync requirements. `ClockService` uses ESP32 SNTP directly and configures London time. |
| Battery measurement | `BaseBattery` / `ADCBattery` in `include/battery.h` and `src/battery/adc_battery.cpp`, with target-specific `TRMNL_DEVICE` data | Reuse the ADC measurement. This target exposes voltage, not a reliable state-of-charge percentage, so show voltage rather than inventing a percentage. |
| Power-down and wake | `goToSleep()` in `src/bl.cpp` disconnects Wi-Fi, turns the radio off, and configures timer/button wake; `display_sleep()` puts the panel to sleep | The calendar reuses the panel sleep function and the existing device-table interrupt pin for GPIO wake, alongside a configurable 12-hour timer. It does not call the TRMNL application sleep path. |
| RTC-retained state | Existing TRMNL RTC attributes in `src/globals.cpp` | Calendar RTC memory holds a display-state marker/hash and a small pending-manual-refresh marker, never an image buffer. |
| TRMNL cloud application flow | `bl_init()` and the API/services in `src/bl.cpp` | Bypass only this application flow for the calendar build. Do not remove upstream services or board support. |

The calendar-specific startup is selected only by the `EINK_CALENDAR_APP`
flag on `TRMNL_7inch5_OG_DIY_Kit`; this avoids changing other environments
that share `BOARD_XIAO_EPAPER_DISPLAY`. No TRMNL cloud calls or credentials
are part of the calendar demo.

## Calendar architecture

The calendar data model is independent of the display and network formats.
`CalendarEvent` contains a stable ID, title, start and end, an all-day flag,
source/calendar name, and optional location. `CalendarProvider` separates
retrieval from rendering. `MockCalendarProvider` remains available for
deterministic development, and `IcalendarFeedProvider` retrieves the configured
private feed. Shared helpers order events, select timed events by instant and
all-day events by civil-date overlap, format event times, and safely truncate
titles. The display adapter receives selected event data and draws the local
monochrome Today / This Week layout.

Timed `startEpoch` and `endEpoch` values are Unix seconds; end times are
exclusive for range overlap. All-day events use `allDayStartDate` and
exclusive `allDayEndDate` civil dates, never synthetic midnight timestamps.
The iCalendar parser converts UTC values to instants and interprets floating
date-times as London wall time. Its parser is bounded and deliberately
supports a limited subset; current limitations are listed below.

The 800 x 480 layout gives Today the dominant left-hand column and This Week
the smaller right-hand column. It uses black and white only, bounded text
buffers, truncation, and the existing e-paper drawing buffer. The UI does not
keep a second full-screen bitmap in RAM.

## Calendar footer

The calendar has a 32 px white status bar across the bottom of the 800x480
canvas, with a one-pixel black separator at y=448. The columns use a rounded 60/40 split after accounting for 16 px
outer margins and a 32 px padded gutter: 768 px inside the margins,
736 px available to the columns, 442 px main content and 294 px sidebar
content. Full-bleed backgrounds meet at x=474; sidebar content starts
at x=490. Sidebar cards use a 44 px block containing only a centered weekday,
with 12 px between the divider and event text and 12 px right padding, giving
event titles 226 px. Cards are 52 px tall and show only the weekday and a
vertically centered title in the existing 13 px Manrope Medium font, with
ellipsis truncation when needed. The sidebar heading has no date
range; the forecast shows only weekday names and icons, starting at
y=51, without dates or temperatures. The first sidebar card starts at y=124. Forecast cells
share the sidebar width evenly. Main-card gaps
are 10 px and sidebar-card gaps are 8 px, leaving all cards above the footer.
Events beyond the visible cards do not display overflow labels.

`FooterStatus` in `include/calendar/footer_status.h` passes values from the
lifecycle adapter to the shared renderer. Footer drawing uses the existing
compressed bitmap font renderer with compact Manrope Medium (`Manrope_Medium_8.h`).
Wi-Fi, calendar and battery icons are vendored Lucide SVGs rasterized into
16px monochrome sprites (128 bytes total) by `tools/generate_status_icons.py`,
using the weather icon generator dependencies. Disconnected Wi-Fi uses
Lucide `wifi-off`; battery percentage fills the Lucide battery outline.
License notices are in `assets/status/lucide/LICENSE`. Separator dots are
filled circles. There is no new graphics
library, framebuffer, network operation or heap allocation in footer drawing.

The left group reports all current-day events in the bounded provider data,
including events beyond the four visible cards, followed by local
`Last updated: HH:MM`. The timestamp is prepared after
calendar/weather retrieval, immediately before deciding whether to draw.
It is part of the visible-state hash, so a changed displayed minute triggers
a redraw even if events are unchanged. One render still performs one panel
refresh. The same minute and otherwise unchanged visible state can still skip
refreshing.

The right group shows a generic **Home WiFi** label and the actual connection
state, followed by the battery reading. The label is intentionally a
placeholder rather than the saved SSID. A supplied percentage is
clamped to 0–100. Firmware uses the existing measured battery voltage because
the ADC abstraction does not expose calibrated charge percentage; unknown
battery/time values show `--%` / `--:--`. The host preview uses a clearly
documented 72% battery mock and connected Wi-Fi state, plus actual local
refresh time and fixture event count. Charging is reserved for future use.

Text width and capital height come from the selected Manrope bitmap metrics.
The right group is anchored to the right margin; a bounded, truncated Wi-Fi
label prevents the left group from overlapping it. Tests cover long/null
labels, event counts, voltage/percentage fallbacks, card boundaries and one
refresh per render.

## Build and flash

Build from the repository root:

```powershell
pio run -e TRMNL_7inch5_OG_DIY_Kit
```

With the board connected and in its normal ESP32-S3 download mode, flash with:

```powershell
pio run -e TRMNL_7inch5_OG_DIY_Kit -t upload
```

If PlatformIO cannot select the correct serial port, list ports with
`pio device list` and pass one explicitly:

```powershell
pio run -e TRMNL_7inch5_OG_DIY_Kit -t upload --upload-port COM3
```

Open serial diagnostics at 115200 baud:

```powershell
pio device monitor -e TRMNL_7inch5_OG_DIY_Kit
```

The board target, panel profile, and SPI pins are defined upstream in
`platformio.ini` and `src/display.cpp`. Use the board's BOOT/download procedure
if automatic upload mode does not start.

## Host development / hardware-free testing

The `calendar-host` PlatformIO environment runs the shared calendar model,
event selection, calendar layout renderer, and `CalendarProvider` contract on
the development machine. A native `DisplayTarget` draws into a monochrome
framebuffer and writes an 800×480, 1-bit grayscale PNG. A small local HTTP
server provides a settings page and lifecycle controls.

Build and run from the repository root:

```sh
pio run -e calendar-host
```

The native environment requires a C++ compiler on `PATH`: MinGW-w64 (`g++`)
on Windows, GCC/build-essential on Linux, or Xcode Command Line Tools on
macOS. Run the deterministic host tests with:

```sh
pio test -e native_calendar
```

PlatformIO writes the executable under `.pio/build/calendar-host/`. On Windows
it is normally `program.exe`; on Linux/macOS it is normally `program`. Run it
from the repository root so `.dev/` paths resolve as documented:

```powershell
.\.pio\build\calendar-host\program.exe
```

```sh
./.pio/build/calendar-host/program
```

At startup the emulator performs one refresh, then starts the local portal at
**http://localhost:8080**. The portal lets you view/edit the calendar URL,
refresh interval, and timezone. Saving persists settings immediately and runs
a render. **Refresh now** runs another lifecycle iteration; **Reload settings
and render** discards any unsaved in-memory state and reloads the persisted
file. There is no periodic timer loop and the process never sleeps for the
configured interval.

Host files are local development state and are ignored by Git:

- Settings: `.dev/calendar-settings.json`
- Preview: `.dev/calendar-preview.png`

The default URL `fixture://default` selects the built-in deterministic
`MockCalendarProvider` fixture relative to the current local date. The fixture
contains all-day and timed events, multiple events on a day, a long title,
events later in the week, and a day with no events. Tests use explicitly
injected dates and never use live Google Calendar data. A local `.ics` fixture
can also be selected using a `file://` URL. Host mode fetches remote
HTTPS URLs using the host's certificate trust store (WinHTTP on Windows,
libcurl on Linux/macOS). Remote feeds use a non-redirecting GET, a 20-second
timeout and a host-only 2 MiB download limit. A local Python adapter expands
recurring events, exclusions and moved instances for the visible window,
converts timed events to UTC and preserves all-day civil dates. It drops
unused feed metadata before passing a maximum 48 KiB normalized feed and
16 visible events to the shared parser and renderer. Expansion has a
10-second timeout; sub-hourly recurrence is rejected. Failed feeds show a
provider error without substituting fixture events. Linux/macOS host builds
require libcurl development headers and libraries.

Set up the host-only recurrence adapter once from the repository root:

```powershell
python -m venv .dev/calendar-tools
.dev/calendar-tools/Scripts/python.exe -m pip install -r tools/calendar_host_requirements.txt
```

On Linux/macOS, use `.dev/calendar-tools/bin/python` for the second command.
The downloaded feed is briefly stored in an ignored `.dev/calendar-feed-*.ics`
file and removed after expansion. No private URL is passed on a command line.
Host recurrence support does not change firmware's 48 KiB download limit or
its recurrence limitations listed below.
For a local Windows fixture, use a file URI such as
`file:///C:/Users/you/Calendar/family.ics`; for Linux/macOS use
`file:///home/you/family.ics` or the corresponding absolute path.

The host app uses the same settings model and validation as the device. Host
JSON-file persistence is an adapter that approximates reload/persistence
behavior; it is **not an implementation of ESP32 NVS**. The device continues
using `Preferences`/NVS for the feed URL, refresh interval, and timezone, and
continues using its existing captive portal for Wi-Fi credentials. The same
calendar layout function is called by the host and the embedded display
adapter; on-device drawing still uses the existing `bb_epaper` buffer and
`EPD_75` profile unchanged.

Emulated: configuration edits/persistence, HTTPS, fixture or local-file provider
flow, calendar date selection/layout, monochrome drawing, PNG output, and
manual refresh/reload actions.

Not emulated: ESP32 instruction execution, Wi-Fi provisioning or NVS internals,
real SNTP/network failures, TLS certificate behavior for a live remote feed,
the XIAO pin map, `bb_epaper` waveform/partial-refresh behavior, panel ghosting,
battery readings, GPIO button wake, or actual deep-sleep current. These still
require an embedded build and, where hardware-dependent, the physical TRMNL
device.
## Wi-Fi setup and credential reuse

The calendar uses the existing TRMNL captive portal and credential store;
there are no Wi-Fi secrets in the source tree. Saved Wi-Fi credentials remain
in the portal's NVS/Preferences namespace (`wificaptive`) and are tried by its
existing `autoConnect()` logic, including its support for saved networks and
the portal's network settings. The private calendar URL is entered into the
calendar-specific field in the captive portal and persisted to the existing
`data` Preferences namespace, using the portal's `api_url` key. Calendar
builds do not expose the saved URL from portal settings responses.

On a fresh device, the screen shows the setup SSID (`TRMNL-` plus the device
MAC suffix). Join that Wi-Fi network with a phone or computer and follow the
captive portal page to select the home Wi-Fi. The portal opens automatically
on boot when no saved credentials exist. If saved credentials no longer work,
the screen shows the setup SSID and the same portal opens so they can be
replaced. The portal retains its upstream 15-minute configuration window;
after that, the calendar performs its own normal sleep/retry rather than
entering the TRMNL cloud application's sleep path. Serial diagnostics report
state only and do not print SSIDs, passwords, or tokens.

The shared portal's per-attempt connection timeout is configured to 15
seconds by the calendar. Its retries and captive-portal lifetime remain
controlled by the upstream implementation.

## Clock, NTP, and London time

After Wi-Fi connects, `ClockService` configures ESP32 SNTP with
`time.google.com` and `pool.ntp.org` and waits up to 30 seconds for the SNTP
time-sync callback. A plausible Unix epoch and successful local-time
conversion are required before rendering a date; the device does not
substitute an unsynchronized RTC value when NTP fails.

The POSIX timezone rule is
`GMT0BST,M3.5.0/1,M10.5.0/2`, passed to Arduino-ESP32's `configTzTime`.
It describes GMT as standard time and BST as one hour ahead, beginning on the
last Sunday in March at 01:00 GMT and ending on the last Sunday in October at
02:00 BST. ESP-IDF's C library applies this rule through `localtime_r`, so
the spring skipped hour and autumn repeated hour are handled by the timezone
implementation rather than hand-coded seasonal offsets. The local date from this conversion drives Today / This Week selection.
`MockCalendarProvider` remains available for deterministic host-side
development; normal device execution retrieves events from the configured
iCalendar feed. Host tests exercise both 2026 UK clock transitions. Linux and
macOS use the POSIX rule; the Windows host adapter uses the OS `GMT Standard
Time` transition data because the MinGW C runtime does not apply the ESP-IDF
POSIX transition rule consistently. Firmware continues using the POSIX rule
above.

## Boot, refresh, and sleep lifecycle

The calendar target performs this sequence on cold boot, timer wake, and
KEY3/manual wake:

1. Determine whether the boot came from reset, the timer, or the KEY3 GPIO.
2. Initialize existing board/display support and read the upstream ADC battery
   voltage before enabling Wi-Fi.
3. On a KEY3 wake, use the upstream button classifier. A single press starts
   the normal refresh immediately; a long press clears saved Wi-Fi settings
   and opens the existing captive portal for reconfiguration.
   A press during an already-running awake cycle is queued and starts a fresh
   refresh after the current cycle safely sleeps the panel and disconnects Wi-Fi.
4. Reuse saved Wi-Fi credentials or open the captive portal when needed.
5. Synchronize via NTP and resolve the current London local date/time.
6. Retrieve and parse the configured private iCalendar feed. A manual wake
   follows the same retrieval path; it does not force a panel redraw if the
   visible state is unchanged.
7. Load the persisted weather cache and fetch Open-Meteo weather after the
   calendar feed. Weather failures retain cached data and do not abort the
   calendar render. Render only if the logical visible state has changed,
   including displayed weather values and day/night icons.
8. Sleep the panel through upstream `display_sleep()`, disconnect and disable
   Wi-Fi, set the timer and KEY3 wake sources, and enter ESP32 deep sleep.

The default automatic wake interval is `AUTO_REFRESH_INTERVAL_SECONDS` (12
hours) in `include/calendar/calendar_config.h`. The existing
`xiao_epaper_display` device-table entry supplies the interrupt pin (GPIO5);
the calendar uses that runtime pin for active-low ESP32-S3 GPIO wake instead
of duplicating a pin number or using the generic `PIN_INTERRUPT` macro.
Upstream code calls this the device interrupt pin rather than naming it
KEY3, so verify the physical KEY3-to-GPIO5 association on the assembled
hardware. NTP server names, Wi-Fi connection timeout, timezone rule, and NTP
timeout live beside the interval in central configuration.

## Open-Meteo weather

The calendar uses the [Open-Meteo forecast API](https://open-meteo.com/en/docs).
No API key is required for this personal, non-commercial use. Weather runs
once during the existing refresh cycle (12 hours by default), including a
manual button refresh. It adds no timer, polling loop or separate panel refresh.

Set `WEATHER_LATITUDE` and `WEATHER_LONGITUDE` in
`include/calendar/calendar_config.h`, or override both using build flags on
`TRMNL_7inch5_OG_DIY_Kit`, for example:

```ini
    -D WEATHER_LATITUDE=51.5074
    -D WEATHER_LONGITUDE=-0.1278
```

The defaults are London. These coordinates are sent to Open-Meteo; the device
does not use GPS or geolocation. The actual URL format is:

```text
https://api.open-meteo.com/v1/forecast?latitude=51.507400&longitude=-0.127800&current=temperature_2m,apparent_temperature,weather_code,is_day,wind_speed_10m&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset&timezone=auto&forecast_days=3&temperature_unit=celsius&wind_speed_unit=kmh
```

The internal model holds current temperature, apparent temperature, wind,
condition and day/night state, plus three daily forecasts with high/low,
rain probability and sunrise/sunset. Sunrise/sunset are copied as local
`HH:MM` from the API's `timezone=auto` timestamps. Calendar timezone handling
is unchanged. The existing weather area shows current temperature, condition,
today's high/low and rain probability; the sidebar shows three forecast days.
Wind and apparent temperature are retained for future use.

HTTPS uses the existing HTTPClient/WiFiClientSecure stack and CA bundle.
Connect, TLS handshake and response stages have 10-second timeouts; the body
is capped at 4096 bytes, JSON nesting at four levels and parser allocation
at an 8192-byte heap arena. DNS timing is also subject to the underlying
ESP32 network stack. No response JSON is passed to the renderer or logged.

Successful responses replace one versioned Preferences blob in the
`cal-weather` namespace. Boot loads and revalidates that cache; a coordinate
change invalidates it. Failed HTTPS, non-200, oversized, incomplete or invalid
responses preserve the last successful data. Cached weather may therefore be
old during an outage; there is no expiration that discards it. A device with
no valid cache displays **Weather unavailable** and omits forecast metrics.
NVS writes occur only after a successful fetch, never during rendering.
Fetch timestamps are excluded from display-change detection.

WMO mapping is centralized in `weatherConditionFromWmoCode()`:

| Codes | Condition / Lucide icon |
| --- | --- |
| 0 | Clear / sun (moon at night) |
| 1, 2 | Partly cloudy / cloud-sun (cloud-moon at night) |
| 3 | Cloudy / cloud |
| 45, 48 | Fog / cloud-fog |
| 51, 53, 55, 56, 57 | Drizzle / cloud-drizzle |
| 61, 63, 66 | Rain / cloud-rain |
| 65, 67, 80, 81, 82 | Heavy rain / cloud-rain-wind |
| 71, 73, 75, 77, 85, 86 | Snow / cloud-snow |
| 95, 96, 99 | Thunderstorm / cloud-lightning |
| Other | Unknown / cloud fallback |

The host preview reads the deterministic `test/fixtures/open_meteo.json`
fixture using the production parser, with forecast dates moved relative to
the host date. It logs that this is demo weather and makes no live request.
Offline tests cover all code mappings, day/night icons, parsing, invalid
responses preserving cached data, weather hashes and one refresh per render.

## Avoiding unnecessary panel refreshes

Before drawing, the app computes a 32-bit FNV-1a hash from the visible state:
status screen kind, or for the calendar the London date, visible events'
ordered IDs/titles/start/end/all-day values, and battery voltage rounded to
the displayed tenth of a volt. A marker and this single hash are stored in
`RTC_DATA_ATTR` memory and survive ESP32 deep sleep. On a match, drawing and
panel refresh are skipped; the panel is still put to sleep and the device
still disconnects Wi-Fi and enters deep sleep. The hash is calculated from
bounded event data, not from a retained framebuffer. RTC memory also carries
only a 32-bit marker when KEY3 is pressed during an active cycle, allowing a
safe restart into a fresh refresh. A reset that loses RTC state safely causes
one full refresh.

## Serial diagnostics and troubleshooting

At 115200 baud, serial output includes wake reason and refresh source
(cold/reset, timer, or KEY3/manual), firmware/model/panel dimensions, Wi-Fi
connection/provisioning state, NTP start/result, current London local time,
whether rendering was performed or skipped, sleep duration, and entry to deep
sleep. It does not log credentials. If the screen
shows **Wi-Fi setup required**, join its displayed setup SSID and complete
the captive portal. **Unable to connect to Wi-Fi** indicates saved-network
failure; use the displayed setup SSID to update settings. **Unable to
synchronize time** indicates Wi-Fi connected but NTP did not complete within
the timeout; verify that the network permits DNS and outbound NTP. **Calendar
feed setup required** means no private feed URL is saved; join the setup Wi-Fi
network and enter the URL through the captive portal. **Unable to load
calendar feed** is accompanied by a serial reason such as TLS/HTTP failure,
unsupported recurrence/timezone, or invalid feed contents.

## Google Calendar retrieval architecture

**Decision:** use Google's private, read-only iCalendar subscription URL directly from the device over HTTPS. The ESP32 does not perform Google OAuth, call Google's Calendar API, or use an intermediate proxy.

```text
Google Calendar private iCalendar feed
        | HTTPS, private URL
        v
XIAO ESP32-S3 -> IcalendarFeedProvider -> CalendarProvider
                                      -> Today / This Week renderer
```

This is a practical self-hosted setup for a personal/family display: there is no server to deploy or maintain, and the existing `CalendarProvider` boundary keeps the renderer independent of the feed format. The private feed URL itself is a bearer secret: anyone who obtains it may be able to read the calendar. Treat it like a password. It is entered through the existing captive portal and stored in device NVS; it is not returned by `/device-settings` or printed by calendar firmware. Do not put the URL in source code, screenshots, logs, issue reports, or Git.

### Setup and transport security

1. In Google Calendar settings, copy the private iCalendar HTTPS `.ics` URL for the calendar to display. Sharing the URL grants read access; do not use a public/published feed unless that exposure is intended.
2. Join the device's setup Wi-Fi and open its captive portal. In Advanced settings, paste the URL into the **Private iCalendar feed URL** field when initially configuring Wi-Fi or updating the feed URL. The portal masks the URL and does not display the saved value; leave it blank to preserve the current URL.
3. Save the settings through the captive portal. The URL is persisted in NVS and available to the firmware on the next refresh; no rebuild or reflash is required to change it.
4. To remove the URL, select **Clear the saved private calendar feed URL** in the portal. The screen will return to the feed setup message until a new URL is saved.
5. The firmware accepts HTTPS URLs whose path ends in `.ics`, makes a non-redirecting HTTPS GET, checks the server certificate against the embedded ESP-IDF CA bundle, and applies configured response-size and timeout limits. Certificate validation is required; the client does not disable TLS verification.

The portal uses HTTP locally, as in the upstream Wi-Fi provisioning flow, so configure it only on a trusted nearby network. NVS is not encrypted by this application; physical access to the device may expose the URL. Rotate the private URL if the device is lost, accessed by an untrusted person, or the URL is otherwise exposed. The firmware does not log the URL.

### Feed parsing and event semantics

The parser incrementally consumes the response and bounds the total feed, physical/logical line lengths, and number of visible events. It handles VEVENT UID, SUMMARY, DTSTART, DTEND, STATUS:CANCELLED, folded lines, escaped text, UTC date-times, floating/Europe-London date-times, and date-only all-day events. It filters and orders the visible time window before passing bounded `CalendarEvent` values to the shared renderer.

All-day start/end are retained as civil dates, with an exclusive end date, and are never converted to fabricated midnight timestamps. Timed values become Unix instants and are converted to `Europe/London` for display. UTC `Z` values are UTC instants, not London wall time. The feed's source timezone is respected only for `Europe/London`, UTC, or `Etc/UTC`; other TZIDs fail explicitly rather than being silently misinterpreted. A floating (no TZID) date-time is interpreted as London local time. An ambiguous London wall time during the autumn clock-change hour is rejected because it does not identify one instant; feeds that include the equivalent UTC instant avoid this ambiguity.

The parser intentionally does **not** implement all of RFC 5545. In particular, it rejects `RRULE`, `RDATE`, and `EXDATE` with a visible feed failure rather than silently omitting recurring instances or misrepresenting exceptions. Google private feeds may include recurring master components, so compatibility with the user's actual feed must be verified before relying on recurring events. The implementation also does not yet interpret `VTIMEZONE` definitions, arbitrary TZIDs, `DURATION`, or recurrence overrides. Events should include explicit `DTSTART` and `DTEND`. Feed HTTP, TLS, size, parsing, unsupported-timezone, recurrence, and capacity failures are reported as an error screen and serial diagnostic; the device does not substitute mock data for failed live-feed data.

### Future improvement boundary

Keep the current direct `.ics` architecture unless operational needs justify a proxy (for example, aggregating many calendars, guaranteed recurrence expansion, or avoiding exposure of the private URL on the device). A future proxy could normalize feeds into a small bounded format behind another `CalendarProvider`, but neither a proxy nor Google OAuth belongs in this firmware implementation.
## Power management

The calendar reuses the upstream panel sleep function, configures a 12-hour
timer wake plus the existing device-table button GPIO wake, then turns Wi-Fi
off and enters ESP32 deep sleep. It avoids panel updates when the rendered
content has not changed. The physical panel's retention makes it possible
for the ESP32 and Wi-Fi to remain off between updates.

## Development roadmap

1. **Hardware, local UI, Wi-Fi, time, and sleep:** use synchronized London
   time and refresh only when visible content changes.
2. **Direct feed validation:** test the configured private Google `.ics` URL,
   especially recurring events, feed size, and clock-change events; expand
   parser coverage only for behavior needed by real feeds.
3. **Calendar UI/data:** keep improving event aggregation, all-day and
   multi-day behavior, layout, and source labels without coupling the renderer
   to iCalendar syntax.
