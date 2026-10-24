// Live_Map_4layers rev A pin and chain table (FIRMWARE_DESIGN.md §1).
// ledmap.json has to agree with this table; the map loader refuses one that
// doesn't (§7).
#pragma once
#include <stdint.h>

namespace board {

struct Chain {
  const char *name;  // net name on the PCB, used as "chain" in ledmap.json
  uint8_t gpio;
  uint8_t count;
  uint8_t offset;    // first index of this chain in the flat LED array
};

constexpr int NUM_CHAINS = 5;
constexpr int NUM_LEDS = 197;

constexpr Chain CHAINS[NUM_CHAINS] = {
    {"LINE1", 16, 41, 0},    // 550 MetroRail Red Line, D1-D41
    {"LINE2", 21, 37, 41},   // 803 Rapid, D42-D78
    {"LINE3", 19, 44, 78},   // 801 Rapid, D79-D122
    {"LINE4", 17, 41, 122},  // 837 Rapid, D123-D163
    {"LINE5", 18, 34, 163},  // 800 Rapid, D164-D197
};

constexpr uint8_t PIN_WIFI_LED = 25;  // active high, amber D1005
constexpr uint8_t PIN_MODE_BTN = 26;  // SW1003, active low, no external pull-up
constexpr uint8_t PIN_BOOT_BTN = 0;   // SW1001 (IO0), active low

}  // namespace board
