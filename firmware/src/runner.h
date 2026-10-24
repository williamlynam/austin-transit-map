// The network task: fetches the feed (or samples the simulator), decodes it,
// matches vehicles to LEDs and publishes the result for the render loop.
// Runs on core 0 so the LEDs (core 1) never wait on the network.
#pragma once
#include <Arduino.h>

#include "engine.h"
#include "ledmap.h"

struct RouteStats {
  uint16_t lit, hidden, offRoute, stale, noPosition;
};

struct Status {
  uint32_t seq = 0;            // bumps on every publish
  bool feedOk = false;         // last live fetch worked
  String lastError;            // empty when fine
  int httpCode = 0;
  uint32_t lastFetchMs = 0;    // millis() of the last attempt
  uint32_t lastChangeMs = 0;   // millis() when the feed content last changed
  uint32_t fetchMs = 0;        // how long the last fetch took
  uint32_t bytes = 0;
  uint64_t feedT = 0;          // header timestamp of the current data
  uint16_t vehicles = 0, otherRoutes = 0, dropped = 0, noRouteId = 0;
  uint8_t otherIdCount = 0;
  char otherIds[MAX_OTHER_IDS][16];
  uint32_t nextPollInMs = 0;
  RouteStats routes[32];
};

namespace runner {
void begin(LedMap &map);
// Copy the latest occupancy if it changed since lastSeq. Returns true if copied.
bool takeOccupancy(uint32_t &lastSeq, Occupancy &out);
Status status();
void restartSimulation();  // mode changed to Simulated, or map reloaded
}  // namespace runner
