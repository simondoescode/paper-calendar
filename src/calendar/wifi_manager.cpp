#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WifiCaptive.h>
#include <calendar/calendar_config.h>
#include <calendar/wifi_manager.h>
#include <string.h>
#include <wifi_network.h>

namespace calendar {

  bool WifiManager::hasSavedCredentials() const { return WifiCaptivePortal.isSaved(); }

  bool WifiManager::connectWithSavedCredentials() {
    if (!hasSavedCredentials()) {
      return false;
    }

    WifiCaptivePortal.setHostname(getWifiClientHostname());
    WifiCaptivePortal.setConnectionTimeout(kWifiConnectionTimeoutMs);
    return ::connectWithSavedCredentials();
  }

  bool WifiManager::openProvisioningPortal() {
    WifiCaptivePortal.setHostname(getWifiClientHostname());
    WifiCaptivePortal.setConnectionTimeout(kWifiConnectionTimeoutMs);
    WifiCaptivePortal.setPortalTimeoutCallback([]() {});
    return WifiCaptivePortal.startPortal() && isConnected();
  }

  void WifiManager::clearSavedCredentials() { WifiCaptivePortal.resetSettings(); }

  bool WifiManager::isConnected() const { return WiFi.status() == WL_CONNECTED; }

  void WifiManager::disconnect() {
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.disconnect();
    }
    WiFi.mode(WIFI_OFF);
  }

  void WifiManager::getProvisioningSsid(char *output, size_t outputSize) const {
    if (output == nullptr || outputSize == 0) {
      return;
    }
    const String ssid = WifiCaptivePortal.getAPSSID();
    strlcpy(output, ssid.c_str(), outputSize);
  }

  bool WifiManager::getCalendarFeedUrl(char *output, size_t outputSize) const {
    if (output == nullptr || outputSize == 0) {
      return false;
    }
    output[0] = '\0';
    Preferences prefs;
    if (!prefs.begin("data", true)) {
      return false;
    }
    const String feedUrl = prefs.getString("api_url", "");
    prefs.end();
    if (feedUrl.isEmpty() || feedUrl.length() >= outputSize) {
      return false;
    }
    strlcpy(output, feedUrl.c_str(), outputSize);
    return true;
  }

} // namespace calendar
