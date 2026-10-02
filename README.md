# Paper Calendar

A standalone 7.5-inch e-paper family calendar built for the TRMNL BYOD OG DIY Kit / Seeed XIAO ESP32-S3.

The device connects directly to Wi-Fi, reads calendar data from a private HTTPS iCalendar feed, fetches weather, renders everything locally on the ESP32-S3, then returns to deep sleep. It does not require the TRMNL cloud service.

## Features

- 800×480 monochrome e-paper calendar UI
- Today and upcoming events
- All-day and timed event layouts
- Private HTTPS iCalendar / ICS feed support
- Weather forecast and icons
- Wi-Fi setup through a local captive portal
- Calendar settings stored in NVS
- 12-hour automatic refresh
- KEY3 manual refresh
- Battery, Wi-Fi and last-updated status
- Deep-sleep power management
- GitHub Releases OTA firmware updates
- Desktop host preview for UI development without hardware

## Hardware validation status

`v0.1.0` is the first pre-hardware release. The firmware builds and host tests pass in CI, but the complete flow has not yet been validated on a physical TRMNL BYOD 7.5-inch OG DIY Kit. Treat this release as an early developer release until USB flashing, captive-portal setup, display refresh, deep sleep, KEY3 wake and OTA update have been exercised on-device.

## Hardware

Primary supported target:

- TRMNL BYOD 7.5-inch OG DIY Kit
- Seeed XIAO ESP32-S3
- 7.5-inch 800×480 monochrome e-paper display

PlatformIO environment:

```text
TRMNL_7inch5_OG_DIY_Kit
```

The repository still contains some upstream TRMNL hardware support because the calendar reuses its display, Wi-Fi, board and power-management code. Paper Calendar is the default target.

## How it works

```text
Wake
 │
 ├─ Connect to Wi-Fi
 ├─ Synchronise time
 ├─ Check for firmware update when due
 ├─ Download calendar ICS feed
 ├─ Fetch weather
 ├─ Render the 800×480 display
 └─ Deep sleep for 12 hours
       │
       └─ KEY3 can wake the device early
```

Calendar and weather data are fetched directly by the device. The display image is generated locally rather than rendered by an external service.

## Getting started

### 1. Install PlatformIO

The easiest option is the PlatformIO extension for VS Code.

You can also install PlatformIO Core:

```bash
pip install platformio
```

The repository includes wrapper scripts that can locate PlatformIO even when it is not globally available.

Run all commands below from the repository root. In Windows PowerShell, if you see
`pio : The term 'pio' is not recognized`, use `.\scripts\pio.cmd` in place of
`pio` for any command below. No global PATH change is required. For example:

```powershell
.\scripts\pio.cmd --version
.\scripts\pio.cmd run -e TRMNL_7inch5_OG_DIY_Kit
.\scripts\pio.cmd run -e TRMNL_7inch5_OG_DIY_Kit -t upload
```

### 2. Build the firmware

The Paper Calendar target is the default environment:

```bash
pio run
```

Equivalent explicit command:

```bash
pio run -e TRMNL_7inch5_OG_DIY_Kit
```

If `pio` is not on your PATH:

Windows:

```powershell
.\scripts\pio.cmd run -e TRMNL_7inch5_OG_DIY_Kit
```

Linux/macOS:

```bash
bash scripts/pio.sh run -e TRMNL_7inch5_OG_DIY_Kit
```

Or:

```bash
python scripts/platformio_cli.py run -e TRMNL_7inch5_OG_DIY_Kit
```

### 3. Flash over USB

Connect the XIAO ESP32-S3 by USB and run:

```bash
pio run -t upload
```

Or explicitly:

```bash
pio run -e TRMNL_7inch5_OG_DIY_Kit -t upload
```

If PlatformIO cannot find the board automatically, list available ports:

```bash
pio device list
```

Then specify the port:

```bash
pio run -e TRMNL_7inch5_OG_DIY_Kit -t upload --upload-port <PORT>
```

The first OTA-capable firmware must be installed over USB. Later releases can update over Wi-Fi.

## Wi-Fi and calendar setup

On first boot, or when no Wi-Fi credentials are stored, the device starts its local setup portal.

Use the portal to configure:

- Wi-Fi credentials
- private iCalendar / ICS feed URL

The calendar URL is stored locally in ESP32 NVS.

For Google Calendar, use the calendar's private iCal address. Treat the URL as a secret because possession of it grants read access to that calendar.

A long KEY3 press clears saved Wi-Fi settings so the device can be reconfigured.

## Refresh behaviour

The normal calendar refresh interval is:

```text
12 hours
```

KEY3 can wake the device for an immediate manual refresh.

The firmware avoids unnecessary e-paper refreshes when the visible content has not changed.

## Weather

Weather is fetched directly by the ESP32 and cached locally.

Default coordinates are defined in:

