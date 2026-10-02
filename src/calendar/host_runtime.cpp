#include <calendar/font_text_bounds.h>
#include <calendar/host_runtime.h>

#if defined(CALENDAR_HOST)

#include <algorithm>
#include <array>
#include <calendar/calendar_config.h>
#include <calendar/calendar_renderer.h>
#include <calendar/icalendar_parser.h>
#include <calendar/mock_calendar_provider.h>
#include <chrono>
#include <errno.h>
#include <fonts/Manrope_Bold_13.h>
#include <fonts/Manrope_Bold_25.h>
#include <fonts/Manrope_Medium_10.h>
#include <fonts/Manrope_Medium_8.h>
#include <fonts/Manrope_SemiBold_11.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host_font_decoder/Group5.h"
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#include <winsock2.h>
#include <ws2tcpip.h>
using HostSocket = SOCKET;
static const HostSocket kInvalidSocket = INVALID_SOCKET;
static void closeHostSocket(HostSocket socket) { closesocket(socket); }
#else
#include <arpa/inet.h>
#include <curl/curl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using HostSocket = int;
static const HostSocket kInvalidSocket = -1;
static void closeHostSocket(HostSocket socket) { close(socket); }
#endif

namespace calendar {
  namespace {

    constexpr uint16_t kWidth = 800;
    constexpr uint16_t kHeight = 480;
    constexpr size_t kRowBytes = (kWidth + 7) / 8;

