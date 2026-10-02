#pragma once

#include <stdint.h>

namespace calendar {

  enum class DisplayColor : uint8_t {
    Black = 0,
    DarkGrey = 1,
    LightGrey = 2,
    White = 3,
  };

// The calendar uses the four logical grayscale samples supported by bb_epaper's
// 7.5-inch 4-gray profiles. The embedded adapter passes these values directly
// to the driver, which maps them to the calibrated two-plane panel encoding.
  namespace CalendarColors {
    constexpr DisplayColor Background = DisplayColor::White;
    constexpr DisplayColor Foreground = DisplayColor::Black;
    constexpr DisplayColor Border = DisplayColor::Black;
    constexpr DisplayColor Muted = DisplayColor::DarkGrey;
    constexpr DisplayColor Sidebar = DisplayColor::LightGrey;
  } // namespace CalendarColors

  constexpr uint8_t kCalendarFontMain = 36;
  constexpr uint8_t kCalendarFontHeading = 18;
  constexpr uint8_t kCalendarFontTitle = 16;
  constexpr uint8_t kCalendarFontMetadata = 13;
  constexpr uint8_t kCalendarFontFooter = 11;
// Status text shares the compact Manrope Medium bitmap used by the footer.
  constexpr uint8_t kCalendarFontStatus = 12;

  struct TextVerticalBounds {
    int16_t top; // Relative to the drawing baseline.
    uint16_t height;
  };

  class DisplayTarget {
  public:
    virtual ~DisplayTarget() = default;
    virtual bool begin() = 0;
    virtual void text(uint16_t x, uint16_t y, const char *value, uint8_t size,
                      DisplayColor foreground = DisplayColor::Black, DisplayColor background = DisplayColor::White) = 0;
    virtual uint16_t textWidth(const char *value, uint8_t size) = 0;
  // Capital glyph height above the baseline, excluding converter leading.
    virtual uint8_t fontHeight(uint8_t size) = 0;
    virtual TextVerticalBounds textVerticalBounds(const char *, uint8_t size) {
      return {static_cast<int16_t>(-fontHeight(size)), fontHeight(size)};
    }
    virtual void line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, DisplayColor color = DisplayColor::Black) = 0;
    virtual void fillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, DisplayColor color) = 0;
    virtual void roundRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t radius, DisplayColor fill,
                           DisplayColor border) = 0;
    virtual void circle(uint16_t x, uint16_t y, uint16_t radius, DisplayColor color, bool filled) = 0;
  // bb_epaper sprite format: row-major, MSB first, 1 = ink, 0 = transparent.
    virtual void bitmap(uint16_t x, uint16_t y, const uint8_t *data, uint16_t width, uint16_t height,
                        DisplayColor color) = 0;
    virtual bool refresh() = 0;
  };

} // namespace calendar
