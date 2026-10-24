#include "display.h"

Rgb Display::colorFor(uint16_t led, const uint8_t *routes, uint8_t n) const {
  if (!n) return Rgb();
  int owner = map.ownerRoute(led);
  int pick = routes[0];
  for (uint8_t k = 0; k < n; k++)
    if (routes[k] == owner) pick = owner;  // the chain's own route on top
  return pick < int(map.routes.size()) ? map.routes[pick].color : Rgb(255, 255, 255);
}

void Display::setOccupancy(const Occupancy &occ) {
  for (int i = 0; i < board::NUM_LEDS; i++) {
    lit[i] = occ.n[i] > 0;
    if (lit[i]) hue[i] = colorFor(uint16_t(i), occ.route[i], occ.n[i]);
  }
}

void Display::render(uint32_t nowMs, DisplayMode mode, Rgb out[board::NUM_LEDS]) {
  if (mode == DisplayMode::ChainTest) {
    // Each chain in its own colour, dim, with a bright chase from index 0 so
    // the wiring order and the route-to-GPIO table can be checked by eye.
    static const Rgb chainColors[board::NUM_CHAINS] = {
        Rgb(255, 0, 0), Rgb(0, 255, 0), Rgb(0, 0, 255), Rgb(255, 160, 0), Rgb(200, 0, 255)};
    for (int c = 0; c < board::NUM_CHAINS; c++) {
      const board::Chain &ch = board::CHAINS[c];
      int head = int((nowMs / (chainStepMs ? chainStepMs : 1)) % ch.count);
      for (int i = 0; i < ch.count; i++)
        out[ch.offset + i] = i == head ? Rgb(255, 255, 255) : scale(chainColors[c], 0.15f);
    }
    started = false;
    return;
  }

  // The first frame starts the clock; from there levels move 1/fadeMs per ms.
  float step = 0.0f;
  if (!fadeMs) step = 1.0f;  // no fading: every change shows at once
  else if (started) step = float(nowMs - lastMs) / float(fadeMs);
  lastMs = nowMs;
  started = true;
  for (int i = 0; i < board::NUM_LEDS; i++) {
    float target = lit[i] ? 1.0f : 0.0f;
    if (level[i] < target) level[i] = level[i] + step > target ? target : level[i] + step;
    else if (level[i] > target) level[i] = level[i] - step < target ? target : level[i] - step;
    if (lineView) {
      int owner = map.ownerRoute(uint16_t(i));
      Rgb line = owner >= 0 && owner < int(map.routes.size()) ? scale(map.routes[owner].color, lineLevel) : Rgb();
      out[i] = mix(line, Rgb(255, 255, 255), level[i]);
    } else {
      out[i] = scale(hue[i], level[i]);
    }
  }
}
