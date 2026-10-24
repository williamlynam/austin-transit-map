// User settings, kept in /config.json on LittleFS and edited on the web page.
// Wi-Fi credentials live separately in NVS (see wifi_link.cpp).
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "rgb.h"
#include "display.h"
#include "sim.h"

#ifndef TRANSIT_DEFAULT_MODE
#define TRANSIT_DEFAULT_MODE 0
#endif

constexpr const char *DEFAULT_GH_REPO = "williamlynam/austin-transit-map";
constexpr const char *DEFAULT_FEED_URL = "https://data.texas.gov/download/eiei-9rpf/application%2Foctet-stream";

struct ColorOverride {
  std::string routeId;
  Rgb color;
};

struct Settings {
  DisplayMode mode = DisplayMode(TRANSIT_DEFAULT_MODE);
  // Below 2/255 (0.8%) the LEDs' 8-bit steps distort the route colors.
  static constexpr uint8_t MIN_BRIGHTNESS = 2;
  uint8_t brightness[3] = {3, 8, 26};     // MODE button presets (of 255): 1%, 3%, 10%
  uint16_t lineVehiclePct = 150;          // line view: vehicle brightness, % of the line
  bool lineView = false;                  // whole line in route color, vehicles white
  uint8_t level = 0;                      // which preset is active
  bool nightEnabled = true;
  uint16_t nightStart = 22 * 60, nightEnd = 6 * 60;  // minutes after midnight, Austin time
  uint8_t nightPct = 15;                             // % of the active preset at night
  uint16_t maxMa = 1500;                             // FastLED power limit at 5 V
  uint16_t pollS = 30;                               // 30..60
  String feedUrl = DEFAULT_FEED_URL;
  uint16_t connectS = 15;                            // DNS + TCP + TLS together
  uint16_t readS = 20;                               // reading the body
  float simTimeScale = 8.0f;
  uint16_t chainStepMs = 120;                        // chain test: ms per LED step
  std::vector<SimRoute> simRoutes;                   // vehicles per hour and mph per route
  String ghRepo = DEFAULT_GH_REPO;                   // firmware updates: owner/name of a public repo
  bool ghPrerelease = false;                         // also offer pre-releases
  bool ghDailyCheck = true;                          // look for an update once a day (never installs)
  // The first-time setup was finished (on Wi-Fi, or "Set up without Wi-Fi").
  // Phones joining the hotspot then get the settings page, not the setup steps.
  bool setupDone = false;
  std::vector<ColorOverride> colors;                 // manual colour per route_id

  Settings() { defaultSimRoutes(simRoutes); }
  static constexpr uint16_t MIN_POLL_S = 30, MAX_POLL_S = 60;
  void clamp();
};

// Global settings, guarded by a mutex because the network task reads them.
namespace settings {
void begin();                 // load /config.json (defaults if missing)
Settings get();               // a copy
void set(const Settings &s);  // replace and save
void toJson(const Settings &s, JsonObject out);
// Apply fields present in a JSON object. Returns false with err on bad input.
bool fromJson(JsonObjectConst in, Settings &s, String &err);
void factoryReset();          // delete /config.json
}  // namespace settings
