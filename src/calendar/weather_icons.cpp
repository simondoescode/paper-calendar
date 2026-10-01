#include <calendar/weather_icons.h>
#if defined(ARDUINO) && !defined(CALENDAR_HOST)
#include <Arduino.h>
#endif
#include <assets/weather/weather_bitmaps.h>

namespace calendar {
WeatherIcon getWeatherIcon(WeatherCondition condition, bool isDaytime) {
  switch (condition) {
  case WeatherCondition::Clear: return isDaytime ? WeatherIcon::Sunny : WeatherIcon::ClearNight;
  case WeatherCondition::PartlyCloudy:
    return isDaytime ? WeatherIcon::PartlyCloudy : WeatherIcon::PartlyCloudyNight;
  case WeatherCondition::Drizzle: return WeatherIcon::Drizzle;
  case WeatherCondition::Rain: return WeatherIcon::Rain;
  case WeatherCondition::HeavyRain: return WeatherIcon::HeavyRain;
  case WeatherCondition::Thunderstorm: return WeatherIcon::Thunderstorm;
  case WeatherCondition::Snow: return WeatherIcon::Snow;
  case WeatherCondition::Fog:
  case WeatherCondition::Mist: return WeatherIcon::Fog;
  case WeatherCondition::Wind: return WeatherIcon::Wind;
  case WeatherCondition::Cloudy:
  case WeatherCondition::Overcast:
  case WeatherCondition::Unknown: return WeatherIcon::Cloudy;
  }
  return WeatherIcon::Cloudy;
}

namespace WeatherIcons {
const WeatherBitmap &asset(WeatherIcon icon, uint8_t size) {
  const size_t index = static_cast<uint8_t>(icon) <= static_cast<uint8_t>(WeatherIcon::Wind)
      ? static_cast<uint8_t>(icon) : static_cast<uint8_t>(WeatherIcon::Cloudy);
  return size >= CURRENT_SIZE ? kWeather48[index] : kWeather32[index];
}

void draw(DisplayTarget &display, WeatherIcon icon, uint16_t x, uint16_t y, uint8_t size,
          DisplayColor color) {
  const WeatherBitmap &image = asset(icon, size);
  display.bitmap(x, y, image.bitmap, image.width, image.height, color);
}
} // namespace WeatherIcons
} // namespace calendar
