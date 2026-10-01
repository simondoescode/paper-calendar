#include <calendar/weather.h>
#include <math.h>
#include <stdio.h>
namespace calendar {
WeatherCondition weatherConditionFromWmoCode(int code) {
  switch (code) {
  case 0: return WeatherCondition::Clear;
  case 1: case 2: return WeatherCondition::PartlyCloudy;
  case 3: return WeatherCondition::Cloudy;
  case 45: case 48: return WeatherCondition::Fog;
  case 51: case 53: case 55: case 56: case 57: return WeatherCondition::Drizzle;
  case 61: case 63: case 66: return WeatherCondition::Rain;
  case 65: case 67: case 80: case 81: case 82: return WeatherCondition::HeavyRain;
  case 71: case 73: case 75: case 77: case 85: case 86: return WeatherCondition::Snow;
  case 95: case 96: case 99: return WeatherCondition::Thunderstorm;
  default: return WeatherCondition::Unknown;
  }
}
const char *conditionLabel(WeatherCondition condition) {
  switch (condition) {
  case WeatherCondition::Clear: return "Clear";
  case WeatherCondition::PartlyCloudy: return "Partly cloudy";
  case WeatherCondition::Cloudy: case WeatherCondition::Overcast: return "Cloudy";
  case WeatherCondition::Fog: case WeatherCondition::Mist: return "Fog";
  case WeatherCondition::Drizzle: return "Drizzle";
  case WeatherCondition::Rain: return "Rain";
  case WeatherCondition::HeavyRain: return "Heavy rain";
  case WeatherCondition::Snow: return "Snow";
  case WeatherCondition::Thunderstorm: return "Thunderstorm";
  case WeatherCondition::Wind: return "Wind";
  default: return "Unknown";
  }
}
uint32_t weatherDisplayHash(const WeatherData &weather) {
  uint32_t hash = 2166136261UL;
  auto add = [&hash](const char *s) { while (*s) { hash ^= static_cast<uint8_t>(*s++); hash *= 16777619UL; } };
  add(weather.valid ? "valid" : "unavailable");
  if (!weather.valid) return hash;
  char values[96];
  snprintf(values, sizeof(values), "%ld/%u/%u", lroundf(weather.current.temperatureC),
           static_cast<unsigned>(weather.current.condition), weather.current.isDay);
  add(values);
  for (const auto &day : weather.forecast) {
    snprintf(values, sizeof(values), "%s/%ld/%ld/%d/%u", day.date, lroundf(day.temperatureMinC),
             lroundf(day.temperatureMaxC), day.precipitationProbability, static_cast<unsigned>(day.condition));
    add(values);
  }
  return hash;
}
} // namespace calendar
