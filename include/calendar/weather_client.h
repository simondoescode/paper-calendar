#pragma once
#include <calendar/weather.h>
namespace calendar {
  bool buildWeatherUrl(char *output, size_t capacity, double latitude, double longitude);
// Transactional: invalid responses leave output (including cached data) untouched.
  bool parseWeatherResponse(const char *json, size_t length, WeatherData &output, uint32_t fetchedAt);
#if defined(EINK_CALENDAR_APP)
  bool fetchWeather(WeatherData &output, double latitude, double longitude);
  bool loadWeatherCache(WeatherData &output, double latitude, double longitude);
#endif
} // namespace calendar