```text
include/calendar/calendar_config.h
```

They can be overridden with build flags.

Weather icons are monochrome assets designed for the e-paper display.

## OTA updates

Paper Calendar uses GitHub Releases for OTA updates.

The device periodically checks:

```text
https://github.com/simondoescode/paper-calendar/releases/latest/download/manifest.json
```

A release contains:

```text
paper-calendar.bin
manifest.json
```

The manifest includes the firmware version, download URL, file size and SHA-256 digest.

Before installing an update the device:

1. downloads the manifest over HTTPS;
2. compares its version with the installed firmware;
3. downloads the new image into the inactive OTA partition;
4. verifies its size and SHA-256 digest;
5. switches boot partitions;
6. reboots into the new firmware.

The partition table already provides dual OTA application slots.

### Publishing a release

Firmware versions are currently defined in:

```text
include/config.h
```

Update:

```cpp
#define FW_MAJOR_VERSION 0
#define FW_MINOR_VERSION 1
#define FW_PATCH_VERSION 0
```

Then create and push a matching tag:

```bash
git tag v0.1.0
git push origin v0.1.0
```

The `calendar-release` GitHub Actions workflow builds the real calendar target and publishes the OTA assets to GitHub Releases.

## Development without hardware

The project includes a native host preview.

Build it with:

```bash
pio run -e calendar-host
```

On Windows PowerShell, build and start the emulator from the repository root:

```powershell
.\scripts\pio.cmd run -e calendar-host
.\.pio\build\calendar-host\program.exe
```

On Linux/macOS, start it with `.pio/build/calendar-host/program` after building.

Leave the terminal running and open [http://localhost:8080/](http://localhost:8080/)
in your browser to access the local setup portal. Settings and the display preview
are written to `.dev/calendar-settings.json` and `.dev/calendar-preview.png`.

The portal includes weather latitude and longitude fields in decimal degrees.
These are saved locally and used to fetch live Open-Meteo weather when the emulator
starts, settings are saved, or you refresh. Settings are not transferred to the device.
If a request fails, the host retains the last successful forecast for that location
until it exits. Without a successful forecast, it shows weather as unavailable.

Press **Ctrl+C** in the emulator terminal to stop it before rebuilding. On Windows,
a running emulator locks `program.exe` and causes an `Access is denied` build error.

This can render an 800×480 desktop preview so layout changes can be developed before the physical display arrives.

The host implementation also supports local configuration and calendar-feed development.

See [docs/EINK_CALENDAR.md](docs/EINK_CALENDAR.md) for more detailed architecture and development notes.

## Tests

Run the calendar-specific host tests with:

```bash
pio test -e native_calendar
```

GitHub Actions runs a single calendar-focused CI workflow on pull requests and pushes to `main`.

It performs:

```text
format check
    ↓
build TRMNL_7inch5_OG_DIY_Kit
    ↓
run native_calendar tests
```

The repository intentionally does not run the inherited TRMNL Ubuntu/macOS/Windows firmware matrix.

## CI and releases

There are two primary GitHub Actions workflows:

### `calendar-ci`

Runs for pull requests and pushes to `main`.

- checks formatting of changed C/C++ files;
- builds the actual Paper Calendar firmware;
- runs calendar host tests.

### `calendar-release`

Runs for version releases.

- builds `TRMNL_7inch5_OG_DIY_Kit`;
- produces `paper-calendar.bin`;
- calculates its SHA-256 digest;
- generates `manifest.json`;
- publishes the files to GitHub Releases for OTA installation.

## Project structure

```text
include/calendar/          Calendar interfaces and configuration
src/calendar/              Calendar application implementation
test/test_calendar/        Calendar host tests
docs/EINK_CALENDAR.md      Detailed architecture and development notes
data/cert/                 HTTPS certificate bundle
boards/min_spiffs.csv      ESP32 OTA partition layout
.github/workflows/
  calendar-ci.yml          Build + test
  calendar-release.yml     OTA release publishing
```

The project also retains selected upstream TRMNL source and libraries used by the supported XIAO/e-paper hardware path.

## Display

The current target uses the existing `EPD_75` profile and `bb_epaper` driver for an 800×480 monochrome panel.

The calendar UI uses a black-and-white display palette with dithering where a light-grey visual treatment is required.

## Power

The device is designed to spend most of its time in deep sleep.

Wi-Fi is enabled only while synchronising data and checking for updates. E-paper retains the last rendered image without continuous power, making long refresh intervals practical.

Battery operation is supported by the hardware, although USB power is suitable for development and initial setup.

## Upstream

Paper Calendar is based on the open-source TRMNL firmware project and retains portions of its board support, display drivers, Wi-Fi provisioning and power-management implementation.

Upstream project:

https://github.com/usetrmnl/firmware

This fork is independently adapted for the standalone Paper Calendar use case and does not require the TRMNL cloud service.
