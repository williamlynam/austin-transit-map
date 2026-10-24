// MODE (IO26) and IO0 buttons, both active low.
#pragma once
#include <stdint.h>

enum class ButtonEvent : uint8_t { None, ModeShort, ModeLong, BootLong };

namespace buttons {
void begin();
ButtonEvent poll();  // call every loop; at most one event per call
}  // namespace buttons
