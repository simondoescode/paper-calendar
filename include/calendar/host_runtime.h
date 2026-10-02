#pragma once

#include <calendar/calendar.h>
#include <calendar/display_target.h>
#include <calendar/settings.h>

#include <functional>

namespace calendar {

class HostCalendarProvider : public CalendarProvider {
public:
  HostCalendarProvider(const CalendarSettings &settings, CalendarDate today);
  size_t loadEvents(CalendarEvent *events, size_t capacity) const override;
  const char *error() const;

private:
  CalendarSettings _settings;
  CalendarDate _today;
  mutable char _error[128];
};

class HostDisplayTarget : public DisplayTarget {
public:
  explicit HostDisplayTarget(const char *path);
  ~HostDisplayTarget() override;
  bool begin() override;
  void text(uint16_t x, uint16_t y, const char *value, uint8_t size, DisplayColor foreground,
            DisplayColor background) override;
  uint16_t textWidth(const char *value, uint8_t size) override;
  uint8_t fontHeight(uint8_t size) override;
  TextVerticalBounds textVerticalBounds(const char *value, uint8_t size) override;
  void line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, DisplayColor color) override;
  void fillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, DisplayColor color) override;
  void roundRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t radius,
                 DisplayColor fill, DisplayColor border) override;
  void circle(uint16_t x, uint16_t y, uint16_t radius, DisplayColor color, bool filled) override;
  void bitmap(uint16_t x, uint16_t y, const uint8_t *data, uint16_t width, uint16_t height,
              DisplayColor color) override;
  bool refresh() override;
  bool writeStatus(DisplayStatus status, const char *message);

private:
  struct Impl;
  Impl *_impl;
};

class HostPortal {
public:
  HostPortal(ConfigStore &store, HostDisplayTarget &display, const std::function<void()> &refresh);
  ~HostPortal();
  HostPortal(const HostPortal &) = delete;
  HostPortal &operator=(const HostPortal &) = delete;
  int run();

private:
  struct Impl;
  Impl *_impl;
};

} // namespace calendar
