#pragma once
#include <stdint.h>

struct Rgb {
  uint8_t r = 0, g = 0, b = 0;
  constexpr Rgb() = default;
  constexpr Rgb(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}
  bool operator==(const Rgb &o) const { return r == o.r && g == o.g && b == o.b; }
};

// "#RRGGBB" or "RRGGBB". Returns false (and leaves out alone) on anything else.
bool parseHexColor(const char *s, Rgb &out);
void formatHexColor(const Rgb &c, char out[8]);

// a + (b - a) * f, f in 0..1.
Rgb mix(const Rgb &a, const Rgb &b, float f);
Rgb scale(const Rgb &c, float f);
