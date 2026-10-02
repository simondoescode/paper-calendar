#pragma once

#include <calendar/display_target.h>
#include <stddef.h>

namespace calendar {

// Small bb_epaper font glyph records: width, height and signed yOffset.
// The reader supports both host memory and embedded flash without allocation.
template <typename ReadByte>
TextVerticalBounds smallFontTextVerticalBounds(const uint8_t *font, const char *text, ReadByte read) {
  const unsigned first = read(font + 2) | (read(font + 3) << 8);
  const unsigned last = read(font + 4) | (read(font + 5) << 8);
  int16_t top = 0, bottom = 0;
  bool found = false;
  if (text != nullptr) {
    for (const unsigned char *ch = reinterpret_cast<const unsigned char *>(text); *ch; ++ch) {
      if (*ch < first || *ch > last) continue;
      const uint8_t *glyph = font + 12 + static_cast<size_t>(*ch - first) * 8;
      const uint8_t height = read(glyph + 4);
      if (!read(glyph + 2) || !height) continue;
      const int16_t glyphTop = static_cast<int8_t>(read(glyph + 6));
      const int16_t glyphBottom = glyphTop + height;
      if (!found || glyphTop < top) top = glyphTop;
      if (!found || glyphBottom > bottom) bottom = glyphBottom;
      found = true;
    }
  }
  return {top, static_cast<uint16_t>(bottom - top)};
}

} // namespace calendar
