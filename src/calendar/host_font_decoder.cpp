#if defined(CALENDAR_HOST)
// Use the same portable Group 5 decoder as bb_epaper 2.1.9.
#include "host_font_decoder/g5dec.inl"

int G5DECODER::init(int width, int height, uint8_t *data, int length) {
  return g5_decode_init(&_g5dec, width, height, data, length);
}

int G5DECODER::decodeLine(uint8_t *output) {
  return g5_decode_line(&_g5dec, output);
}
#endif
