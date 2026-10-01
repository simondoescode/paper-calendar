#pragma once

#include <calendar/display_target.h>

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
};

struct CurrentWeather {
  int temperature;
  int high;
  int low;
  WeatherIcon icon;
};

struct DailyForecast {
  const char *weekday;
  int temperature;
  WeatherIcon icon;
};

struct WeatherData {
  CurrentWeather current;
  DailyForecast forecast[5];
};

WeatherData defaultWeatherData();

namespace WeatherIcons {

constexpr uint8_t ICON_SIZE = 40;
void draw(DisplayTarget &display, WeatherIcon icon, uint16_t x, uint16_t y, uint8_t size,
          DisplayColor color = DisplayColor::Black);

} // namespace WeatherIcons
} // namespace calendar
