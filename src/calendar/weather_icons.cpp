#include <calendar/weather_icons.h>

namespace calendar {

WeatherData defaultWeatherData() {
  WeatherData data = {{14, 16, 9, WeatherIcon::PartlyCloudy},
                      {{"FRI", 16, WeatherIcon::Sunny},
                       {"SAT", 15, WeatherIcon::Cloudy},
                       {"SUN", 13, WeatherIcon::Rain},
                       {"MON", 14, WeatherIcon::Cloudy},
                       {"TUE", 15, WeatherIcon::PartlyCloudy}}};
  return data;
}

namespace WeatherIcons {
namespace {

void thickLine(DisplayTarget &display, int x1, int y1, int x2, int y2, DisplayColor color) {
  display.line(static_cast<uint16_t>(x1), static_cast<uint16_t>(y1), static_cast<uint16_t>(x2),
               static_cast<uint16_t>(y2), color);
  display.line(static_cast<uint16_t>(x1), static_cast<uint16_t>(y1 + 1), static_cast<uint16_t>(x2),
               static_cast<uint16_t>(y2 + 1), color);
}

void sun(DisplayTarget &display, int x, int y, int size, DisplayColor color) {
  const int cx = x + size / 2;
  const int cy = y + size / 2;
  const int radius = size / 6;
  display.circle(cx, cy, radius, color, false);
  const int inner = size / 3;
  const int outer = size / 2 - 2;
  const int diagonalInner = inner * 7 / 10;
  const int diagonalOuter = outer * 7 / 10;
  thickLine(display, cx, cy - inner, cx, cy - outer, color);
  thickLine(display, cx, cy + inner, cx, cy + outer, color);
  thickLine(display, cx - inner, cy, cx - outer, cy, color);
  thickLine(display, cx + inner, cy, cx + outer, cy, color);
  thickLine(display, cx - diagonalInner, cy - diagonalInner, cx - diagonalOuter, cy - diagonalOuter, color);
  thickLine(display, cx + diagonalInner, cy - diagonalInner, cx + diagonalOuter, cy - diagonalOuter, color);
  thickLine(display, cx - diagonalInner, cy + diagonalInner, cx - diagonalOuter, cy + diagonalOuter, color);
  thickLine(display, cx + diagonalInner, cy + diagonalInner, cx + diagonalOuter, cy + diagonalOuter, color);
}

void cloud(DisplayTarget &display, int x, int y, int size, DisplayColor color) {
  const int base = y + size * 7 / 10;
  const int left = x + size / 8;
  const int right = x + size * 7 / 8;
  display.circle(x + size * 3 / 8, y + size / 2, size / 5, color, false);
  display.circle(x + size * 5 / 8, y + size * 2 / 5, size / 4, color, false);
  display.circle(x + size * 3 / 4, y + size * 3 / 5, size / 7, color, false);
  thickLine(display, left, base, right, base, color);
  thickLine(display, left, y + size * 3 / 5, left, base, color);
}

} // namespace

void draw(DisplayTarget &display, WeatherIcon icon, uint16_t x, uint16_t y, uint8_t size,
          DisplayColor color) {
  const int ix = x;
  const int iy = y;
  const int is = size;
  if (icon == WeatherIcon::Sunny) {
    sun(display, ix, iy, is, color);
    return;
  }
  if (icon == WeatherIcon::PartlyCloudy) {
    sun(display, ix, iy, is * 2 / 3, color);
    cloud(display, ix + is / 8, iy + is / 7, is * 7 / 8, color);
    return;
  }
  cloud(display, ix, iy, is, color);
  const int base = iy + is * 7 / 10;
  if (icon == WeatherIcon::Rain) {
    for (int i = 0; i < 3; ++i) {
      const int dropX = ix + is / 4 + i * is / 4;
      thickLine(display, dropX, base + is / 10, dropX - is / 12, base + is / 4, color);
    }
  } else if (icon == WeatherIcon::Snow) {
    for (int i = 0; i < 3; ++i) {
      const int cx = ix + is / 4 + i * is / 4;
      const int cy = base + is / 6;
      display.line(cx - 2, cy, cx + 2, cy, color);
      display.line(cx, cy - 2, cx, cy + 2, color);
    }
  } else if (icon == WeatherIcon::Fog) {
    for (int i = 0; i < 3; ++i) {
      thickLine(display, ix + is / 6, base + 3 + i * is / 9, ix + is * 5 / 6,
                base + 3 + i * is / 9, color);
    }
  }
}

} // namespace WeatherIcons
} // namespace calendar
