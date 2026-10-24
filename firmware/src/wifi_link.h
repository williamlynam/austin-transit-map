// Wi-Fi, the setup hotspot and the WIFI status LED.
//
// - No saved network: the hotspot "TransitMap-XXXX" (open) turns on by itself.
// - Saved network unreachable for 5 minutes: the hotspot turns on by itself.
// - The hotspot never turns itself off (Will, 2026-10-06): it stays on after
//   the board joins Wi-Fi until MODE is held for 3 s. MODE long press toggles it.
// - The board runs normally with the hotspot off.
//
// WIFI LED (IO25), as Will asked: blinks while the hotspot is on, off when not
// connected, solid when connected. Blinking wins over solid.
//
// Debug builds: compile with -DHOTSPOT_ALWAYS_ON=1 (env transitmap_debug) and
// the hotspot turns on at boot and stays on. MODE long press and Complete
// Setup leave it on. Release builds leave the flag at 0 and behave as above.
#pragma once
#include <Arduino.h>

#ifndef HOTSPOT_ALWAYS_ON
#define HOTSPOT_ALWAYS_ON 0
#endif

namespace wifilink {
void begin();
void loop();  // call often from the main loop

bool connected();
bool hotspotOn();
constexpr bool hotspotLocked() { return HOTSPOT_ALWAYS_ON != 0; }
String hotspotName();
// This board's address on Wi-Fi, e.g. "transitmap-ab12.local".
String localName();
String ssid();  // the saved network, empty if none

void toggleHotspot();  // MODE long press
void hotspotOnByHand();  // the Setup network switch on the web page
// "Complete Setup" on the web page: turn the hotspot off. Refused (false)
// while the board isn't on Wi-Fi, so you can't lock yourself out.
bool hotspotOff();
// Save a network and start joining it. The hotspot stays as it is.
void setNetwork(const String &ssid, const String &pass);
void forgetNetwork();  // factory reset
}  // namespace wifilink
