#pragma once

#include <stddef.h>

namespace calendar {

class WifiManager {
public:
  bool hasSavedCredentials() const;
  bool connectWithSavedCredentials();
  bool openProvisioningPortal();
  void clearSavedCredentials();
  bool isConnected() const;
  void disconnect();
  void getProvisioningSsid(char *output, size_t outputSize) const;
  bool getCalendarFeedUrl(char *output, size_t outputSize) const;
};

} // namespace calendar
