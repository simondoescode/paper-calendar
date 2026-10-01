# Calendar firmware flash audit

Audited on 2026-10-01, target `TRMNL_7inch5_OG_DIY_Kit`, baseline source revision
`760b5ed`, before the small cleanup below. Measurements come from a
fresh normal PlatformIO build, `firmware.map`, and target GNU `nm -S -C`.
No device was flashed or queried for its flash chip ID.

## Capacity and current baseline

| Measurement | Before cleanup |
| --- | ---: |
| PlatformIO application flash | 1,474,581 bytes (77.587%) |
| Actual `firmware.bin` file | 1,475,024 bytes, including image headers/alignment |
| Static DRAM | 56,288 / 327,680 bytes (17.178%) |
| Initialized DRAM / zero-initialized DRAM | 22,448 / 33,840 bytes |
| Application partition | 1,900,544 bytes (`0x1D0000`, 1.8125 MiB) |
| PlatformIO flash headroom | 425,963 bytes |
| Configured board flash | 8 MiB (`seeed_xiao_esp32s3`) |
| Documented project hardware | XIAO ESP32-S3 **Plus**, nominally 16 MiB |

The earlier ~74% measurement predates Open-Meteo. The current ~77.6% is a
percentage of **one OTA application slot**, not total physical flash. This
image is only 17.58% of 8 MiB, or 8.79% of 16 MiB. Seeed specifies
[8 MiB for S3/Sense and 16 MiB for Plus](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/).
The physical board's identity/capacity must be confirmed before increasing
the configured capacity; a local build cannot establish which board is plugged in.

`env:esp32_base` overrides the board's default 8 MiB partition layout with
`boards/min_spiffs.csv`, inherited by the calendar target:

| Partition | Offset | Size |
| --- | --- | --- |
| NVS | `0x9000` | `0x5000` (20 KiB) |
| OTA selection data | `0xE000` | `0x2000` (8 KiB) |
| app0 / ota_0 | `0x10000` | `0x1D0000` (1.8125 MiB) |
| app1 / ota_1 | `0x1E0000` | `0x1D0000` (1.8125 MiB) |
| SPIFFS | `0x3B0000` | `0x40000` (256 KiB) |
| Core dump | `0x3F0000` | `0x10000` (64 KiB) |

The table ends at 4 MiB, leaving half the configured 8 MiB unused, or
three quarters of the documented Plus's 16 MiB. It unnecessarily limits
application headroom but does not inflate the firmware itself.
`scripts/extra/post_build_seeed.py` also hardcodes `--flash_size 4MB` when
creating the merged image, while the normal application image is built for
8 MiB. Any future capacity change must reconcile this script, the board
configuration, bootloader/image flash header and partition table together.

**OTA:** keep two application slots if OTA/rollback is to remain possible.
Removing app1 gains capacity but sacrifices the normal dual-slot update
scheme. Merely uploading an application through OTA does not migrate the
partition table. Moving app1 or filesystem offsets needs a planned USB/serial
reflash/migration and configuration backup; an indiscriminate full erase
would lose private-feed settings and Wi-Fi credentials. A larger dual-slot
8/16 MiB layout can improve headroom without removing OTA. No partition or
flash-header change is part of this cleanup.

## Largest linked contributors

These are **retained input-section bytes**, including flash code, read-only
data, and initialized RAM content stored in the image. Archive rows are
disjoint; JSON/font breakdowns below overlap these rows and must not be added
again. Attribution accounts for 1,471,833 of 1,474,884 loaded-section bytes;
the remainder is linker padding/generated data. PlatformIO uses a slightly
different set of sections, hence the small difference from its headline size.

