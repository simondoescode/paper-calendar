# Agent guidance

## Project scope

This repository is a TRMNL firmware fork with a standalone family-calendar
application for the XIAO ESP32-S3 / 7.5-inch OG DIY Kit. Preserve the upstream
firmware behavior and board support outside the calendar target.

Before changing calendar behavior, read `docs/EINK_CALENDAR.md` and inspect
the relevant implementation. Keep calendar domain logic separate from
hardware, persistence, networking, and host-emulator adapters.

## Hardware and firmware constraints

- Preserve the existing `TRMNL_7inch5_OG_DIY_Kit` board configuration,
  display pin map, `EPD_75` panel profile, and `bb_epaper` driver.
- Do not route the calendar target through TRMNL cloud startup or add cloud
  dependencies to the standalone lifecycle.
- Reuse the upstream captive portal, Wi-Fi credential storage, battery
  abstraction, display functions, and sleep support where practical.
- Keep calendar-specific startup scoped to `EINK_CALENDAR_APP`; do not
  unintentionally change other PlatformIO targets.
- The configured calendar source is a private HTTPS iCalendar feed. Do not
  add Google OAuth, a proxy, or other calendar networking architecture without
  an explicit request.
- Never commit Wi-Fi credentials, private calendar URLs, tokens, or other
  secrets. Host state under `.dev/` is local and Git-ignored.
- Avoid full-screen duplicate framebuffers, unbounded parsing, and unnecessary
  heap allocation on the embedded path.

## Build and test

Run commands from the repository root. Prefer the narrowest relevant check;
when changing shared calendar or device integration, run:

```sh
pio test -e native_calendar -f test_calendar
pio run -e calendar-host
pio run -e TRMNL_7inch5_OG_DIY_Kit
git diff --check
```

The native environments require a C++ compiler on `PATH` (MinGW-w64 on
Windows). When a change touches common firmware or display code, also build a
non-calendar target, for example:

```sh
pio run -e trmnl
```

Do not claim hardware validation unless firmware was actually flashed and
tested on the physical device. Native tests and host previews do not validate
panel refresh quality, GPIO wake, battery readings, Wi-Fi provisioning on the
board, or deep-sleep current.

## Host emulator

The `calendar-host` environment shares calendar model and renderer code with
the firmware and emulates configuration persistence and the 800x480
monochrome display. It is a development aid, not an ESP32 or NVS emulator.
Run the executable from the repository root so its `.dev/` paths resolve:

```sh
pio run -e calendar-host
```

Then run `.pio/build/calendar-host/program` (Linux/macOS) or
`.pio/build/calendar-host/program.exe` (Windows). The local portal is at
`http://localhost:8080`; settings and preview are written to
`.dev/calendar-settings.json` and `.dev/calendar-preview.png`.
