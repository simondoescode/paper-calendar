#pragma once

#include <WiFiType.h>

#include "wifi-helpers.h"
#include "wifi-types.h"

WifiConnectionResult initiateConnectionAndWaitForOutcome(const WifiCredentials credentials, uint32_t timeoutMs);
wl_status_t waitForConnectResult(uint32_t timeout, const WifiEventData &eventData);
void disableWpa2Enterprise();
