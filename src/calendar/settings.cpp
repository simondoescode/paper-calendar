#include <calendar/settings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace calendar {
  namespace {

    bool equals(const char *left, const char *right) {
      return left != nullptr && right != nullptr && strcmp(left, right) == 0;
    }

    bool endsWithIcs(const char *path, size_t length) {
      if (length < 4) return false;
      const char suffix[] = ".ics";
      for (size_t i = 0; i < 4; ++i) {
        char value = path[length - 4 + i];
        if (value >= 'A' && value <= 'Z') value = static_cast<char>(value - 'A' + 'a');
        if (value != suffix[i]) return false;
      }
      return true;
    }

    void setError(char *error, size_t errorSize, const char *message) {
      if (error != nullptr && errorSize > 0) {
        snprintf(error, errorSize, "%s", message);
      }
    }

    bool decodeFormValue(const char *source, char *destination, size_t capacity) {
      size_t output = 0;
      for (size_t input = 0; source[input] != '\0'; ++input) {
        unsigned char value = static_cast<unsigned char>(source[input]);
        if (value == '+') {
          value = ' ';
        } else if (value == '%') {
          const char high = source[input + 1];
          const char low = source[input + 2];
          if (high == '\0' || low == '\0') {
            return false;
          }
          char hex[3] = {high, low, '\0'};
          char *end = nullptr;
          const long decoded = strtol(hex, &end, 16);
          if (end == hex || *end != '\0' || decoded == 0) {
            return false;
          }
          value = static_cast<unsigned char>(decoded);
          input += 2;
        }
        if (output + 1 >= capacity) {
          return false;
        }
        destination[output++] = static_cast<char>(value);
      }
      destination[output] = '\0';
      return true;
    }

    bool isValidFeedUrl(const char *url) {
      if (url == nullptr) {
        return false;
      }
      if (equals(url, kDefaultFixtureUrl)) {
        return true;
      }
      if (strncmp(url, "https://", 8) == 0) {
        const char *path = strchr(url + 8, '/');
        if (path == nullptr) {
          return false;
        }
        const char *end = strpbrk(path, "?#");
        const size_t pathLength = end == nullptr ? strlen(path) : static_cast<size_t>(end - path);
        return endsWithIcs(path, pathLength);
      }
      if (strncmp(url, "file://", 7) == 0) {
        return url[7] != '\0';
      }
      return false;
    }

    bool copyFormField(const char *body, const char *name, char *output, size_t outputSize, bool required) {
      const size_t nameLength = strlen(name);
      const char *cursor = body;
      while (cursor != nullptr && *cursor != '\0') {
        const char *end = strchr(cursor, '&');
        const size_t pairLength = end == nullptr ? strlen(cursor) : static_cast<size_t>(end - cursor);
        const char *equals = static_cast<const char *>(memchr(cursor, '=', pairLength));
        if (equals != nullptr && static_cast<size_t>(equals - cursor) == nameLength &&
            memcmp(cursor, name, nameLength) == 0) {
          char encoded[1024];
          size_t valueLength = pairLength - nameLength - 1;
          if (valueLength >= sizeof(encoded)) {
            return false;
          }
          memcpy(encoded, equals + 1, valueLength);
          encoded[valueLength] = '\0';
          return decodeFormValue(encoded, output, outputSize);
        }
        cursor = end == nullptr ? nullptr : end + 1;
      }
      if (required) {
        return false;
      }
      output[0] = '\0';
      return true;
    }

  } // namespace

  CalendarSettings defaultCalendarSettings() {
    CalendarSettings settings = {};
    settings.refreshIntervalSeconds = kDefaultRefreshIntervalSeconds;
    snprintf(settings.timezone, sizeof(settings.timezone), "%s", kDefaultCalendarTimezone);
    return settings;
  }

  bool validateCalendarSettings(const CalendarSettings &settings, char *error, size_t errorSize) {
    if (settings.calendarUrl[0] != '\0' && !isValidFeedUrl(settings.calendarUrl)) {
      setError(error, errorSize, "Use fixture://default, a file:// URL, or an HTTPS .ics URL.");
      return false;
    }
    if (settings.refreshIntervalSeconds < kMinimumRefreshIntervalSeconds ||
        settings.refreshIntervalSeconds > kMaximumRefreshIntervalSeconds) {
      setError(error, errorSize, "Refresh interval must be between 60 and 604800 seconds.");
      return false;
    }
    if (!equals(settings.timezone, kDefaultCalendarTimezone) && !equals(settings.timezone, "UTC")) {
      setError(error, errorSize, "Supported timezones are Europe/London and UTC.");
      return false;
    }
    if (error != nullptr && errorSize > 0) {
      error[0] = '\0';
    }
    return true;
  }

  bool applySettingsForm(const char *body, CalendarSettings &settings, char *error, size_t errorSize) {
    if (body == nullptr) {
      setError(error, errorSize, "Missing form body.");
      return false;
    }
    CalendarSettings submitted = {};
    char interval[24];
    if (!copyFormField(body, "calendar_url", submitted.calendarUrl, sizeof(submitted.calendarUrl), true) ||
        !copyFormField(body, "refresh_interval", interval, sizeof(interval), true) ||
        !copyFormField(body, "timezone", submitted.timezone, sizeof(submitted.timezone), true)) {
      setError(error, errorSize, "Missing or oversized setting.");
      return false;
    }
    char *end = nullptr;
    const unsigned long parsedInterval = strtoul(interval, &end, 10);
    if (end == interval || *end != '\0' || parsedInterval > UINT32_MAX) {
      setError(error, errorSize, "Refresh interval must be an integer number of seconds.");
      return false;
    }
    submitted.refreshIntervalSeconds = static_cast<uint32_t>(parsedInterval);
    if (!validateCalendarSettings(submitted, error, errorSize)) {
      return false;
    }
    settings = submitted;
    return true;
  }

} // namespace calendar