| Contributor | Retained bytes | Assessment |
| --- | ---: | --- |
| Application source objects | 141,427 | Weather parser, display, calendar and retained upstream services |
| Wi-Fi `libnet80211` | 113,666 | Required for Wi-Fi |
| C runtime `libc` | 100,580 | Formatting/scanning, conversion and runtime support |
| TCP/IP `liblwip` | 97,801 | Required networking |
| Crypto `libmbedcrypto` | 91,865 | Required TLS and Wi-Fi security |
| Captive portal `libwificaptive` | 86,736 | Preserve provisioning/credential behavior |
| Wi-Fi authentication `libwpa_supplicant` | 70,564 | Includes enterprise/EAP support; not safe to remove blindly |
| TRMNL application library `libtrmnl` | 68,089 | Cloud response parsing and log serialization dominate |
| Embedded CA certificate bundle | 63,376 | One copy; required by calendar and weather HTTPS |
| `ESPAsyncWebServer` | 58,365 | Existing captive portal, including its JSON handler |
| Wi-Fi packet processing `libpp` | 46,831 | Required radio stack |
| TLS protocol `libmbedtls_2` | 43,750 | Required HTTPS |
| `bb_epaper` | 37,768 | Required driver and compressed font support |
| Radio calibration `libphy` | 33,261 | Required Wi-Fi |
| FreeRTOS | 26,107 | Required runtime |
| ESP-IDF drivers | 24,810 | Hardware/serial support |
| mDNS | 22,915 | Enabled by the reused saved-credentials connection path |
| Arduino Wi-Fi wrapper | 22,708 | Required networking |
| HTTPClient | 16,174 | Both feed and weather use this same library |
| SPIFFS implementation + wrapper | 15,220 | Retained upstream file/image paths |
| X.509 parsing | 8,246 | Required certificate validation |
| WiFiClientSecure wrapper | 5,890 | Required HTTPS |

Other findings: PNGdec contributes only 160 bytes and JPEGDEC 400 bytes,
mostly constructor/initialization remnants. Their full decoding routines are
already garbage-collected. Bluetooth is only 367 bytes, not a complete BLE
stack. ArduinoLog is 1,436 bytes; ESP logging 1,034 bytes. OTA libraries retain
1,915 bytes of `app_update` and 817 bytes of Arduino `Update`, not a complete
running TRMNL OTA workflow. Dependency-list entries and compiled object sizes
are not evidence that their full contents occupy firmware.

Largest individual named symbols (aliases counted once):

| Symbol | Bytes |
| --- | ---: |
| Captive portal `INDEX_HTML` | 12,426 |
| `_vfprintf_r` | 12,250 |
| `_svfprintf_r` | 11,979 |
| Captive portal JSON settings callback | 9,555 |
| `__ssvfscanf_r` | 9,341 |
| `serialize_log(LogWithDetails const&)` | 8,973 |
| `_vfiprintf_r` / `_svfiprintf_r` | 8,354 / 8,130 |
| `__ssvfiscanf_r` | 8,077 |
| ArduinoJson String-input object parser | 7,610 |
| ArduinoJson byte-pointer object parser | 7,469 |
| TLS server / client handshake steps | 6,626 / 6,150 |
| Weather response parser | 5,473 |

The certificate blob is larger than these symbols but has start/end symbols
without a useful `nm` size; its 63,376 bytes are established by the map.

## Fonts, weather and duplicate assets

| Font asset | Linked bytes | Use |
| --- | ---: | --- |
| Manrope Bold 25 | 3,646 | Main weekday/date role |
| Manrope Bold 13 | 2,388 | Headings and weather temperature |
| Manrope SemiBold 11 | 2,253 | Event titles |
| Manrope Medium 10 | 2,101 | Metadata |
| Manrope Medium 8 | 1,913 | Footer/status |
| nicoclean 8 | 1,902 | Retained upstream SPI display message paths |
| Driver built-in `ucFont` / `ucSmallFont` | 672 / 480 | Built-in drawing fallbacks retained in bb_epaper |
| Inter 18 | **0** | Header has 7,599 bytes; linker discards it for this SPI target |
| Roboto Black 24 | **0** | Header has 3,451 bytes; QA font is discarded |

Five Manrope assets use three weights, totaling **12,301 bytes**. All six
project font assets total **14,203 bytes**, less than 1% of firmware. Two
additional built-in driver tables bring all eight font assets to **15,355
bytes**. The project fonts use
PROGMEM/compressed Group 5 assets, not runtime TTF loading. The size suffixes
are converter point sizes, not displayed pixel heights; see `include/fonts/README.md`.
Each linked font appears once. Deleting Inter/Roboto from the repository
would save **zero flash** here and break or limit other supported targets.
Previously superseded Manrope headers are already absent from this working tree.

