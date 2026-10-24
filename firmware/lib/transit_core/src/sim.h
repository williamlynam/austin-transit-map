// Simulated feed: made-up vehicles driving every route of the LED map.
//
// Each vehicle moves along one of its route's paths using the map's own LED
// coordinates, so it exercises the shared crossing LEDs (D10, D23) and the
// 837 one-way loop. The result is encoded as a real GTFS-rt FeedMessage and
// decoded again, so the whole pipeline after the network is tested.
#pragma once
#include <stdint.h>

#include <vector>

#include "ledmap.h"
#include "source.h"

// How one route is simulated (Will, 2026-10-07: set on the web page).
struct SimRoute {
  char routeId[16];
  float perHour;  // vehicles per hour in each direction
  float mph;      // driving speed
};

struct SimOptions {
  float timeScale = 8.0f;      // run the clock faster so movement is visible
  float jitterM = 20.0f;       // GPS noise across the line
  // Routes not listed here use these.
  float defaultPerHour = 4.0f;
  float defaultMph = 15.0f;
  std::vector<SimRoute> routes;
  // Tests only: every 4th vehicle omits direction_id to exercise the bearing
  // fallback. Off on the board, where it put some 837 buses off route.
  bool someWithoutDirection = false;
};

// Starting values for the web page's Simulation settings. Placeholders until
// Will picks real ones.
void defaultSimRoutes(std::vector<SimRoute> &out);

class SimSource : public VehicleSource {
 public:
  explicit SimSource(const LedMap &m) : map(m) {}
  SimOptions opts;

  const char *name() const override { return "simulated"; }
  // (Re)build the vehicle list from the map. Returns how many vehicles.
  int setup();
  void sample(uint32_t nowMs, uint64_t nowS, Feed &out) override;

  // For tests: the path a simulated vehicle was put on.
  int pathOf(const char *vehicleId) const;

 private:
  struct SimVehicle {
    uint8_t route;
    uint8_t path;      // index into LedMap::paths
    bool reverse;      // drives the path backwards (second direction of a single-path route)
    int16_t dir;       // direction_id to report, -1 = leave it out
    float offsetM;     // start position along the path
    float lengthM;
    float speedMps;
    char id[24];
  };
  const LedMap &map;
  std::vector<SimVehicle> vehicles;

  float pathLength(int path) const;
  // Point and heading at distance s along a path.
  void pointAt(int path, float s, float &x, float &y, float &headingDeg) const;
};
