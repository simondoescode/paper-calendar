#pragma once

#include <stdint.h>

namespace calendar {

enum class DisplayColor : uint8_t {
  Black,
  White,
  LightGrey,
};

constexpr uint8_t kCalendarFontMain = 52;
constexpr uint8_t kCalendarFontHeading = 28;
constexpr uint8_t kCalendarFontTitle = 20;
constexpr uint8_t kCalendarFontMetadata = 16;
constexpr uint8_t kCalendarFontFooter = 14;

class DisplayTarget {
public:
  virtual ~DisplayTarget() = default;
  virtual bool begin() = 0;
  virtual void text(uint16_t x, uint16_t y, const char *value, uint8_t size,
                    DisplayColor foreground = DisplayColor::Black,
                    DisplayColor background = DisplayColor::White) = 0;
  virtual uint16_t textWidth(const char *value, uint8_t size) = 0;
  // Capital glyph height above the baseline, excluding converter leading.
  virtual uint8_t fontHeight(uint8_t size) = 0;
  virtual void line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                    DisplayColor color = DisplayColor::Black) = 0;
  virtual void fillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, DisplayColor color) = 0;
  virtual void roundRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t radius,
                         DisplayColor fill, DisplayColor border) = 0;
  virtual void circle(uint16_t x, uint16_t y, uint16_t radius, DisplayColor color, bool filled) = 0;
  virtual bool refresh() = 0;
};

} // namespace calendar
