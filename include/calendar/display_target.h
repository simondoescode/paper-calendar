#pragma once

#include <stdint.h>

namespace calendar {

enum class DisplayColor : uint8_t {
  Black = 0,
  White = 1,
  // Pattern token, never a hardware grey level. Only rectangle fills and text
  // backgrounds support it on the embedded adapter.
  DitherLightGrey = 2,
};

// Canonical solid palette for the one-bit calendar display.
namespace CalendarColors {
constexpr DisplayColor Background = DisplayColor::White;
constexpr DisplayColor Foreground = DisplayColor::Black;
constexpr DisplayColor Border = DisplayColor::Black;
}

// Simulated tones are kept separate from physical colours.
namespace CalendarPatterns {
constexpr DisplayColor Sidebar = DisplayColor::DitherLightGrey;
constexpr uint8_t kSidebarToken = static_cast<uint8_t>(Sidebar);
constexpr bool sidebarInk(int x, int y) { return ((x + 2 * y) & 3) == 0; }
}

constexpr uint8_t kCalendarFontMain = 36;
constexpr uint8_t kCalendarFontHeading = 18;
constexpr uint8_t kCalendarFontTitle = 16;
constexpr uint8_t kCalendarFontMetadata = 13;
constexpr uint8_t kCalendarFontFooter = 11;
// Status text shares the compact Manrope Medium bitmap used by the footer.
constexpr uint8_t kCalendarFontStatus = 12;

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
  // bb_epaper sprite format: row-major, MSB first, 1 = ink, 0 = transparent.
  virtual void bitmap(uint16_t x, uint16_t y, const uint8_t *data, uint16_t width, uint16_t height,
                      DisplayColor color) = 0;
  virtual bool refresh() = 0;
};

} // namespace calendar
