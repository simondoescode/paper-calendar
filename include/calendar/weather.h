#pragma once
#include <stddef.h>
#include <stdint.h>

namespace calendar {
enum class WeatherCondition : uint8_t {
  Clear, PartlyCloudy, Cloudy, Overcast, Drizzle, Rain, HeavyRain, Thunderstorm, Snow, Fog, Mist, Wind, Unknown,
};
constexpr size_t kWeatherForecastDays = 3;
struct CurrentWeather {
  float temperatureC;
  float apparentTemperatureC;
  float windSpeedKmh;
  int weatherCode;
  bool isDay;
  WeatherCondition condition;
};
struct DailyWeather {
  char date[11];
  float temperatureMinC;
  float temperatureMaxC;
  int precipitationProbability;
  int weatherCode;
  WeatherCondition condition;
  char sunrise[6];
  char sunset[6];
};
struct WeatherData {
  bool valid;
  CurrentWeather current;
  DailyWeather forecast[kWeatherForecastDays];
  uint32_t fetchedAt;
};
WeatherCondition weatherConditionFromWmoCode(int code);
const char *conditionLabel(WeatherCondition condition);
// Hash only values visible in the weather UI, excluding fetch time.
uint32_t weatherDisplayHash(const WeatherData &weather);
} // namespace calendar
