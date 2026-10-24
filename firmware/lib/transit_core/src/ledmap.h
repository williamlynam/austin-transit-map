// ledmap.json (FIRMWARE_DESIGN.md §7), parsed once at boot into compact arrays.
//
// Positions are per route: a route's own LEDs come from route.leds, and the
// LEDs it borrows from another chain (the D10/D23 crossings) come from
// route.shared with that route's own lat/lon. Snapping runs over paths: each
// path keeps only its placed, non-skipped LEDs, already projected to metres.
#pragma once
#include <stdint.h>

#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "board.h"
#include "rgb.h"

struct MapPoint {
  uint16_t led;  // physical LED, 0..board::NUM_LEDS-1
  float x, y;    // metres from the projection origin
};

struct MapPath {
  int16_t dir;        // GTFS direction_id this path serves, -1 for "both"
  bool north, south;  // name matches /north/i, /south/i (bearing fallback, §4.2)
  char name[24];
  uint16_t first, count;  // slice of LedMap::points
};

struct MapRoute {
  char id[16];
  char name[40];
  uint8_t chain;  // index into board::CHAINS
  Rgb brand;      // "color" from the map (CapMetro brand colour)
  Rgb color;      // what the LEDs show: brand, unless overridden in settings
  uint8_t firstPath, pathCount;  // slice of LedMap::paths
  // Physical LEDs named by every one of the route's paths (raw seq, placed or
  // not). A vehicle with unknown direction may only light one of these (§4.2).
  uint32_t inAllPaths[(board::NUM_LEDS + 31) / 32];
  uint16_t placed, total;  // LEDs with lat/lon over LEDs named by the route
  bool inAll(uint16_t led) const { return inAllPaths[led >> 5] & (1u << (led & 31)); }
};

class LedMap {
 public:
  // Fill a filter that keeps only the keys load() reads, for deserializeJson.
  static void makeFilter(JsonDocument &filter);

  // Parse and validate. On failure returns false, err says why, and the map
  // is left empty.
  bool load(JsonVariantConst doc, std::string &err);
  void clear();

  bool empty() const { return routes.empty(); }
  int routeIndex(const char *routeId) const;
  int ownerRoute(uint16_t led) const { return led < board::NUM_LEDS ? owner[led] : -1; }
  const char *ref(uint16_t led) const { return led < board::NUM_LEDS ? refs[led] : ""; }
  int ledByRef(const char *ref) const;

  // Equirectangular projection to metres (§4.3). float32 end to end, like the
  // feed's own lat/lon; against the preview's double maths this differs by
  // well under a metre, which never changes which LED lights in practice.
  void project(float lat, float lon, float &x, float &y) const;
  void unproject(float x, float y, float &lat, float &lon) const;

  float lat0 = 30.2672f, lon0 = -97.7431f;
  float kx = 0, ky = 110540.0f;
  float maxSnapM = 400;
  float snapFraction = 0.3f;  // long segments get a corridor this share of their length
  std::string notes;  // the map's "notes", shown on the status page

  std::vector<MapRoute> routes;
  std::vector<MapPath> paths;
  std::vector<MapPoint> points;
  int8_t owner[board::NUM_LEDS];  // route owning each physical LED, -1 if none
  static constexpr int REF_LEN = 6;  // "D197" and room to spare
  char refs[board::NUM_LEDS][REF_LEN];
};
