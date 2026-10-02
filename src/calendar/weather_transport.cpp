#if defined(EINK_CALENDAR_APP) && !defined(CALENDAR_HOST)
#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <calendar/calendar_config.h>
#include <calendar/weather_client.h>
#include <memory>
#include <time.h>

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_data_cert_x509_crt_bundle_bin_start");
namespace calendar {
  namespace {
    struct CachedResponse {
      uint32_t version;
      uint32_t fetchedAt;
      double latitude;
      double longitude;
      uint32_t length;
      char json[kMaximumWeatherResponseBytes + 1];
    };
    bool saveCache(const CachedResponse &response) {
      Preferences store;
      if (!store.begin("cal-weather", false)) return false;
      const bool saved = store.putBytes("response", &response, sizeof(response)) == sizeof(response);
      store.end();
      return saved;
    }
  } // namespace
  bool loadWeatherCache(WeatherData &output, double latitude, double longitude) {
    Preferences store;
    if (!store.begin("cal-weather", true)) return false;
    std::unique_ptr<CachedResponse> cached(new (std::nothrow) CachedResponse{});
    const bool loaded = cached && store.getBytesLength("response") == sizeof(CachedResponse) &&
                        store.getBytes("response", cached.get(), sizeof(CachedResponse)) == sizeof(CachedResponse);
    store.end();
    if (!loaded || cached->version != 1 || cached->latitude != latitude || cached->longitude != longitude ||
        cached->length > kMaximumWeatherResponseBytes ||
        !parseWeatherResponse(cached->json, cached->length, output, cached->fetchedAt))
      return false;
    Serial.println("[weather] Loaded cached forecast");
    return true;
  }
  bool fetchWeather(WeatherData &output, double latitude, double longitude) {
    char url[512];
    if (!buildWeatherUrl(url, sizeof(url), latitude, longitude)) {
      Serial.println("[weather] Invalid coordinates");
      return false;
    }
    Serial.println("[weather] Fetching Open-Meteo");
    WiFiClientSecure tls;
    tls.setCACertBundle(rootca_crt_bundle_start);
    tls.setHandshakeTimeout(kWeatherTimeoutMs / 1000);
    HTTPClient http;
    http.useHTTP10(true); // Bounded raw stream, without chunked-transfer decoding.
    http.setConnectTimeout(kWeatherTimeoutMs);
    http.setTimeout(kWeatherTimeoutMs);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(tls, url)) {
      Serial.println("[weather] HTTPS initialization failed");
      return false;
    }
    const int status = http.GET();
    Serial.printf("[weather] HTTP %d\n", status);
    if (status != HTTP_CODE_OK) {
      http.end();
      Serial.println("[weather] Request failed; retaining cache");
      return false;
    }
    const int expected = http.getSize();
    if (expected > static_cast<int>(kMaximumWeatherResponseBytes)) {
      http.end();
      Serial.println("[weather] Response too large");
      return false;
    }
    std::unique_ptr<CachedResponse> response(new (std::nothrow) CachedResponse{});
    if (!response) {
      http.end();
      Serial.println("[weather] Allocation failed");
      return false;
    }
    Stream *stream = http.getStreamPtr();
    const uint32_t started = millis();
    bool complete = false;
    while (millis() - started < kWeatherTimeoutMs) {
      const int available = stream->available();
      if (available > 0) {
        if (response->length == kMaximumWeatherResponseBytes) break;
        const size_t remaining = kMaximumWeatherResponseBytes - response->length;
        const size_t amount = static_cast<size_t>(available) < remaining ? available : remaining;
        const int received = stream->readBytes(response->json + response->length, amount);
        if (received <= 0) break;
        response->length += received;
      }
      if (expected >= 0 && response->length == static_cast<size_t>(expected)) {
        complete = true;
        break;
      }
      if (!http.connected() && !stream->available()) {
        complete = expected < 0;
        break;
      }
      delay(1);
    }
    http.end();
    response->version = 1;
    response->latitude = latitude;
    response->longitude = longitude;
    response->fetchedAt = static_cast<uint32_t>(time(nullptr));
    if (!complete || !parseWeatherResponse(response->json, response->length, output, response->fetchedAt)) {
      Serial.println("[weather] Incomplete or invalid response; retaining cache");
      return false;
    }
    Serial.printf("[weather] %.1fC, code=%d, condition=%s\n", output.current.temperatureC, output.current.weatherCode,
                  conditionLabel(output.current.condition));
    if (!saveCache(*response)) Serial.println("[weather] Cache write failed; using fresh forecast this cycle");
    return true;
  }
} // namespace calendar
#endif
