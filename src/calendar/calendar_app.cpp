#include <Arduino.h>
#include <ArduinoLog.h>
#include <Preferences.h>
#include <button.h>
#include <calendar/calendar.h>
#include <calendar/calendar_config.h>
#include <calendar/calendar_app.h>
#include <calendar/calendar_renderer.h>
#include <calendar/clock_service.h>
#include <calendar/icalendar_feed_provider.h>
#include <calendar/settings.h>
#include <calendar/wake_reason.h>
#include <calendar/wifi_manager.h>
#include <config.h>
#include <display.h>
#include <esp_sleep.h>
#include <globals.h>
#include <misc/buzzer.h>
#include <pins.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory>

extern TRMNL_DEVICE *pDevice;

namespace {

const char *const kWeekdayNames[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char *const kMonthNames[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                   "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
const char *const kFullWeekdayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
                                         "Saturday"};
const char *const kFullMonthNames[] = {"January", "February", "March", "April", "May", "June",
                                       "July", "August", "September", "October", "November", "December"};
RTC_DATA_ATTR uint32_t gLastDisplayHash = 0;
RTC_DATA_ATTR uint32_t gLastDisplayMarker = 0;
RTC_NOINIT_ATTR volatile uint32_t gPendingManualRefreshMarker;
constexpr uint32_t kDisplayHashMarker = 0x43414C31;
constexpr uint32_t kPendingManualRefreshValue = 0x4B455933;
uint32_t gRefreshIntervalSeconds = calendar::AUTO_REFRESH_INTERVAL_SECONDS;

void IRAM_ATTR onKey3Pressed() { gPendingManualRefreshMarker = kPendingManualRefreshValue; }

class EpaperDisplayTarget : public calendar::DisplayTarget {
public:
  bool begin() override { return display_calendar_begin(); }
  void text(uint16_t x, uint16_t y, const char *value, uint8_t size) override {
    display_calendar_text(x, y, value, size);
  }
  void line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) override {
    display_calendar_line(x1, y1, x2, y2);
  }
  bool refresh() override { return display_calendar_refresh(); }
};

bool drawCalendar(const calendar::CalendarEvent *events, size_t count, calendar::CalendarDate date,
                  int16_t batteryTenthsVolts) {
  char firmwareVersion[48];
  if (FW_COMMIT[0] != '\0') {
    snprintf(firmwareVersion, sizeof(firmwareVersion), "%s-%s", FW_VERSION_STRING, FW_COMMIT);
  } else {
    snprintf(firmwareVersion, sizeof(firmwareVersion), "%s", FW_VERSION_STRING);
  }
  EpaperDisplayTarget display;
  return calendar::renderCalendar(display, events, count, date, firmwareVersion, DEVICE_MODEL,
                                  batteryTenthsVolts);
}

const char *statusText(calendar::DisplayStatus status) {
  switch (status) {
  case calendar::DisplayStatus::WifiSetupRequired:
    return "Wi-Fi setup required";
  case calendar::DisplayStatus::WifiConnectionFailed:
    return "Unable to connect to Wi-Fi";
  case calendar::DisplayStatus::TimeSyncFailed:
    return "Unable to synchronize time";
  case calendar::DisplayStatus::CalendarFeedSetupRequired:
    return "Calendar feed setup required";
  case calendar::DisplayStatus::CalendarFeedFailed:
    return "Unable to load calendar feed";
  case calendar::DisplayStatus::Calendar:
    return "E-Ink Calendar";
  }
  return "Calendar error";
}

void drawStatus(calendar::DisplayStatus status, const char *provisioningSsid, const char *detail) {
  display_calendar_text(32, 24, "E-Ink Calendar", 24);
  display_calendar_line(32, 76, 768, 76);
  display_calendar_text(32, 124, statusText(status), 24);
  if (status == calendar::DisplayStatus::WifiSetupRequired) {
    char instruction[80];
    snprintf(instruction, sizeof(instruction), "Connect to setup Wi-Fi: %.42s", provisioningSsid);
    display_calendar_text(32, 175, instruction, 18);
    display_calendar_text(32, 207, "Then follow the captive portal instructions.", 18);
  } else if (status == calendar::DisplayStatus::WifiConnectionFailed) {
    display_calendar_text(32, 175, "Check the network, or join the device setup Wi-Fi", 18);
    char instruction[80];
    snprintf(instruction, sizeof(instruction), "Setup Wi-Fi: %.55s", provisioningSsid);
    display_calendar_text(32, 207, instruction, 18);
  } else if (status == calendar::DisplayStatus::TimeSyncFailed) {
    display_calendar_text(32, 175, "Wi-Fi is connected, but NTP time was not available.", 18);
    display_calendar_text(32, 207, "The calendar date is not being shown as valid.", 18);
  } else if (status == calendar::DisplayStatus::CalendarFeedSetupRequired) {
    display_calendar_text(32, 175, "Open the setup Wi-Fi portal and paste your private .ics URL.", 18);
    display_calendar_text(32, 207, "Keep this URL private; it grants calendar read access.", 18);
  } else if (status == calendar::DisplayStatus::CalendarFeedFailed) {
    display_calendar_text(32, 175, detail == nullptr ? "Check the private .ics URL and feed contents." : detail, 18);
    display_calendar_text(32, 207, "No events were shown; the device will retry after sleep.", 18);
  }
  display_calendar_text(32, 455, "The device will retry automatically after sleep.", 8);
}

bool renderIfChanged(calendar::DisplayStatus status, calendar::CalendarDate date,
                     const calendar::CalendarEvent *events, size_t eventCount, int16_t batteryTenthsVolts,
                     const char *provisioningSsid = "", const char *detail = "") {
  const uint32_t stateHash =
    calendar::displayStateHash(status, date, events, eventCount, batteryTenthsVolts);
  if (gLastDisplayMarker == kDisplayHashMarker && gLastDisplayHash == stateHash) {
    Serial.println("Calendar render skipped: visible content unchanged");
    return true;
  }

  if (status == calendar::DisplayStatus::Calendar) {
    if (!drawCalendar(events, eventCount, date, batteryTenthsVolts)) {
      Serial.println("Calendar render failed: display target refresh failed");
      return false;
    }
  } else {
    if (!display_calendar_begin()) {
      Serial.println("Calendar render failed: display buffer allocation failed");
      return false;
    }
    drawStatus(status, provisioningSsid, detail);
    if (!display_calendar_refresh()) {
      Serial.println("Calendar render failed: e-paper refresh failed");
      return false;
    }
  }
  gLastDisplayHash = stateHash;
  gLastDisplayMarker = kDisplayHashMarker;
  Serial.println("Calendar render performed");
  return true;
}

void enterDeepSleep(bool displayInitialized, calendar::WifiManager &wifiManager) {
  if (displayInitialized) {
    display_sleep();
  }
  wifiManager.disconnect();
  const uint64_t sleepMicros = static_cast<uint64_t>(gRefreshIntervalSeconds) * 1000000ULL;
  esp_sleep_enable_timer_wakeup(sleepMicros);
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  if (pDevice != nullptr) {
    pins_init();
    const esp_err_t keyWakeResult =
      esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(pDevice->interrupt_pin), 0);
    if (keyWakeResult != ESP_OK) {
      Serial.printf("KEY3 GPIO wake setup failed: %d\n", static_cast<int>(keyWakeResult));
    }
  } else {
    Serial.println("KEY3 GPIO wake unavailable: board device configuration is missing");
  }
  if (gPendingManualRefreshMarker == kPendingManualRefreshValue) {
    Serial.println("KEY3 pressed while awake; restarting into an immediate refresh");
    Serial.flush();
    ESP.restart();
  }
#else
  Serial.println("KEY3 GPIO wake unavailable: this calendar wake implementation requires ESP32-S3");
#endif
  Serial.printf("Sleeping for %lu seconds\n",
                static_cast<unsigned long>(gRefreshIntervalSeconds));
  Serial.println("Entering deep sleep");
  Serial.flush();
  esp_deep_sleep_start();
}

} // namespace

