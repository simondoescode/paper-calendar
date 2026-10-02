#if defined(EINK_CALENDAR_APP) || defined(CALENDAR_HOST)
#if defined(CALENDAR_HOST)
#undef ARDUINOJSON_ENABLE_ARDUINO_STRING
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 0
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT  0
#define ARDUINOJSON_ENABLE_PROGMEM        0
#endif
#include <ArduinoJson.h>
#include <calendar/calendar_config.h>
#include <calendar/weather_client.h>
#include <math.h>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace calendar {
  namespace {
// A fixed arena also bounds ArduinoJson 7's otherwise elastic allocations.
    class WeatherJsonAllocator : public ArduinoJson::Allocator {
    public:
      void *allocate(size_t size) override {
        const size_t aligned = (size + alignof(max_align_t) - 1) / alignof(max_align_t) * alignof(max_align_t);
        if (aligned + sizeof(Header) > sizeof(arena) - used) return nullptr;
        auto *header = reinterpret_cast<Header *>(arena + used);
        header->size = size;
        used += sizeof(Header) + aligned;
        return header + 1;
      }
      void deallocate(void *) override {}
      void *reallocate(void *ptr, size_t size) override {
        if (!ptr) return allocate(size);
        const size_t old = (static_cast<Header *>(ptr) - 1)->size;
        if (size <= old) return ptr;
        void *next = allocate(size);
        if (next) memcpy(next, ptr, old);
        return next;
      }

    private:
      struct alignas(max_align_t) Header {
        size_t size;
      };
      alignas(max_align_t) unsigned char arena[8192];
      size_t used = 0;
    };
    bool number(JsonVariantConst value) { return value.is<float>() && isfinite(value.as<float>()); }
    bool dateString(const char *value) {
      if (!value || strlen(value) != 10 || value[4] != '-' || value[7] != '-') return false;
      for (size_t i = 0; i < 10; ++i)
        if (i != 4 && i != 7 && (value[i] < '0' || value[i] > '9')) return false;
      const int month = (value[5] - '0') * 10 + value[6] - '0';
      const int day = (value[8] - '0') * 10 + value[9] - '0';
      const int year = atoi(value);
      const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      if (year < 1970 || month < 1 || month > 12 || day < 1) return false;
      const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
      return day <= days[month - 1] + (month == 2 && leap ? 1 : 0);
    }
    bool localTime(const char *value, const char *date, char *output) {
      if (!value || strlen(value) != 16 || strncmp(value, date, 10) || value[10] != 'T' || value[13] != ':')
        return false;
      for (int i : {11, 12, 14, 15})
        if (value[i] < '0' || value[i] > '9') return false;
      if (atoi(value + 11) > 23 || atoi(value + 14) > 59) return false;
      memcpy(output, value + 11, 5);
      output[5] = '\0';
      return true;
    }
  } // namespace
  bool buildWeatherUrl(char *output, size_t capacity, double latitude, double longitude) {
    if (!output || !capacity) return false;
    output[0] = '\0';
    if (!isfinite(latitude) || !isfinite(longitude) || latitude < -90 || latitude > 90 || longitude < -180 ||
        longitude > 180)
      return false;
    const int count =
      snprintf(output, capacity,
               "https://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f"
               "&current=temperature_2m,apparent_temperature,weather_code,is_day,wind_speed_10m"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
               "&timezone=auto&forecast_days=3&temperature_unit=celsius&wind_speed_unit=kmh",
               latitude, longitude);
    if (count < 0 || static_cast<size_t>(count) >= capacity) {
      output[0] = '\0';
      return false;
    }
    return true;
  }
  bool parseWeatherResponse(const char *json, size_t length, WeatherData &output, uint32_t fetchedAt) {
    if (!json || !length || length > kMaximumWeatherResponseBytes) return false;
    std::unique_ptr<WeatherJsonAllocator> allocator(new (std::nothrow) WeatherJsonAllocator);
    if (!allocator) return false;
    JsonDocument filter;
    for (const char *key : {"temperature_2m", "apparent_temperature", "weather_code", "is_day", "wind_speed_10m"})
      filter["current"][key] = true;
    for (const char *key :
         {"time", "weather_code", "temperature_2m_max", "temperature_2m_min", "precipitation_probability_max",
          "sunrise", "sunset"})
      filter["daily"][key] = true;
    JsonDocument document(allocator.get());
    if (deserializeJson(document, json, length, DeserializationOption::Filter(filter),
                        DeserializationOption::NestingLimit(4)))
      return false;
    WeatherData candidate = {};
    const JsonObjectConst current = document["current"];
    if (!number(current["temperature_2m"]) || !number(current["apparent_temperature"]) ||
        !number(current["wind_speed_10m"]) || !current["weather_code"].is<int>() || !current["is_day"].is<int>())
      return false;
    const int isDay = current["is_day"];
    if (isDay != 0 && isDay != 1) return false;
    candidate.current = {
        current["temperature_2m"],
        current["apparent_temperature"],
        current["wind_speed_10m"],
        current["weather_code"],
        isDay == 1,
        weatherConditionFromWmoCode(current["weather_code"])};
    if (fabsf(candidate.current.temperatureC) > 100 || fabsf(candidate.current.apparentTemperatureC) > 150 ||
        candidate.current.windSpeedKmh < 0 || candidate.current.windSpeedKmh > 500)
      return false;
    const JsonObjectConst daily = document["daily"];
    for (const char *key :
         {"time", "weather_code", "temperature_2m_max", "temperature_2m_min", "precipitation_probability_max",
          "sunrise", "sunset"}) {
      if (!daily[key].is<JsonArrayConst>() || daily[key].size() != kWeatherForecastDays) return false;
    }
    for (size_t i = 0; i < kWeatherForecastDays; ++i) {
      DailyWeather &day = candidate.forecast[i];
      const char *date = daily["time"][i];
      if (!dateString(date) || (i && strcmp(date, candidate.forecast[i - 1].date) <= 0) ||
          !number(daily["temperature_2m_min"][i]) || !number(daily["temperature_2m_max"][i]) ||
          !daily["weather_code"][i].is<int>() || !daily["precipitation_probability_max"][i].is<int>())
        return false;
      memcpy(day.date, date, sizeof(day.date));
      day.temperatureMinC = daily["temperature_2m_min"][i];
      day.temperatureMaxC = daily["temperature_2m_max"][i];
      day.weatherCode = daily["weather_code"][i];
      day.precipitationProbability = daily["precipitation_probability_max"][i];
      day.condition = weatherConditionFromWmoCode(day.weatherCode);
      if (fabsf(day.temperatureMinC) > 100 || fabsf(day.temperatureMaxC) > 100 ||
          day.temperatureMinC > day.temperatureMaxC || day.precipitationProbability < 0 ||
          day.precipitationProbability > 100 || !localTime(daily["sunrise"][i], date, day.sunrise) ||
          !localTime(daily["sunset"][i], date, day.sunset))
        return false;
    }
    candidate.valid = true;
    candidate.fetchedAt = fetchedAt;
    output = candidate;
    return true;
  }
} // namespace calendar

#endif
