// One decoded feed snapshot, already filtered to the board's routes (§3.3).
#pragma once
#include <stdint.h>

// More than the five routes run at once; any extra are counted in
// Feed::dropped and shown on the status page.
constexpr int MAX_VEHICLES = 64;
constexpr int MAX_OTHER_IDS = 24;

struct Vehicle {
  char id[24];
  char routeId[16];
  char label[16];
  int16_t dir;        // trip.direction_id, -1 when the feed leaves it out
  bool hasPos;
  float lat, lon;
  bool hasBearing;
  float bearing;
  uint64_t ts;        // vehicle timestamp (epoch s), 0 when absent
};

struct Feed {
  uint64_t t = 0;            // header timestamp, 0 when absent
  uint16_t count = 0;        // vehicles kept (routes on the board)
  uint16_t otherRoutes = 0;  // vehicles on routes the board doesn't show
  uint16_t dropped = 0;      // board vehicles past MAX_VEHICLES
  uint16_t noRouteId = 0;    // vehicles with no route_id at all (part of otherRoutes)
  // Distinct route_ids in the feed that the map doesn't have, for debugging a
  // route that never lights (e.g. the feed says "801-123" and the map "801").
  uint8_t otherIdCount = 0;
  char otherIds[MAX_OTHER_IDS][16];
  Vehicle v[MAX_VEHICLES];

  void clearCounts() { count = otherRoutes = dropped = noRouteId = 0; otherIdCount = 0; }
  void noteOther(const char *routeId);
};
