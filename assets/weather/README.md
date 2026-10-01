# Lucide weather bitmaps

Original SVGs in `lucide/` come from the official
[Lucide repository](https://github.com/lucide-icons/lucide), pinned to the
commit in `lucide/REVISION`. `lucide/LICENSE` preserves the full ISC and
Feather MIT notices. No attribution is drawn on the device.

Icons: sun, moon, cloud-sun, cloud-moon, cloud, cloud-drizzle, cloud-rain,
cloud-rain-wind, cloud-lightning, cloud-snow, cloud-fog, wind. Cloudy,
overcast, and unknown conditions use cloud; fog and mist use cloud-fog.
`getWeatherIcon()` maps normalised conditions and daytime/nighttime;
provider codes belong in provider adapters, not the renderer.

From the repository root, using a development Python environment:

```sh
python -m pip install -r tools/requirements-weather-icons.txt
python tools/generate_weather_icons.py --preview .dev/weather-icons.png
```

The script uses resvg at 4x resolution, downsamples with Lanczos, then
thresholds to pure black/white. Lucide's original rounded 2-unit strokes
are preserved (about 2.7 px at 32 px and 4 px at 48 px). Source SVGs remain
unchanged. Zero bits are transparent and one bits are foreground, matching
`bb_epaper::drawSprite`: row-major, MSB first, byte-aligned rows, pitch
`(width + 7) / 8`. Firmware draws into the existing display buffer; the host
uses identical bit ordering and clips at its framebuffer boundaries.

Generated `include/assets/weather/weather_bitmaps.h` contains 32x32 forecast
and 48x48 current-weather assets: 4,992 bitmap bytes plus small descriptor
tables. Firmware needs neither Python nor SVG parsing. Regeneration uses
only the vendored SVGs and does not access the network.
