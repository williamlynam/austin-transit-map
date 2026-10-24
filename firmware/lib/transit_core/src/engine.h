// What the firmware does with one feed snapshot (FIRMWARE_DESIGN.md §4):
// drop stale vehicles, pick the direction path, snap to an LED, apply
// hysteresis, and collect the vehicles on each physical LED.
//
// This mirrors live-preview/js/engine.js and Geo.snapToPath line for line, so
// the preview and the board light the same LED for the same feed.
#pragma once
#include <stdint.h>

#include "board.h"
#include "feed.h"
#include "ledmap.h"

constexpr int MAX_PER_LED = 4;

// Which routes have a vehicle on each physical LED (route indexes into
// LedMap::routes). This is all the renderer needs.
struct Occupancy {
  uint8_t n[board::NUM_LEDS];
  uint8_t route[board::NUM_LEDS][MAX_PER_LED];
  void clear();
  void add(uint16_t led, uint8_t routeIndex);
};

enum class VState : uint8_t { Lit, Hidden, Stale, NoPosition, OffRoute };
enum class How : uint8_t { SinglePath, DirectionId, Bearing, Unknown };

struct Placement {
  VState state;
  How how;
  int8_t route;     // index into LedMap::routes
  int8_t path;      // index into LedMap::paths, -1 if none
  int16_t led;      // physical LED, -1 if none
  bool held;        // hysteresis kept the previous LED
  bool borrowed;    // the LED belongs to another route's chain (D10/D23)
  float distM;      // distance from the route line
};

struct EngineOptions {
  uint32_t staleS = 120;
  bool hysteresis = true;
  float hyst = 0.6f;
};

// Snap result over one path's usable points (Geo.snapToPath).
struct SnapHit {
  int16_t led;
  float d;
  int k;    // segment index
  float t;  // projection parameter on the segment
};

bool snapToPath(const LedMap &map, const MapPath &path, float lat, float lon, SnapHit &hit);

class Engine {
 public:
  explicit Engine(const LedMap &m) : map(m) {}
  EngineOptions opts;

  void reset() { nprev = 0; }

  // nowS = 0 means "clock unknown": vehicle staleness is then not checked.
  // place[k] receives the outcome for feed.v[k].
  void update(const Feed &feed, uint64_t nowS, Occupancy &out, Placement *place);

 private:
  struct Prev {
    char id[24];
    int8_t route, path;
    int16_t led;
  };
  const LedMap &map;
  Prev prev[MAX_VEHICLES];
  int nprev = 0;

  int choosePath(const MapRoute &r, const Vehicle &v, How &how) const;
  bool hold(const Vehicle &v, int route, int path, SnapHit &hit) const;
};
