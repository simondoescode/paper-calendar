#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <calendar/calendar_config.h>
#include <calendar/icalendar_feed_provider.h>

#include <string.h>
#include <strings.h>

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_data_cert_x509_crt_bundle_bin_start");

namespace calendar {
namespace {

bool isHttpsIcalendarUrl(const char *url) {
  if (url == nullptr || strncmp(url, "https://", 8) != 0) {
    return false;
  }
  const char *path = strchr(url + 8, '/');
  if (path == nullptr) {
    return false;
  }
  const char *query = strpbrk(path, "?#");
  const size_t pathLength = query == nullptr ? strlen(path) : static_cast<size_t>(query - path);
  if (pathLength < 4) {
    return false;
  }
  const char *suffix = path + pathLength - 4;
  return strcasecmp(suffix, ".ics") == 0;
}

} // namespace

IcalendarFeedProvider::IcalendarFeedProvider(const char *url, CalendarRange range)
    : _url{}, _range(range), _error(IcalendarError::None) {
  if (url != nullptr) {
    strlcpy(_url, url, sizeof(_url));
  }
}

size_t IcalendarFeedProvider::loadEvents(CalendarEvent *events, size_t capacity) const {
  _error = IcalendarError::None;
  if (events == nullptr || capacity == 0 || _url[0] == '\0') {
    _error = IcalendarError::InvalidInput;
    return 0;
  }
  if (!isHttpsIcalendarUrl(_url)) {
    _error = IcalendarError::InvalidInput;
    return 0;
  }

  IcalendarParser parser(_range, events, capacity);
  WiFiClientSecure tlsClient;
  tlsClient.setCACertBundle(rootca_crt_bundle_start);
  HTTPClient http;
  http.setTimeout(kCalendarFeedTimeoutMs);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(tlsClient, _url)) {
    _error = IcalendarError::Transport;
    return 0;
  }

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    http.end();
    _error = status > 0 ? IcalendarError::HttpStatus : IcalendarError::Transport;
    return 0;
  }
  const int contentLength = http.getSize();
  if (contentLength > static_cast<int>(kMaximumCalendarFeedBytes)) {
    http.end();
    _error = IcalendarError::FeedTooLarge;
    return 0;
  }

  Stream *stream = http.getStreamPtr();
  uint8_t chunk[512];
  size_t received = 0;
  const uint32_t startedAt = millis();
  while (http.connected() || stream->available() > 0) {
    const int available = stream->available();
    if (available <= 0) {
      if (millis() - startedAt >= kCalendarFeedTimeoutMs) {
        _error = IcalendarError::Transport;
        break;
      }
      delay(1);
      continue;
    }
    const size_t chunkSize = static_cast<size_t>(available) < sizeof(chunk) ?
                             static_cast<size_t>(available) : sizeof(chunk);
    const int bytesRead = stream->readBytes(chunk, chunkSize);
    if (bytesRead <= 0) {
      _error = IcalendarError::Transport;
      break;
    }
    received += static_cast<size_t>(bytesRead);
    if (received > kMaximumCalendarFeedBytes) {
      _error = IcalendarError::FeedTooLarge;
      break;
    }
    if (!parser.write(chunk, static_cast<size_t>(bytesRead))) {
      _error = parser.error();
      break;
    }
  }
  http.end();

  if (_error != IcalendarError::None) {
    return 0;
  }
  if (!parser.finish()) {
    _error = parser.error();
    return 0;
  }
  return parser.eventCount();
}

IcalendarError IcalendarFeedProvider::error() const { return _error; }

const char *icalendarErrorMessage(IcalendarError error) {
  switch (error) {
  case IcalendarError::None:
    return "No error";
  case IcalendarError::InvalidInput:
    return "Invalid or missing HTTPS iCalendar URL";
  case IcalendarError::Transport:
    return "iCalendar feed connection failed";
  case IcalendarError::HttpStatus:
    return "iCalendar feed returned an HTTP error";
  case IcalendarError::FeedTooLarge:
    return "iCalendar feed exceeded the configured size limit";
  case IcalendarError::MalformedFeed:
    return "iCalendar feed is malformed or contains unsupported dates";
  case IcalendarError::UnsupportedTimezone:
    return "iCalendar feed uses a timezone other than Europe/London or UTC";
  case IcalendarError::UnsupportedRecurrence:
    return "iCalendar feed contains recurrence rules not safely supported";
  case IcalendarError::TooManyEvents:
    return "iCalendar feed contains more visible events than the device limit";
  }
  return "Unknown iCalendar feed error";
}

} // namespace calendar
