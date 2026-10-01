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
  void text(uint16_t x, uint16_t y, const char *value, uint8_t size) override;
  void line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) override;
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
