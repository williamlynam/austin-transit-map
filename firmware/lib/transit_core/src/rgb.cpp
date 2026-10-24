#include "rgb.h"

#include <stdio.h>

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseHexColor(const char *s, Rgb &out) {
  if (!s) return false;
  if (*s == '#') s++;
  uint8_t v[3];
  for (int k = 0; k < 3; k++) {
    int hi = hexval(s[2 * k]);
    if (hi < 0) return false;
    int lo = hexval(s[2 * k + 1]);
    if (lo < 0) return false;
    v[k] = uint8_t(hi * 16 + lo);
  }
  if (s[6] != '\0') return false;
  out = Rgb(v[0], v[1], v[2]);
  return true;
}

void formatHexColor(const Rgb &c, char out[8]) { snprintf(out, 8, "#%02X%02X%02X", c.r, c.g, c.b); }

static uint8_t lerp8(uint8_t a, uint8_t b, float f) {
  float v = a + (float(b) - float(a)) * f;
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  return uint8_t(v + 0.5f);
}

Rgb mix(const Rgb &a, const Rgb &b, float f) {
  if (f <= 0) return a;
  if (f >= 1) return b;
  return Rgb(lerp8(a.r, b.r, f), lerp8(a.g, b.g, f), lerp8(a.b, b.b, f));
}

Rgb scale(const Rgb &c, float f) { return mix(Rgb(), c, f); }