    uint32_t crc32(const uint8_t *data, size_t length) {
      uint32_t crc = 0xffffffffu;
      for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
          crc = (crc >> 1) ^ (0xedb88320u & static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1))));
        }
      }
      return ~crc;
    }

    void appendBigEndian(std::vector<uint8_t> &output, uint32_t value) {
      output.push_back(static_cast<uint8_t>(value >> 24));
      output.push_back(static_cast<uint8_t>(value >> 16));
      output.push_back(static_cast<uint8_t>(value >> 8));
      output.push_back(static_cast<uint8_t>(value));
    }

    void appendChunk(std::vector<uint8_t> &png, const char type[4], const std::vector<uint8_t> &payload) {
      appendBigEndian(png, static_cast<uint32_t>(payload.size()));
      const size_t crcStart = png.size();
      png.insert(png.end(), type, type + 4);
      png.insert(png.end(), payload.begin(), payload.end());
      appendBigEndian(png, crc32(png.data() + crcStart, png.size() - crcStart));
    }

    uint32_t adler32(const std::vector<uint8_t> &data) {
      uint32_t first = 1;
      uint32_t second = 0;
      for (size_t i = 0; i < data.size(); ++i) {
        first = (first + data[i]) % 65521;
        second = (second + first) % 65521;
      }
      return (second << 16) | first;
    }

    std::vector<uint8_t> encodePng(const std::vector<uint8_t> &pixels) {
      std::vector<uint8_t> scanlines;
      scanlines.reserve(static_cast<size_t>(kHeight) * (kRowBytes + 1));
      for (uint16_t y = 0; y < kHeight; ++y) {
        scanlines.push_back(0);
        for (uint16_t x = 0; x < kWidth; x += 8) {
          uint8_t packed = 0;
          for (uint8_t bit = 0; bit < 8; ++bit) {
            if (pixels[static_cast<size_t>(y) * kWidth + x + bit] != 0) {
              packed |= static_cast<uint8_t>(0x80u >> bit);
            }
          }
          scanlines.push_back(packed);
        }
      }
      std::vector<uint8_t> compressed;
      compressed.push_back(0x78);
      compressed.push_back(0x01);
      size_t offset = 0;
      while (offset < scanlines.size()) {
        const size_t blockLength = std::min<size_t>(65535, scanlines.size() - offset);
        compressed.push_back(offset + blockLength == scanlines.size() ? 1 : 0);
        const uint16_t length = static_cast<uint16_t>(blockLength);
        const uint16_t inverse = static_cast<uint16_t>(~length);
        compressed.push_back(static_cast<uint8_t>(length));
        compressed.push_back(static_cast<uint8_t>(length >> 8));
        compressed.push_back(static_cast<uint8_t>(inverse));
        compressed.push_back(static_cast<uint8_t>(inverse >> 8));
        compressed.insert(compressed.end(), scanlines.begin() + offset, scanlines.begin() + offset + blockLength);
        offset += blockLength;
      }
      appendBigEndian(compressed, adler32(scanlines));

      std::vector<uint8_t> png = {137, 80, 78, 71, 13, 10, 26, 10};
      std::vector<uint8_t> header;
      appendBigEndian(header, kWidth);
      appendBigEndian(header, kHeight);
      header.push_back(1);
      header.push_back(0);
      header.push_back(0);
      header.push_back(0);
      header.push_back(0);
      appendChunk(png, "IHDR", header);
      appendChunk(png, "IDAT", compressed);
      appendChunk(png, "IEND", std::vector<uint8_t>());
      return png;
    }

    CalendarDate hostLocalDate() {
      const time_t now = time(nullptr);
      struct tm local = {};
#if defined(_WIN32)
      localtime_s(&local, &now);
#else
      localtime_r(&now, &local);
#endif
      return {local.tm_year + 1900, static_cast<unsigned>(local.tm_mon + 1), static_cast<unsigned>(local.tm_mday)};
    }

    bool ensureParentDirectory(const std::string &path) {
      const size_t slash = path.find_last_of("/\\");
      if (slash == std::string::npos) return true;
      const std::string directory = path.substr(0, slash);
#if defined(_WIN32)
      return _mkdir(directory.c_str()) == 0 || errno == EEXIST;
#else
      return mkdir(directory.c_str(), 0755) == 0 || errno == EEXIST;
#endif
    }

    std::string escapeHtml(const char *value) {
      std::string output;
      for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        switch (*cursor) {
        case '&':
          output += "&amp;";
          break;
        case '<':
          output += "&lt;";
          break;
        case '>':
          output += "&gt;";
          break;
        case '"':
          output += "&quot;";
          break;
        case '\'':
          output += "&#39;";
          break;
        default:
          output += *cursor;
          break;
        }
      }
      return output;
    }

    std::string decodeJsonString(const char *body, const char *key) {
      const std::string token = std::string("\"") + key + "\"";
      const char *position = strstr(body, token.c_str());
      if (position == nullptr) return std::string();
      position = strchr(position + token.size(), ':');
      if (position == nullptr) return std::string();
      while (*++position == ' ' || *position == '\t') {
      }
      if (*position++ != '"') return std::string();
      std::string output;
      while (*position != '\0' && *position != '"') {
        if (*position == '\\' && position[1] != '\0') {
          ++position;
          if (*position == 'n')
            output += '\n';
          else if (*position == 'r')
            output += '\r';
          else
            output += *position;
          ++position;
        } else {
          output += *position++;
        }
      }
      return output;
    }

    bool decodeFileUrlPath(const char *url, std::string &path) {
      path.clear();
      for (const char *cursor = url + 7; *cursor != '\0'; ++cursor) {
        if (*cursor != '%') {
          path += *cursor;
          continue;
        }
        if (cursor[1] == '\0' || cursor[2] == '\0') return false;
        char encoded[3] = {cursor[1], cursor[2], '\0'};
        char *end = nullptr;
        const long value = strtol(encoded, &end, 16);
        if (end == encoded || *end != '\0' || value == 0) return false;
        path += static_cast<char>(value);
        cursor += 2;
      }
#if defined(_WIN32)
      if (path.size() >= 3 && path[0] == '/' && path[2] == ':') path.erase(0, 1);
#endif
      return !path.empty();
    }

    struct FeedSink {
      explicit FeedSink(std::string &value) : data(value) {}
      std::string &data;
      bool write(const uint8_t *data, size_t length) {
        if (length > 2 * 1024 * 1024 - this->data.size()) return false;
        this->data.append(reinterpret_cast<const char *>(data), length);
        return true;
      }
    };

