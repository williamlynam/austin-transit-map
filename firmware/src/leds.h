// The five WS2812B chains, sent one after another through one RMT channel.
#pragma once
#include "board.h"
#include "rgb.h"

namespace leds {
void begin(uint16_t maxMa);
void setPowerLimit(uint16_t maxMa);
void show(const Rgb frame[board::NUM_LEDS], uint8_t brightness);
}  // namespace leds
