// Where vehicles come from when they don't come from CapMetro.
//
// The simulated feed is one VehicleSource. A timetable-driven source can be
// added later as another one without touching decoding, matching or rendering.
#pragma once
#include <stdint.h>

#include "feed.h"

class VehicleSource {
 public:
  virtual ~VehicleSource() = default;
  virtual const char *name() const = 0;
  // Fill out with the vehicles at time nowMs (milliseconds since the source
  // started) and epoch time nowS (0 if the clock isn't set yet).
  virtual void sample(uint32_t nowMs, uint64_t nowS, Feed &out) = 0;
};