// Host-only transport. Never include the private URL in diagnostics.
    bool fetchHostFeed(const char *url, std::string &data, char *error, size_t errorSize) {
      FeedSink sink{data};
      snprintf(error, errorSize, "HTTPS connection failed.");
#if defined(_WIN32)
      struct Handle {
        HINTERNET value;
        ~Handle() {
          if (value) WinHttpCloseHandle(value);
        }
      };
      const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url, -1, nullptr, 0);
      if (length == 0) return false;
      std::vector<wchar_t> wide(length);
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url, -1, wide.data(), length);
      URL_COMPONENTS parts = {};
      parts.dwStructSize = sizeof(parts);
      parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
      if (!WinHttpCrackUrl(wide.data(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return false;
      const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
      std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
      path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
      Handle session{WinHttpOpen(L"CalendarHost/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0)};
      if (!session.value || !WinHttpSetTimeouts(session.value, kCalendarFeedTimeoutMs, kCalendarFeedTimeoutMs,
                                                kCalendarFeedTimeoutMs, kCalendarFeedTimeoutMs))
        return false;
      Handle connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};
      if (!connection.value) return false;
      Handle request{WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
      DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
      if (!request.value ||
          !WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy)) ||
          !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
          !WinHttpReceiveResponse(request.value, nullptr))
        return false;
      DWORD status = 0, statusSize = sizeof(status);
      if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
          status != 200) {
        snprintf(error, errorSize, "Calendar server returned HTTP %lu.", static_cast<unsigned long>(status));
        return false;
      }
      const auto started = std::chrono::steady_clock::now();
      uint8_t chunk[512];
      for (;;) {
        DWORD count = 0;
        if (std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(kCalendarFeedTimeoutMs) ||
            !WinHttpReadData(request.value, chunk, sizeof(chunk), &count))
          return false;
        if (count == 0) return true;
        if (!sink.write(chunk, count)) {
          snprintf(error, errorSize, "Calendar feed exceeds host 2 MiB limit.");
          return false;
        }
      }
#else
      static const bool initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
      if (!initialized) return false;
      CURL *request = curl_easy_init();
      if (!request) return false;
      curl_easy_setopt(request, CURLOPT_URL, url);
      curl_easy_setopt(request, CURLOPT_FOLLOWLOCATION, 0L);
      curl_easy_setopt(request, CURLOPT_NOSIGNAL, 1L);
      curl_easy_setopt(request, CURLOPT_TIMEOUT_MS, static_cast<long>(kCalendarFeedTimeoutMs));
      curl_easy_setopt(
        request, CURLOPT_WRITEFUNCTION, +[](char *data, size_t size, size_t count, void *context) -> size_t {
          const size_t length = size * count;
          return static_cast<FeedSink *>(context)->write(reinterpret_cast<uint8_t *>(data), length) ? length : 0;
        });
      curl_easy_setopt(request, CURLOPT_WRITEDATA, &sink);
      const CURLcode result = curl_easy_perform(request);
      long status = 0;
      curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &status);
      curl_easy_cleanup(request);
      if (result != CURLE_OK)
        snprintf(error, errorSize, "HTTPS fetch failed (transport code %u).", static_cast<unsigned>(result));
      else if (status != 200)
        snprintf(error, errorSize, "Calendar server returned HTTP %ld.", status);
      return result == CURLE_OK && status == 200;
#endif
    }

    bool normalizeHostFeed(const std::string &data, CalendarRange range, IcalendarParser &parser) {
  // Private temporary feed is confined to ignored host state and removed on exit.
      const std::string path =
        ".dev/calendar-feed-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ics";
      struct Cleanup {
        std::string path;
        ~Cleanup() { remove(path.c_str()); }
      } cleanup{path};
      if (!ensureParentDirectory(path)) return false;
      std::ofstream file(path, std::ios::binary);
      file.write(data.data(), data.size());
      file.close();
      if (!file) return false;
      char dates[48];
      snprintf(dates, sizeof(dates), " %lld %lld", static_cast<long long>(range.startEpoch),
               static_cast<long long>(range.endEpoch));
#if defined(_WIN32)
      const std::string command =
        ".dev\\calendar-tools\\Scripts\\python.exe tools/expand_host_calendar.py " + path + dates;
      FILE *pipe = _popen(command.c_str(), "rb");
#else
      const std::string command = ".dev/calendar-tools/bin/python tools/expand_host_calendar.py " + path + dates;
      FILE *pipe = popen(command.c_str(), "r");
#endif
      if (!pipe) return false;
      uint8_t chunk[512];
      bool valid = true;
      size_t count;
      while ((count = fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
        if (valid && !parser.write(chunk, count)) valid = false;
      }
      valid = valid && !ferror(pipe);
#if defined(_WIN32)
      const int status = _pclose(pipe);
#else
      const int status = pclose(pipe);
#endif
      return valid && status == 0;
    }

  } // namespace

  HostCalendarProvider::HostCalendarProvider(const CalendarSettings &settings, CalendarDate today)
      : _settings(settings), _today(today), _error{} {}

  size_t HostCalendarProvider::loadEvents(CalendarEvent *events, size_t capacity) const {
    _error[0] = '\0';
    if (events == nullptr || capacity == 0) {
      snprintf(_error, sizeof(_error), "Event buffer unavailable.");
      return 0;
    }
    if (_settings.calendarUrl[0] == '\0') {
      snprintf(_error, sizeof(_error), "Calendar URL is not configured.");
      return 0;
    }
    if (strcmp(_settings.calendarUrl, kDefaultFixtureUrl) == 0) {
      MockCalendarProvider fixture(_today);
      return fixture.loadEvents(events, capacity);
    }
    if (strncmp(_settings.calendarUrl, "https://", 8) == 0) {
      CalendarRange range = {
          calendarTodayRange(_today).startEpoch, calendarRestOfWeekRange(_today).endEpoch, _today,
          calendarRestOfWeekRange(_today).endDate};
      IcalendarParser parser(range, events, capacity);
      std::string data;
      if (!fetchHostFeed(_settings.calendarUrl, data, _error, sizeof(_error))) {
        return 0;
      }
      _error[0] = '\0';
      if (!normalizeHostFeed(data, range, parser)) {
        snprintf(_error, sizeof(_error),
                 "Feed expansion failed: check host Python setup, feed validity, or 16-event week limit.");
        return 0;
      }
      if (!parser.finish()) {
        snprintf(_error, sizeof(_error), "iCalendar feed parser error %u.", static_cast<unsigned>(parser.error()));
        return 0;
      }
      return parser.eventCount();
    }
    if (strncmp(_settings.calendarUrl, "file://", 7) != 0) {
      snprintf(_error, sizeof(_error), "Use an HTTPS .ics feed, fixture://default, or a local file:// feed.");
      return 0;
    }
    std::string path;
    if (!decodeFileUrlPath(_settings.calendarUrl, path)) {
      snprintf(_error, sizeof(_error), "Local calendar file URL is invalid.");
      return 0;
    }
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
      snprintf(_error, sizeof(_error), "Could not open local calendar feed.");
      return 0;
    }
    CalendarRange range = {
        calendarTodayRange(_today).startEpoch, calendarRestOfWeekRange(_today).endEpoch, _today,
        calendarRestOfWeekRange(_today).endDate};
    IcalendarParser parser(range, events, capacity);
    std::array<uint8_t, 512> chunk;
    size_t total = 0;
    while (file) {
      file.read(reinterpret_cast<char *>(chunk.data()), chunk.size());
      const std::streamsize count = file.gcount();
      if (count <= 0) break;
      total += static_cast<size_t>(count);
      if (total > kMaximumCalendarFeedBytes || !parser.write(chunk.data(), static_cast<size_t>(count))) {
        snprintf(_error, sizeof(_error), "Local iCalendar feed exceeded limits or is malformed.");
        return 0;
      }
    }
    if (!parser.finish()) {
      snprintf(_error, sizeof(_error), "Local iCalendar feed parser error %u.", static_cast<unsigned>(parser.error()));
      return 0;
    }
    return parser.eventCount();
  }

  const char *HostCalendarProvider::error() const { return _error; }

  struct HostDisplayTarget::Impl {
    explicit Impl(const char *filePath) : path(filePath == nullptr ? "" : filePath), pixels(kWidth * kHeight, 1) {}
    std::string path;
    std::vector<uint8_t> pixels;
  };

  HostDisplayTarget::HostDisplayTarget(const char *path) : _impl(new Impl(path)) {}
  HostDisplayTarget::~HostDisplayTarget() { delete _impl; }

  bool HostDisplayTarget::begin() {
    if (_impl == nullptr) return false;
    std::fill(_impl->pixels.begin(), _impl->pixels.end(), 1);
    return true;
  }

  namespace {
    void setHostPixel(std::vector<uint8_t> &pixels, int x, int y, DisplayColor color) {
      if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
      const bool black =
        color == DisplayColor::Black || (color == CalendarPatterns::Sidebar && CalendarPatterns::sidebarInk(x, y));
      pixels[static_cast<size_t>(y) * kWidth + x] = black ? 0 : 1;
    }

    const uint8_t *hostCalendarFont(uint8_t size) {
      switch (size) {
      case kCalendarFontMain:
        return Manrope_Bold_25;
      case kCalendarFontHeading:
        return Manrope_Bold_13;
      case kCalendarFontTitle:
        return Manrope_SemiBold_11;
      case kCalendarFontMetadata:
        return Manrope_Medium_10;
      case kCalendarFontFooter:
        return Manrope_Medium_8;
      case kCalendarFontStatus:
        return Manrope_Medium_8;
      default:
        return Manrope_Medium_10;
      }
    }

    uint16_t fontWord(const uint8_t *value) {
      return static_cast<uint16_t>(value[0] | (static_cast<uint16_t>(value[1]) << 8));
    }

    uint8_t glyphAdvance(const uint8_t *font, unsigned char character) {
      const uint16_t first = fontWord(font + 2);
      const uint16_t last = fontWord(font + 4);
      if (character < first || character > last) return 0;
      return font[12 + static_cast<size_t>(character - first) * 8 + 3];
    }
  } // namespace

  void HostDisplayTarget::text(uint16_t x, uint16_t y, const char *value, uint8_t size, DisplayColor foreground,
                               DisplayColor background) {
    if (value == nullptr || _impl == nullptr) return;
    const uint8_t *font = hostCalendarFont(size);
    const uint16_t first = fontWord(font + 2);
    const uint16_t last = fontWord(font + 4);
    const size_t bitmapStart = 12 + (last - first + 1) * 8;
    uint16_t cursorX = x;
    for (const unsigned char *character = reinterpret_cast<const unsigned char *>(value);
         *character != '\0' && cursorX < kWidth; ++character) {
      if (*character < first || *character > last) continue;
      const uint8_t *metrics = font + 12 + (*character - first) * 8;
      const uint8_t width = metrics[2];
      const uint8_t height = metrics[4];
      const int left = static_cast<int>(cursorX) + static_cast<int8_t>(metrics[5]);
      const int top = static_cast<int>(y) + static_cast<int8_t>(metrics[6]);
      if (width > 0 && height > 0) {
        const uint16_t offset = fontWord(metrics);
      // The decoder reads ahead by a word. Pad the bounded glyph stream so
      // the last glyph never reads beyond the font array.
        const size_t fontBytes = size == kCalendarFontMain      ? sizeof(Manrope_Bold_25)
                                 : size == kCalendarFontHeading ? sizeof(Manrope_Bold_13)
                                 : size == kCalendarFontTitle   ? sizeof(Manrope_SemiBold_11)
                                 : size == kCalendarFontFooter  ? sizeof(Manrope_Medium_8)
                                 : size == kCalendarFontStatus  ? sizeof(Manrope_Medium_8)
                                                                : sizeof(Manrope_Medium_10);
        const size_t end = *character < last ? bitmapStart + fontWord(metrics + 8) : fontBytes;
        const size_t start = bitmapStart + offset;
        if (end <= start || end > fontBytes) continue;
        std::vector<uint8_t> compressed(font + start, font + end);
        compressed.resize(compressed.size() + 4, 0);
        G5DECODER decoder;
        std::array<uint8_t, 32> row = {};
        if (decoder.init(width, height, compressed.data(), static_cast<int>(compressed.size())) != G5_SUCCESS) continue;
        for (uint16_t rowY = 0; rowY < height; ++rowY) {
          const int result = decoder.decodeLine(row.data());
          if (result != G5_SUCCESS && result != G5_DECODE_COMPLETE) break;
          for (uint16_t column = 0; column < width; ++column) {
            const bool ink = (row[column / 8] & (0x80u >> (column & 7))) != 0;
            setHostPixel(_impl->pixels, left + column, top + rowY, ink ? foreground : background);
          }
        }
      }
      cursorX = static_cast<uint16_t>(cursorX + glyphAdvance(font, *character));
    }
  }

  uint16_t HostDisplayTarget::textWidth(const char *value, uint8_t size) {
    if (value == nullptr) return 0;
    const uint8_t *font = hostCalendarFont(size);
    uint16_t width = 0;
    for (const unsigned char *character = reinterpret_cast<const unsigned char *>(value); *character != '\0';
         ++character) {
      width = static_cast<uint16_t>(width + glyphAdvance(font, *character));
    }
    return width;
  }

  TextVerticalBounds HostDisplayTarget::textVerticalBounds(const char *value, uint8_t size) {
    return smallFontTextVerticalBounds(hostCalendarFont(size), value, [](const uint8_t *p) { return *p; });
  }

  uint8_t HostDisplayTarget::fontHeight(uint8_t size) {
    const uint8_t *font = hostCalendarFont(size);
    const size_t index = 'H' - fontWord(font + 2);
    return static_cast<uint8_t>(-static_cast<int8_t>(font[12 + index * 8 + 6]));
  }

  void HostDisplayTarget::bitmap(uint16_t x, uint16_t y, const uint8_t *data, uint16_t width, uint16_t height,
                                 DisplayColor color) {
    if (_impl == nullptr || data == nullptr) return;
    const size_t pitch = (width + 7) / 8;
    for (uint16_t row = 0; row < height; ++row) {
      for (uint16_t column = 0; column < width; ++column) {
        if ((data[row * pitch + column / 8] & (0x80u >> (column & 7))) != 0) {
          setHostPixel(_impl->pixels, static_cast<int>(x) + column, static_cast<int>(y) + row, color);
        }
      }
    }
  }

  void HostDisplayTarget::line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, DisplayColor color) {
    if (_impl == nullptr) return;
    int x = x1;
    int y = y1;
    const int dx = abs(static_cast<int>(x2) - x);
    const int sx = x < x2 ? 1 : -1;
    const int dy = -abs(static_cast<int>(y2) - y);
    const int sy = y < y2 ? 1 : -1;
    int error = dx + dy;
    while (true) {
      if (x >= 0 && x < kWidth && y >= 0 && y < kHeight) {
        setHostPixel(_impl->pixels, x, y, color);
      }
      if (x == x2 && y == y2) break;
      const int twiceError = 2 * error;
      if (twiceError >= dy) {
        error += dy;
        x += sx;
      }
      if (twiceError <= dx) {
        error += dx;
        y += sy;
      }
    }
  }

  void HostDisplayTarget::fillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, DisplayColor color) {
    if (_impl == nullptr) return;
    for (int py = y; py < static_cast<int>(y + height); ++py) {
      for (int px = x; px < static_cast<int>(x + width); ++px)
        setHostPixel(_impl->pixels, px, py, color);
    }
  }

  void HostDisplayTarget::roundRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t radius,
                                    DisplayColor fill, DisplayColor border) {
    if (_impl == nullptr || width == 0 || height == 0) return;
    for (int py = y; py < static_cast<int>(y + height); ++py) {
      for (int px = x; px < static_cast<int>(x + width); ++px) {
        const int nearestX = px < x + radius ? x + radius : (px >= x + width - radius ? x + width - radius - 1 : px);
        const int nearestY = py < y + radius ? y + radius : (py >= y + height - radius ? y + height - radius - 1 : py);
        const int dx = px - nearestX;
        const int dy = py - nearestY;
        if (dx * dx + dy * dy > radius * radius) continue;
        const bool edge = px == x || py == y || px == x + width - 1 || py == y + height - 1 ||
                          dx * dx + dy * dy >= (radius - 1) * (radius - 1);
        setHostPixel(_impl->pixels, px, py, edge ? border : fill);
      }
    }
  }

  void HostDisplayTarget::circle(uint16_t x, uint16_t y, uint16_t radius, DisplayColor color, bool filled) {
    if (_impl == nullptr) return;
    const int r2 = radius * radius;
    const int inner = radius > 1 ? (radius - 2) * (radius - 2) : 0;
    for (int py = static_cast<int>(y) - radius; py <= static_cast<int>(y) + radius; ++py) {
      for (int px = static_cast<int>(x) - radius; px <= static_cast<int>(x) + radius; ++px) {
        const int dx = px - x;
        const int dy = py - y;
        const int distance = dx * dx + dy * dy;
        if (distance <= r2 && (filled || distance >= inner)) setHostPixel(_impl->pixels, px, py, color);
      }
    }
  }

  bool HostDisplayTarget::refresh() {
    if (_impl == nullptr || _impl->path.empty()) return false;
    if (!ensureParentDirectory(_impl->path)) return false;
    const std::vector<uint8_t> png = encodePng(_impl->pixels);
    std::ofstream file(_impl->path.c_str(), std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    return file.good();
  }

  bool HostDisplayTarget::writeStatus(DisplayStatus status, const char *message) {
    return renderStatus(*this, status, message);
  }

  struct HostPortal::Impl {
    Impl(ConfigStore &settingsStore, HostDisplayTarget &screen, const std::function<void()> &runRefresh)
        : store(settingsStore), display(screen), refresh(runRefresh) {}
    ConfigStore &store;
    HostDisplayTarget &display;
    std::function<void()> refresh;

    std::string settingsHtml() {
      CalendarSettings settings = defaultCalendarSettings();
      if (!store.load(settings)) {
        settings = defaultCalendarSettings();
      }
      std::string url = escapeHtml(settings.calendarUrl);
      char interval[24];
      char latitude[48], longitude[48];
      snprintf(latitude, sizeof(latitude), "%.6f", settings.weatherLatitude);
      snprintf(longitude, sizeof(longitude), "%.6f", settings.weatherLongitude);
      snprintf(interval, sizeof(interval), "%lu", static_cast<unsigned long>(settings.refreshIntervalSeconds));
      return std::string(
        "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width'>"
        "<title>Calendar emulator</title><style>body{font:16px system-ui;max-width:760px;margin:2rem auto;padding:0 "
        "1rem}"
        "label{display:block;margin:1rem 0 .3rem}input{width:100%;padding:.65rem;box-sizing:border-box}"
        "button{padding:.7rem;margin:.7rem .5rem .7rem 0}img{max-width:100%;border:1px solid #888}"
        "#status{white-space:pre-wrap}</style></head><body><h1>Calendar host emulator</h1>"
        "<p>Local settings are saved to <code>.dev/calendar-settings.json</code>. Fixture data works offline.</p>"
        "<form id=settings><label>Calendar URL</label><input name=calendar_url type=url value=\"" +
        url +
        "\" required><small>Use a private HTTPS .ics feed URL, fixture://default, or a local file://... .ics "
        "feed.</small>"
        "<label>Refresh interval (seconds)</label><input name=refresh_interval type=number min=60 max=604800 value=\"" +
        interval + "\" required><label>Timezone</label><input name=timezone value=\"" + escapeHtml(settings.timezone) +
        "\" required><h2>Weather location</h2><label>Latitude</label>"
        "<input name=weather_latitude type=number min=-90 max=90 step=any value=\"" +
        std::string(latitude) + "\" required><label>Longitude</label>"
        "<input name=weather_longitude type=number min=-180 max=180 step=any value=\"" +
        std::string(longitude) +
        "\" required><p>Enter decimal coordinates. The host preview uses offline demo weather; "
        "these settings do not change its forecast or the physical device.</p>"
        "<button>Save settings</button></form><button id=refresh>Refresh now</button>"
        "<button id=reload>Reload settings and render</button><p id=status></p><h2>800 × 480 preview</h2>"
        "<img id=preview src='/preview.png' alt='Calendar display preview'>"
        "<script>const status=document.querySelector('#status');function updatePreview(){"
        "document.querySelector('#preview').src='/preview.png?'+Date.now()}document.querySelector('#settings')"
        ".addEventListener('submit',async e=>{e.preventDefault();const body=new URLSearchParams(new "
        "FormData(e.target));"
        "const r=await "
        "fetch('/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});"
        "status.textContent=await r.text();if(r.ok)updatePreview()});"
        "async function action(path){const r=await fetch(path,{method:'POST'});status.textContent=await r.text();"
        "if(r.ok)updatePreview()}document.querySelector('#refresh').onclick=()=>action('/refresh');"
        "document.querySelector('#reload').onclick=()=>action('/reload');</script></body></html>");
    }

    static void sendResponse(HostSocket client, int status, const char *contentType, const std::string &body) {
      const char *statusText =
        status == 200 ? "OK"
                      : (status == 400 ? "Bad Request" : (status == 404 ? "Not Found" : "Internal Server Error"));
      std::string header = std::string("HTTP/1.1 ") + std::to_string(status) + " " + statusText +
                           "\r\nContent-Type: " + contentType + "\r\nContent-Length: " + std::to_string(body.size()) +
                           "\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";
      const std::string response = header + body;
      size_t sent = 0;
      while (sent < response.size()) {
#if defined(_WIN32)
        const int count = ::send(client, response.data() + sent, static_cast<int>(response.size() - sent), 0);
#else
        const ssize_t count = ::send(client, response.data() + sent, response.size() - sent, 0);
#endif
        if (count <= 0) return;
        sent += static_cast<size_t>(count);
      }
    }

    void handle(HostSocket client) {
      std::string request;
      request.reserve(4096);
      char buffer[2048];
      size_t headerEnd = std::string::npos;
      size_t contentLength = 0;
      while (request.size() < 16384) {
#if defined(_WIN32)
        const int received = recv(client, buffer, sizeof(buffer), 0);
#else
        const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
#endif
        if (received <= 0) break;
        request.append(buffer, static_cast<size_t>(received));
        headerEnd = request.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
          const size_t lengthPosition = request.find("Content-Length:");
          if (lengthPosition != std::string::npos && lengthPosition < headerEnd) {
            contentLength = static_cast<size_t>(strtoul(request.c_str() + lengthPosition + 15, nullptr, 10));
          }
          if (request.size() >= headerEnd + 4 + contentLength) break;
        }
      }
      if (headerEnd == std::string::npos || request.size() > 16384) {
        sendResponse(client, 400, "text/plain", "Malformed request.");
        return;
      }
      const size_t firstSpace = request.find(' ');
      const size_t secondSpace = firstSpace == std::string::npos ? firstSpace : request.find(' ', firstSpace + 1);
      if (firstSpace == std::string::npos || secondSpace == std::string::npos) {
        sendResponse(client, 400, "text/plain", "Malformed request line.");
        return;
      }
      const std::string method = request.substr(0, firstSpace);
      const std::string path = request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
      const std::string body = request.substr(headerEnd + 4, contentLength);
      if (method == "GET" && (path == "/" || path.find("/?") == 0)) {
        sendResponse(client, 200, "text/html; charset=utf-8", settingsHtml());
      } else if (method == "GET" && path.find("/preview.png") == 0) {
        std::ifstream file(".dev/calendar-preview.png", std::ios::binary);
        const bool exists = file.good();
        const std::string image((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        sendResponse(client, exists ? 200 : 404, "image/png", image);
      } else if (method == "POST" && path == "/settings") {
        CalendarSettings settings = defaultCalendarSettings();
        if (!store.load(settings)) settings = defaultCalendarSettings();
        char error[160];
        if (!applySettingsForm(body.c_str(), settings, error, sizeof(error))) {
          sendResponse(client, 400, "text/plain; charset=utf-8", error);
        } else if (!store.save(settings)) {
          sendResponse(client, 500, "text/plain; charset=utf-8", "Could not persist settings.");
        } else {
          if (refresh) refresh();
          sendResponse(client, 200, "text/plain; charset=utf-8", "Settings saved to .dev/calendar-settings.json.");
        }
      } else if (method == "POST" && (path == "/refresh" || path == "/reload")) {
        if (refresh) refresh();
        sendResponse(client, 200, "text/plain; charset=utf-8",
                     path == "/reload" ? "Settings reloaded and calendar rendered." : "Calendar refreshed.");
      } else {
        sendResponse(client, 404, "text/plain", "Not found.");
      }
    }
  };

  HostPortal::HostPortal(ConfigStore &store, HostDisplayTarget &display, const std::function<void()> &refresh)
      : _impl(new Impl(store, display, refresh)) {}

  HostPortal::~HostPortal() { delete _impl; }

  int HostPortal::run() {
    if (_impl == nullptr) return 1;
#if defined(_WIN32)
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      fprintf(stderr, "Could not initialize Windows sockets.\n");
      return 1;
    }
#endif
    HostSocket server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == kInvalidSocket) {
      fprintf(stderr, "Could not create portal socket.\n");
      return 1;
    }
    int reuseAddress = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuseAddress), sizeof(reuseAddress));
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || listen(server, 8) != 0) {
      fprintf(stderr, "Could not bind portal to http://localhost:8080 (port may already be in use).\n");
      closeHostSocket(server);
      return 1;
    }
    printf("Portal: http://localhost:8080\n");
    for (;;) {
      HostSocket client = accept(server, nullptr, nullptr);
      if (client == kInvalidSocket) continue;
      _impl->handle(client);
      closeHostSocket(client);
    }
  }

} // namespace calendar

#endif