void calendar_app_setup() {
  Log.begin(LOG_LEVEL_INFO, &Serial);
  std::unique_ptr<calendar::ConfigStore> configStore(calendar::createEspConfigStore());
  calendar::CalendarSettings calendarSettings = calendar::defaultCalendarSettings();
  if (!configStore || !configStore->load(calendarSettings)) {
    Serial.println("Calendar preferences invalid; using safe defaults");
    calendarSettings = calendar::defaultCalendarSettings();
  }
  gRefreshIntervalSeconds = calendarSettings.refreshIntervalSeconds;
  const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  const bool timerWake = wakeCause == ESP_SLEEP_WAKEUP_TIMER;
  const bool gpioWake = wakeCause == ESP_SLEEP_WAKEUP_GPIO || wakeCause == ESP_SLEEP_WAKEUP_EXT0 ||
                        wakeCause == ESP_SLEEP_WAKEUP_EXT1;
  const bool pendingManualRefresh = gPendingManualRefreshMarker == kPendingManualRefreshValue;
  gPendingManualRefreshMarker = 0;
  const calendar::WakeReason wakeReason =
    calendar::classifyWakeReason(timerWake, gpioWake || pendingManualRefresh);
  switch (wakeReason) {
  case calendar::WakeReason::ColdBootOrReset:
    Serial.println("Calendar boot; wake reason cold boot/reset");
    break;
  case calendar::WakeReason::Timer:
    Serial.println("Calendar boot; wake reason timer");
    break;
  case calendar::WakeReason::Key3:
    Serial.println("Calendar boot; wake reason KEY3/manual refresh");
    break;
  }

  if (!preferences.begin("data", false)) {
    Serial.println("Calendar startup failed: could not open device preferences");
    calendar::WifiManager wifiManager;
    enterDeepSleep(false, wifiManager);
    return;
  }
  display_init();
  pins_init();
  buzzer().init();
  if (wakeReason == calendar::WakeReason::Key3) {
    const ButtonPressResult buttonResult = read_button_presses();
    if (buttonResult == LongPress || buttonResult == SoftReset) {
      Serial.println("KEY3 long press: clearing saved Wi-Fi settings for reconfiguration");
      calendar::WifiManager wifiManager;
      wifiManager.clearSavedCredentials();
    }
  }
  if (pDevice != nullptr) {
    attachInterrupt(digitalPinToInterrupt(pDevice->interrupt_pin), onKey3Pressed, FALLING);
  } else {
    Serial.println("KEY3 manual refresh unavailable: board device configuration is missing");
  }
  preferences.end();
  Serial.printf("E-Ink Calendar firmware %s (%s), model %s, resolution %ux%u\n", FW_VERSION_STRING, FW_COMMIT,
                DEVICE_MODEL, static_cast<unsigned>(display_width()), static_cast<unsigned>(display_height()));

  const float batteryVoltage = display_battery_voltage();
  const int16_t batteryTenthsVolts =
    batteryVoltage >= 0.0f ? static_cast<int16_t>(lroundf(batteryVoltage * 10.0f)) : -1;
  calendar::WifiManager wifiManager;
  bool connected = false;

  if (wifiManager.hasSavedCredentials()) {
    Serial.println("Wi-Fi connecting with saved credentials");
    connected = wifiManager.connectWithSavedCredentials();
    if (connected) {
      Serial.println("Wi-Fi connected");
    } else {
      Serial.println("Wi-Fi connection failed");
      char provisioningSsid[40] = {};
      wifiManager.getProvisioningSsid(provisioningSsid, sizeof(provisioningSsid));
      renderIfChanged(calendar::DisplayStatus::WifiConnectionFailed, {}, nullptr, 0, -1, provisioningSsid);
      Serial.println("Starting Wi-Fi provisioning portal");
      connected = wifiManager.openProvisioningPortal();
    }
  } else {
    Serial.println("No saved Wi-Fi credentials");
    char provisioningSsid[40] = {};
    wifiManager.getProvisioningSsid(provisioningSsid, sizeof(provisioningSsid));
    renderIfChanged(calendar::DisplayStatus::WifiSetupRequired, {}, nullptr, 0, -1, provisioningSsid);
    Serial.println("Starting Wi-Fi provisioning portal");
    connected = wifiManager.openProvisioningPortal();
  }

  if (!connected) {
    enterDeepSleep(true, wifiManager);
    return;
  }

  if (!configStore->load(calendarSettings)) {
    Serial.println("Calendar preferences invalid after Wi-Fi setup; using safe defaults");
    calendarSettings = calendar::defaultCalendarSettings();
  }
  gRefreshIntervalSeconds = calendarSettings.refreshIntervalSeconds;

  calendar::ClockService clock;
  Serial.println("Starting NTP synchronization");
  if (!clock.synchronize(calendarSettings.timezone)) {
    Serial.println("NTP synchronization failed");
    renderIfChanged(calendar::DisplayStatus::TimeSyncFailed, {}, nullptr, 0, -1);
    enterDeepSleep(true, wifiManager);
    return;
  }

  int64_t nowEpoch = 0;
  calendar::CalendarDate today = {};
  unsigned hour = 0;
  unsigned minute = 0;
  if (!clock.now(nowEpoch, today, hour, minute)) {
    Serial.println("NTP reported success but local time is invalid");
    renderIfChanged(calendar::DisplayStatus::TimeSyncFailed, {}, nullptr, 0, -1);
    enterDeepSleep(true, wifiManager);
    return;
  }
  Serial.printf("London local time: %04d-%02u-%02u %02u:%02u\n", today.year, today.month, today.day, hour, minute);

  char calendarFeedUrl[512] = {};
  bool haveCalendarFeed = false;
  if (wifiManager.getCalendarFeedUrl(calendarFeedUrl, sizeof(calendarFeedUrl))) {
    haveCalendarFeed = true;
  } else if (calendarSettings.calendarUrl[0] != '\0') {
    strncpy(calendarFeedUrl, calendarSettings.calendarUrl, sizeof(calendarFeedUrl) - 1);
    haveCalendarFeed = true;
  }

  if (!haveCalendarFeed) {
    Serial.println("Private calendar feed URL is not configured");
    renderIfChanged(calendar::DisplayStatus::CalendarFeedSetupRequired, today, nullptr, 0, batteryTenthsVolts);
    Serial.println("Starting Wi-Fi portal for private iCalendar feed setup");
    wifiManager.openProvisioningPortal();
    enterDeepSleep(true, wifiManager);
    return;
  }

  const calendar::CalendarRange feedRange = {
    calendar::calendarTodayRange(today).startEpoch,
    calendar::calendarRestOfWeekRange(today).endEpoch,
    today,
    calendar::calendarRestOfWeekRange(today).endDate,
  };
  calendar::IcalendarFeedProvider provider(calendarFeedUrl, feedRange);
  calendar::CalendarEvent events[calendar::kMaxEvents] = {};
  const size_t eventCount = provider.loadEvents(events, calendar::kMaxEvents);
  if (provider.error() != calendar::IcalendarError::None) {
    const char *error = calendar::icalendarErrorMessage(provider.error());
    Serial.printf("iCalendar feed load failed: %s\n", error);
    renderIfChanged(calendar::DisplayStatus::CalendarFeedFailed, today, nullptr, 0, -1, "", error);
    enterDeepSleep(true, wifiManager);
    return;
  }
  Serial.printf("Loaded %u visible iCalendar events\n", static_cast<unsigned>(eventCount));
  renderIfChanged(calendar::DisplayStatus::Calendar, today, events, eventCount, batteryTenthsVolts);
  enterDeepSleep(true, wifiManager);
}
