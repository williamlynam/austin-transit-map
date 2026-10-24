// The settings page: http://transitmap-XXXX.local (last 4 MAC digits) on your Wi-Fi, or
// http://192.168.4.1 from the setup hotspot.
#pragma once
#include <Arduino.h>

#include "ledmap.h"

namespace web {
// mapError: why the LED map didn't load (empty if it did).
// onSettings: called on the main loop after settings were saved.
void begin(LedMap &map, const String &mapError, void (*onSettings)());
void loop();
}  // namespace web
