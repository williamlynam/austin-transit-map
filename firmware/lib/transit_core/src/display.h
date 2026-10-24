// Turns "which routes are on which LED" into colours (FIRMWARE_DESIGN.md §5,
// as changed by Will on 2026-10-06):
// - one colour per route, no direction tint;
// - lineView (Will, 0.1.12): every LED of a route shows its colour and
//   vehicles show as white dots; off by default;
// - on a shared LED (D10, D23) the chain's own route wins, so the 550 train
//   always shows on top of a crossing bus;
// - several vehicles on one LED look like one (Will, 0.1.12);
// - vehicles jump straight to their new LED (Will, 2026-10-07: no fading).
//   fadeMs > 0 brings back a crossfade of that length.
#pragma once
#include <stdint.h>

#include "board.h"
#include "rgb.h"
#include "engine.h"
#include "ledmap.h"

enum class DisplayMode : uint8_t { Live = 0, Simulated = 1, ChainTest = 2 };

class Display {
 public:
  explicit Display(const LedMap &m) : map(m) {}
  uint32_t fadeMs = 0;
  uint32_t chainStepMs = 120;  // chain test: how long the chase stays on each LED
  bool lineView = false;       // whole line in route colour, vehicles white
  // Line view: the line LEDs are scaled by this so the vehicles, sent at a
  // higher overall brightness, stand out from the line.
  float lineLevel = 1.0f;

  void setOccupancy(const Occupancy &occ);
  // Colours for every physical LED at time nowMs.
  void render(uint32_t nowMs, DisplayMode mode, Rgb out[board::NUM_LEDS]);

  // The colour an LED should show for these occupants (no fading).
  Rgb colorFor(uint16_t led, const uint8_t *routes, uint8_t n) const;

 private:
  const LedMap &map;
  bool lit[board::NUM_LEDS] = {};
  Rgb hue[board::NUM_LEDS];
  float level[board::NUM_LEDS] = {};
  uint32_t lastMs = 0;
  bool started = false;
};
