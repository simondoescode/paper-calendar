#include <calendar/host_runtime.h>

#if defined(CALENDAR_HOST)

#include <calendar/calendar_config.h>
#include <calendar/calendar_renderer.h>
#include <calendar/icalendar_parser.h>
#include <calendar/mock_calendar_provider.h>

#include <algorithm>
#include <array>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using HostSocket = SOCKET;
static const HostSocket kInvalidSocket = INVALID_SOCKET;
static void closeHostSocket(HostSocket socket) { closesocket(socket); }
#else
#include <arpa/inet.h>
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

std::array<uint8_t, 5> glyph(char character) {
  if (character >= 'a' && character <= 'z') {
    character = static_cast<char>(character - 'a' + 'A');
  }
  switch (character) {
  case 'A': return {{0x7e, 0x11, 0x11, 0x11, 0x7e}};
  case 'B': return {{0x7f, 0x49, 0x49, 0x49, 0x36}};
  case 'C': return {{0x3e, 0x41, 0x41, 0x41, 0x22}};
  case 'D': return {{0x7f, 0x41, 0x41, 0x22, 0x1c}};
  case 'E': return {{0x7f, 0x49, 0x49, 0x49, 0x41}};
  case 'F': return {{0x7f, 0x09, 0x09, 0x09, 0x01}};
  case 'G': return {{0x3e, 0x41, 0x49, 0x49, 0x7a}};
  case 'H': return {{0x7f, 0x08, 0x08, 0x08, 0x7f}};
  case 'I': return {{0x00, 0x41, 0x7f, 0x41, 0x00}};
  case 'J': return {{0x20, 0x40, 0x41, 0x3f, 0x01}};
  case 'K': return {{0x7f, 0x08, 0x14, 0x22, 0x41}};
  case 'L': return {{0x7f, 0x40, 0x40, 0x40, 0x40}};
  case 'M': return {{0x7f, 0x02, 0x0c, 0x02, 0x7f}};
  case 'N': return {{0x7f, 0x04, 0x08, 0x10, 0x7f}};
  case 'O': return {{0x3e, 0x41, 0x41, 0x41, 0x3e}};
  case 'P': return {{0x7f, 0x09, 0x09, 0x09, 0x06}};
  case 'Q': return {{0x3e, 0x41, 0x51, 0x21, 0x5e}};
  case 'R': return {{0x7f, 0x09, 0x19, 0x29, 0x46}};
  case 'S': return {{0x46, 0x49, 0x49, 0x49, 0x31}};
  case 'T': return {{0x01, 0x01, 0x7f, 0x01, 0x01}};
  case 'U': return {{0x3f, 0x40, 0x40, 0x40, 0x3f}};
  case 'V': return {{0x1f, 0x20, 0x40, 0x20, 0x1f}};
  case 'W': return {{0x3f, 0x40, 0x38, 0x40, 0x3f}};
  case 'X': return {{0x63, 0x14, 0x08, 0x14, 0x63}};
  case 'Y': return {{0x07, 0x08, 0x70, 0x08, 0x07}};
  case 'Z': return {{0x61, 0x51, 0x49, 0x45, 0x43}};
  case '0': return {{0x3e, 0x51, 0x49, 0x45, 0x3e}};
  case '1': return {{0x00, 0x42, 0x7f, 0x40, 0x00}};
  case '2': return {{0x42, 0x61, 0x51, 0x49, 0x46}};
  case '3': return {{0x21, 0x41, 0x45, 0x4b, 0x31}};
  case '4': return {{0x18, 0x14, 0x12, 0x7f, 0x10}};
  case '5': return {{0x27, 0x45, 0x45, 0x45, 0x39}};
  case '6': return {{0x3c, 0x4a, 0x49, 0x49, 0x30}};
  case '7': return {{0x01, 0x71, 0x09, 0x05, 0x03}};
  case '8': return {{0x36, 0x49, 0x49, 0x49, 0x36}};
  case '9': return {{0x06, 0x49, 0x49, 0x29, 0x1e}};
  case ':': return {{0x00, 0x36, 0x36, 0x00, 0x00}};
  case '.': return {{0x00, 0x60, 0x60, 0x00, 0x00}};
  case ',': return {{0x00, 0x40, 0x20, 0x00, 0x00}};
  case '-': return {{0x08, 0x08, 0x08, 0x08, 0x08}};
  case '_': return {{0x40, 0x40, 0x40, 0x40, 0x40}};
  case '/': return {{0x20, 0x10, 0x08, 0x04, 0x02}};
  case '|': return {{0x00, 0x00, 0x7f, 0x00, 0x00}};
  case '\'': return {{0x00, 0x05, 0x03, 0x00, 0x00}};
  case '(': return {{0x00, 0x1c, 0x22, 0x41, 0x00}};
  case ')': return {{0x00, 0x41, 0x22, 0x1c, 0x00}};
  default: return {{0, 0, 0, 0, 0}};
  }
}

