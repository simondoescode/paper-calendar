#pragma once

#include <stdint.h>

namespace calendar {

class DisplayTarget {
public:
  virtual ~DisplayTarget() = default;
  virtual bool begin() = 0;
  virtual void text(uint16_t x, uint16_t y, const char *value, uint8_t size) = 0;
  virtual void line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) = 0;
  virtual bool refresh() = 0;
};

} // namespace calendar