Weather has twelve icons at each of 32x32 and 48x48: **4,992 bytes** of
bitmaps plus **192 bytes** of lookup tables, one copy per size. SVGs, licenses
and generator tooling are development inputs and contribute **zero firmware
bytes**. The two bitmap sizes are intentional display resolutions, not
duplicate payloads. Payload hashes of all linked font/weather/logo/QR/portal
assets found no identical duplicate assets. Linked `logo_small` is 149 bytes;
`wifi_failed_qr` is 411 bytes. Do not delete shared images still used upstream.

## Why bypassing TRMNL startup has not removed all cloud code

`main.cpp` correctly selects calendar startup and skips `bl_process()`.
Nevertheless `button.cpp`, display and Wi-Fi helpers use `Log_info` /
`Log_error`. These reach `app_logger.cpp::log_impl`, whose store/submit branch
references `bl.cpp::logWithAction`. That function builds TRMNL status data,
serializes JSON, and can call cloud log submission. Its error paths retain
additional API/setup/file/display code. Consequently, this is more than
unreferenced source files being compiled: these functions are actually linked
through a shared logging dependency. Any future isolation should also prove
that calendar error logging makes no TRMNL cloud request.

The map retains 31,834 bytes from cloud display-response parsing, 23,350 from
cloud log serialization, and 6,858 from setup-response parsing. JSON template
sections total approximately **127,301 bytes**, spread across weather, portal
and TRMNL objects. This is one ArduinoJson library instantiated for several
reader/filter types, not several installed JSON libraries. The weather object
alone accounts for 53,450 bytes including its emitted templates; its named
parser function is only 5,473 bytes. Cutting JSON blindly would break the
weather parser and captive portal.

Several global constructors survive garbage collection even without runtime
calls. `firmwareUpdateService` has a 1,040-byte BSS object and only its
27-byte constructor remains; its update methods are absent. The environment
sensor global retains its virtual methods and about 5.9 KiB of temperature
library code, despite this board having no configured sensor I2C pins. Shared
cloud response paths also reference the sensor interface, so removing that
entire interface requires more than deleting an unused source file.

## Build flags and diagnostics

The calendar environment explicitly uses `framework = arduino`, rather than
the global `arduino, espidf` setting. Its build therefore does not use
`src/CMakeLists.txt`'s recursive IDF component registration or
`src/idf_component.yml` to bring in ESP-SR, modem, DSP or other IDF-managed
components. Those files serve other firmware environments and must be kept.
PlatformIO's default source selection still compiles the upstream `src/`
tree; function/data section garbage collection removes most unused code.
Broad source exclusions alone will not solve the logger's actual cloud
references and may cause missing symbols. Keep the shared hardware adapters.