CalendarDate hostLocalDate() {
  const time_t now = time(nullptr);
  struct tm local = {};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  return {local.tm_year + 1900, static_cast<unsigned>(local.tm_mon + 1),
          static_cast<unsigned>(local.tm_mday)};
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
    case '&': output += "&amp;"; break;
    case '<': output += "&lt;"; break;
    case '>': output += "&gt;"; break;
    case '"': output += "&quot;"; break;
    case '\'': output += "&#39;"; break;
    default: output += *cursor; break;
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
  while (*++position == ' ' || *position == '\t') {}
  if (*position++ != '"') return std::string();
  std::string output;
  while (*position != '\0' && *position != '"') {
    if (*position == '\\' && position[1] != '\0') {
      ++position;
      if (*position == 'n') output += '\n';
      else if (*position == 'r') output += '\r';
      else output += *position;
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
  if (strncmp(_settings.calendarUrl, "file://", 7) != 0) {
    snprintf(_error, sizeof(_error), "Host source must be fixture://default or a local file:// .ics feed.");
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
  CalendarRange range = {calendarTodayRange(_today).startEpoch,
                         calendarRestOfWeekRange(_today).endEpoch,
                         _today,
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
    snprintf(_error, sizeof(_error), "Local iCalendar feed parser error %u.",
             static_cast<unsigned>(parser.error()));
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

void HostDisplayTarget::text(uint16_t x, uint16_t y, const char *value, uint8_t size) {
  if (value == nullptr || _impl == nullptr) return;
  const uint8_t scale = size >= 24 ? 3 : (size >= 16 ? 2 : 1);
  const uint8_t horizontalAdvance = size >= 24 ? 18 : (size >= 16 ? 9 : 6);
  uint16_t cursorX = x;
  for (const unsigned char *character = reinterpret_cast<const unsigned char *>(value);
       *character != '\0' && cursorX < kWidth; ++character) {
    if (*character >= 128) {
      cursorX = static_cast<uint16_t>(cursorX + 6 * scale);
      continue;
    }
    const std::array<uint8_t, 5> pattern = glyph(static_cast<char>(*character));
    for (uint8_t column = 0; column < pattern.size(); ++column) {
      const uint16_t columnX = size >= 16 && size < 24
                                 ? static_cast<uint16_t>(cursorX + (column * 3) / 2)
                                 : static_cast<uint16_t>(cursorX + column * scale);
      for (uint8_t row = 0; row < 7; ++row) {
        if ((pattern[column] & (1u << row)) == 0) continue;
        const uint8_t horizontalScale = size >= 16 && size < 24 ? 1 : scale;
        for (uint8_t dx = 0; dx < horizontalScale; ++dx) {
          for (uint8_t dy = 0; dy < scale; ++dy) {
            const uint16_t px = static_cast<uint16_t>(columnX + dx);
            const uint16_t py = static_cast<uint16_t>(y + row * scale + dy);
            if (px < kWidth && py < kHeight) {
              _impl->pixels[static_cast<size_t>(py) * kWidth + px] = 0;
            }
          }
        }
      }
    }
    cursorX = static_cast<uint16_t>(cursorX + horizontalAdvance);
  }
}

void HostDisplayTarget::line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
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
      _impl->pixels[static_cast<size_t>(y) * kWidth + x] = 0;
    }
    if (x == x2 && y == y2) break;
    const int twiceError = 2 * error;
    if (twiceError >= dy) { error += dy; x += sx; }
    if (twiceError <= dx) { error += dx; y += sy; }
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
    snprintf(interval, sizeof(interval), "%lu", static_cast<unsigned long>(settings.refreshIntervalSeconds));
    return std::string(
      "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width'>"
      "<title>Calendar emulator</title><style>body{font:16px system-ui;max-width:760px;margin:2rem auto;padding:0 1rem}"
      "label{display:block;margin:1rem 0 .3rem}input{width:100%;padding:.65rem;box-sizing:border-box}"
      "button{padding:.7rem;margin:.7rem .5rem .7rem 0}img{max-width:100%;border:1px solid #888}"
      "#status{white-space:pre-wrap}</style></head><body><h1>Calendar host emulator</h1>"
      "<p>Local settings are saved to <code>.dev/calendar-settings.json</code>. Fixture data works offline.</p>"
      "<form id=settings><label>Calendar URL</label><input name=calendar_url type=url value=\"" + url +
      "\" required><small>Use fixture://default or a local file://... .ics feed. Remote HTTPS is not fetched by host mode.</small>"
      "<label>Refresh interval (seconds)</label><input name=refresh_interval type=number min=60 max=604800 value=\"" +
      interval + "\" required><label>Timezone</label><input name=timezone value=\"" +
      escapeHtml(settings.timezone) +
      "\" required><button>Save settings</button></form><button id=refresh>Refresh now</button>"
      "<button id=reload>Reload settings and render</button><p id=status></p><h2>800 × 480 preview</h2>"
      "<img id=preview src='/preview.png' alt='Calendar display preview'>"
      "<script>const status=document.querySelector('#status');function updatePreview(){"
      "document.querySelector('#preview').src='/preview.png?'+Date.now()}document.querySelector('#settings')"
      ".addEventListener('submit',async e=>{e.preventDefault();const body=new URLSearchParams(new FormData(e.target));"
      "const r=await fetch('/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});"
      "status.textContent=await r.text();if(r.ok)updatePreview()});"
      "async function action(path){const r=await fetch(path,{method:'POST'});status.textContent=await r.text();"
      "if(r.ok)updatePreview()}document.querySelector('#refresh').onclick=()=>action('/refresh');"
      "document.querySelector('#reload').onclick=()=>action('/reload');</script></body></html>");
  }

  static void sendResponse(HostSocket client, int status, const char *contentType, const std::string &body) {
    const char *statusText = status == 200 ? "OK" : (status == 400 ? "Bad Request" :
                             (status == 404 ? "Not Found" : "Internal Server Error"));
    std::string header = std::string("HTTP/1.1 ") + std::to_string(status) + " " + statusText +
                         "\r\nContent-Type: " + contentType + "\r\nContent-Length: " +
                         std::to_string(body.size()) + "\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";
    const std::string response = header + body;
    size_t sent = 0;
    while (sent < response.size()) {
#if defined(_WIN32)
      const int count = ::send(client, response.data() + sent,
                               static_cast<int>(response.size() - sent), 0);
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
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuseAddress),
             sizeof(reuseAddress));
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
