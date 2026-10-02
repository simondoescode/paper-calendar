#include <calendar/settings.h>

#if defined(CALENDAR_HOST)

#include <errno.h>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace calendar {
  namespace {

    bool extractCoordinate(const std::string &json, const char *key, double &output) {
      const std::string token = std::string("\"") + key + "\":";
      const size_t position = json.find(token);
      if (position == std::string::npos) return true; // Existing settings retain defaults.
      const char *start = json.c_str() + position + token.size();
      char *end = nullptr;
      const double value = strtod(start, &end);
      if (end == start) return false;
      while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
        ++end;
      if (*end != ',' && *end != '}') return false;
      output = value;
      return true;
    }

    bool extractString(const std::string &json, const char *key, char *output, size_t outputSize) {
      const std::string token = std::string("\"") + key + "\":";
      size_t position = json.find(token);
      if (position == std::string::npos) {
        return false;
      }
      position = json.find('"', position + token.size());
      if (position == std::string::npos) {
        return false;
      }
      ++position;
      size_t outputIndex = 0;
      while (position < json.size() && json[position] != '"') {
        char value = json[position++];
        if (value == '\\') {
          if (position >= json.size()) {
            return false;
          }
          value = json[position++];
          if (value == 'n')
            value = '\n';
          else if (value == 'r')
            value = '\r';
          else if (value == 't')
            value = '\t';
        }
        if (outputIndex + 1 >= outputSize) {
          return false;
        }
        output[outputIndex++] = value;
      }
      if (position >= json.size()) {
        return false;
      }
      output[outputIndex] = '\0';
      return true;
    }

    bool extractUnsigned(const std::string &json, const char *key, uint32_t &output) {
      const std::string token = std::string("\"") + key + "\":";
      const size_t position = json.find(token);
      if (position == std::string::npos) {
        return false;
      }
      size_t cursor = position + token.size();
      while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t')) {
        ++cursor;
      }
      if (cursor >= json.size() || json[cursor] < '0' || json[cursor] > '9') {
        return false;
      }
      uint64_t value = 0;
      while (cursor < json.size() && json[cursor] >= '0' && json[cursor] <= '9') {
        value = value * 10 + static_cast<unsigned>(json[cursor++] - '0');
        if (value > UINT32_MAX) {
          return false;
        }
      }
      output = static_cast<uint32_t>(value);
      return true;
    }

    void writeJsonString(std::ofstream &file, const char *value) {
      file.put('"');
      for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        if (*cursor == '"' || *cursor == '\\') {
          file.put('\\');
        }
        if (*cursor == '\n') {
          file << "\\n";
        } else if (*cursor == '\r') {
          file << "\\r";
        } else if (*cursor == '\t') {
          file << "\\t";
        } else {
          file.put(*cursor);
        }
      }
      file.put('"');
    }

    bool makeDirectories(const std::string &path) {
      std::string current;
      for (size_t i = 0; i < path.size(); ++i) {
        current.push_back(path[i]);
        if (path[i] != '/' && path[i] != '\\') {
          continue;
        }
        if (current.size() == 1) {
          continue;
        }
#if defined(_WIN32)
        if (_mkdir(current.c_str()) != 0 && errno != EEXIST) {
          return false;
        }
#else
        if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
          return false;
        }
#endif
      }
      if (!path.empty()) {
#if defined(_WIN32)
        if (_mkdir(path.c_str()) != 0 && errno != EEXIST) {
          return false;
        }
#else
        if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
          return false;
        }
#endif
      }
      return true;
    }

  } // namespace

  class HostConfigStore : public ConfigStore {
  public:
    explicit HostConfigStore(const char *path) : _path(path == nullptr ? "" : path) {}

    bool load(CalendarSettings &settings) override {
      settings = defaultCalendarSettings();
      std::ifstream file(_path.c_str(), std::ios::binary);
      if (!file) {
        snprintf(settings.calendarUrl, sizeof(settings.calendarUrl), "%s", kDefaultFixtureUrl);
        return true;
      }
      file.seekg(0, std::ios::end);
      const std::streamoff size = file.tellg();
      if (size < 0 || size > 4096) {
        return false;
      }
      file.seekg(0, std::ios::beg);
      const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      CalendarSettings loaded = defaultCalendarSettings();
      if (!extractString(json, "calendarUrl", loaded.calendarUrl, sizeof(loaded.calendarUrl)) ||
          !extractUnsigned(json, "refreshIntervalSeconds", loaded.refreshIntervalSeconds) ||
          !extractString(json, "timezone", loaded.timezone, sizeof(loaded.timezone)) ||
          !extractCoordinate(json, "weatherLatitude", loaded.weatherLatitude) ||
          !extractCoordinate(json, "weatherLongitude", loaded.weatherLongitude) ||
          !validateCalendarSettings(loaded, nullptr, 0)) {
        return false;
      }
      settings = loaded;
      return true;
    }

    bool save(const CalendarSettings &settings) override {
      if (!validateCalendarSettings(settings, nullptr, 0)) {
        return false;
      }
      const size_t slash = _path.find_last_of("/\\");
      if (slash != std::string::npos) {
        const std::string directory = _path.substr(0, slash);
        if (!makeDirectories(directory)) {
          return false;
        }
      }
      std::ofstream file(_path.c_str(), std::ios::binary | std::ios::trunc);
      if (!file) {
        return false;
      }
      file << "{\n  \"calendarUrl\": ";
      writeJsonString(file, settings.calendarUrl);
      file << ",\n  \"refreshIntervalSeconds\": " << settings.refreshIntervalSeconds << ",\n  \"timezone\": ";
      writeJsonString(file, settings.timezone);
      file << std::setprecision(17) << ",\n  \"weatherLatitude\": " << settings.weatherLatitude
           << ",\n  \"weatherLongitude\": " << settings.weatherLongitude;
      file << "\n}\n";
      return file.good();
    }

  private:
    std::string _path;
  };

  ConfigStore *createHostConfigStore(const char *path) { return new HostConfigStore(path); }

} // namespace calendar

#endif