The inherited `build_type = debug` selects PlatformIO's default debugging
optimization (`-Og`) rather than a production size-focused build. This is a
credible code-size contributor, particularly for ArduinoJson templates.
[PlatformIO documents the debug flags](https://docs.platformio.org/en/stable/projectconf/sections/env/options/debug/debug_build_flags.html).
The target also inherits `CORE_DEBUG_LEVEL=0`; optional `DEV_FIRMWARE`,
`WAIT_FOR_SERIAL` and `DO_NOT_LIGHT_SLEEP` lines are commented out.

The ELF is approximately 29 MB because it contains symbols, DWARF debug data
and Xtensa metadata. These are not written into `firmware.bin`. Removing
`-g` alone is not a flash optimization. Likewise GNU `size`'s aggregate BSS
can include linker dummy/address-reservation sections; it is not the same as
PlatformIO's 56,288-byte static DRAM figure. Runtime TLS buffers, the display
buffer, weather's bounded heap allocations and other heap usage are additional
to that static figure and require on-device measurement.

## Recommended order and estimated savings

Estimates are prospective, not measurements, except the cleanup results below.
Several savings overlap; do not sum the ranges.

| Order | Proposal | Estimated saving | Risk / OTA effect |
| --- | --- | --- | --- |
| 1 | Stop constructing the unused updater global on calendar builds only | Tens to hundreds of flash bytes; 1,040 static RAM bytes | Very low; update methods already absent, OTA partition/rollback functions retained |
| 2 | Compare a calendar-only release/`-Os` build | 20–80 KiB flash, provisional | Requires functional/hardware validation; no partition change |
| 3 | Give calendar builds serial-only shared logging, then let GC remove cloud serialization/API dependencies | 70–120 KiB flash | Medium: inspect error paths, retain provisioning and diagnostics; do not alter upstream logging |
| 4 | After isolating logging, exclude unused cloud sources/globals and sensor objects on calendar builds | 8–20 KiB additional flash, some RAM | Shares savings with row 3; preserve board/display/battery support and any deliberately required OTA entry points |
| 5 | Remove calendar references to upstream image cache/SPIFFS paths | 15–25 KiB flash | Overlaps row 3; check filesystem use before removing; leave partitions until a separate migration |
| 6 | Consolidate required ArduinoJson reader/filter instantiations | 10–25 KiB flash, provisional | Medium; keep bounded parsing and settings validation; overlaps release/logging gains |
| 7 | Build a reduced CA bundle with verified calendar/weather trust chains | 20–50 KiB flash, provisional | Higher maintenance risk: feed host and certificate rotations; never disable TLS verification |
| 8 | Confirm hardware, then adopt a larger calendar-only dual-OTA partition layout | **0 firmware bytes**, substantial slot headroom | Requires partition/header/merge-script migration; preserve two OTA slots and settings |

Smaller optional items: removing mDNS would put approximately 23.7 KiB at
stake but changes hostname discovery; keep it unless that behavior is
explicitly waived. Slimming selected non-displayed weather variants saves at
most hundreds of bytes and changes the icon API. Deleting unused headers,
SVGs or already-discarded source functions saves zero firmware bytes.
Fonts and weather bitmaps are not worthwhile first targets.

## Minimal cleanup and verification

Only the unused calendar updater global construction is removed. Its source
and extern declaration remain available to all upstream firmware targets.
No fonts, SVGs, driver, partition, provisioning, TLS, logging behavior or
optimization flags are changed. The cleanup does not introduce an OTA path
into the standalone lifecycle or remove a previously active calendar OTA path.

| Measurement | Before | After | Saving |
| --- | ---: | ---: | ---: |
| PlatformIO flash | 1,474,581 | 1,474,525 | **56 bytes** |
| Application slot usage | 77.587% | 77.584% | Both round to 77.6% |
| Static DRAM | 56,288 | 55,248 | **1,040 bytes** |
| Static DRAM usage | 17.178% | 16.860% | 0.318 percentage points |
| `firmware.bin` | 1,475,024 | 1,474,960 | 64 bytes including alignment |

The after-cleanup ELF contains neither `firmwareUpdateService` nor its
constructor. Map attribution removes 27 bytes from the updater constructor
and 30 bytes from global initialization; linker alignment makes the net
PlatformIO saving 56 bytes. This deliberately small cleanup is primarily a
RAM improvement; reducing flash materially requires the separately scoped
optimizations above.

Validation: calendar firmware build, 29/29 native calendar tests, host build,
upstream `trmnl` build and `git diff --check` all passed. Upstream output
remains 1,476,132 flash bytes and 40,504 static DRAM bytes, matching its
previous build. Hardware/OTA flashing has not been performed.

## Reproducing the attribution

Run from the repository root after building:

```powershell
pio run -e TRMNL_7inch5_OG_DIY_Kit
pio run -e TRMNL_7inch5_OG_DIY_Kit -t size
python tools/audit_firmware_size.py --toolchain C:/Users/simon/.platformio/packages/toolchain-xtensa-esp32s3/bin --output .dev/flash-audit.json
```

Use a Python installation on PATH, and substitute your PlatformIO toolchain
path. The script parses only live loaded input sections, not the map's
discarded-section or cross-reference lists. Archive totals include retained
initialized-data payloads; symbol totals avoid counting constructor aliases
as independent implementations. Local baseline and after-cleanup attribution
files are kept under ignored `.dev/`, not committed generated maps or ELFs.
