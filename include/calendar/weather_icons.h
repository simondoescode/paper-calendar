#pragma once

#include <calendar/display_target.h>
#include <calendar/weather.h>

#include <stddef.h>
#include <stdint.h>

namespace calendar {

enum class WeatherIcon : uint8_t {
  Sunny,
  Cloudy,
  PartlyCloudy,
  Rain,
  Snow,
  Fog,
  ClearNight,
  PartlyCloudyNight,
  Drizzle,
  HeavyRain,
  Thunderstorm,
  Wind,
};

WeatherIcon getWeatherIcon(WeatherCondition condition, bool isDaytime);

struct WeatherBitmap {
  const uint8_t *bitmap;
  uint16_t width;
  uint16_t height;
};

namespace WeatherIcons {

constexpr uint8_t FORECAST_SIZE = 32;
constexpr uint8_t CURRENT_SIZE = 48;
const WeatherBitmap &asset(WeatherIcon icon, uint8_t size);
void draw(DisplayTarget &display, WeatherIcon icon, uint16_t x, uint16_t y, uint8_t size,
          DisplayColor color = DisplayColor::Black);

} // namespace WeatherIcons
} // namespace calendar
