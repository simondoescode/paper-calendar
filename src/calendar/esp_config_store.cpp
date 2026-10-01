#include <calendar/settings.h>

#if defined(ARDUINO_ARCH_ESP32)

#include <Preferences.h>

namespace calendar {

class EspConfigStore : public ConfigStore {
public:
  bool load(CalendarSettings &settings) override {
    settings = defaultCalendarSettings();
    Preferences preferences;
    if (!preferences.begin("data", true)) {
      return false;
    }
    const String url = preferences.getString("api_url", "");
    const uint32_t interval =
      preferences.getUInt("calendar_interval", settings.refreshIntervalSeconds);
    const String timezone = preferences.getString("calendar_tz", settings.timezone);
    preferences.end();
    if (url.length() >= sizeof(settings.calendarUrl) || timezone.length() >= sizeof(settings.timezone)) {
      return false;
    }
    url.toCharArray(settings.calendarUrl, sizeof(settings.calendarUrl));
    timezone.toCharArray(settings.timezone, sizeof(settings.timezone));
    settings.refreshIntervalSeconds = interval;
    return validateCalendarSettings(settings, nullptr, 0);
  }

  bool save(const CalendarSettings &settings) override {
    if (!validateCalendarSettings(settings, nullptr, 0)) {
      return false;
    }
    Preferences preferences;
    if (!preferences.begin("data", false)) {
      return false;
    }
    const bool saved = preferences.putString("api_url", settings.calendarUrl) > 0 &&
                       preferences.putUInt("calendar_interval", settings.refreshIntervalSeconds) > 0 &&
                       preferences.putString("calendar_tz", settings.timezone) > 0;
    preferences.end();
    return saved;
  }
};

ConfigStore *createEspConfigStore() { return new EspConfigStore(); }

} // namespace calendar

#endif
