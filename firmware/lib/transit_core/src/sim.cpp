#include "sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// Small deterministic hash so the jitter is repeatable between runs.
float noise(uint32_t a, uint32_t b) {
  uint32_t h = a * 2654435761u ^ (b + 0x9E3779B9u + (a << 6) + (a >> 2));
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return float(h & 0xFFFF) / 32767.5f - 1.0f;  // -1..1
}

}  // namespace

void defaultSimRoutes(std::vector<SimRoute> &out) {
  static const SimRoute defs[] = {
      {"550", 2, 35}, {"801", 6, 15}, {"803", 4, 15}, {"837", 4, 12}, {"800", 4, 15}};
  out.assign(defs, defs + sizeof defs / sizeof defs[0]);
}

float SimSource::pathLength(int path) const {
  const MapPath &p = map.paths[path];
  float len = 0;
  for (int k = 1; k < p.count; k++) {
    const MapPoint &a = map.points[p.first + k - 1], &b = map.points[p.first + k];
    len += hypotf(b.x - a.x, b.y - a.y);
  }
  return len;
}

void SimSource::pointAt(int path, float s, float &x, float &y, float &headingDeg) const {
  const MapPath &p = map.paths[path];
  for (int k = 1; k < p.count; k++) {
    const MapPoint &a = map.points[p.first + k - 1], &b = map.points[p.first + k];
    float seg = hypotf(b.x - a.x, b.y - a.y);
    if (s <= seg || k == p.count - 1) {
      float t = seg > 0 ? fminf(1.0f, fmaxf(0.0f, s / seg)) : 0;
      x = a.x + t * (b.x - a.x);
      y = a.y + t * (b.y - a.y);
      headingDeg = atan2f(b.x - a.x, b.y - a.y) * 180.0f / float(M_PI);
      if (headingDeg < 0) headingDeg += 360.0f;
      return;
    }
    s -= seg;
  }
  const MapPoint &only = map.points[p.first];
  x = only.x;
  y = only.y;
  headingDeg = 0;
}

int SimSource::setup() {
  vehicles.clear();
  int n = 0;
  for (size_t ri = 0; ri < map.routes.size(); ri++) {
    const MapRoute &r = map.routes[ri];
    float perHour = opts.defaultPerHour, mph = opts.defaultMph;
    for (const SimRoute &sr : opts.routes)
      if (strcmp(sr.routeId, r.id) == 0) perHour = sr.perHour, mph = sr.mph;
    if (perHour <= 0 || mph <= 0) continue;  // route switched off
    float speed = mph * 0.44704f;
    // Vehicles leave every 60/perHour minutes, so they are this far apart.
    float spacing = speed * 3600.0f / perHour;
    for (int pk = 0; pk < r.pathCount; pk++) {
      int path = r.firstPath + pk;
      float len = pathLength(path);
      if (map.paths[path].count < 2 || len <= 0) continue;
      // A route with one path is driven both ways; each extra path is one-way.
      int ways = r.pathCount == 1 ? 2 : 1;
      for (int w = 0; w < ways; w++) {
        int count = int(len / spacing + 0.5f);
        if (count < 1) count = 1;
        for (int c = 0; c < count && vehicles.size() < size_t(MAX_VEHICLES); c++) {
          SimVehicle v;
          v.route = uint8_t(ri);
          v.path = uint8_t(path);
          v.reverse = w == 1;
          int16_t pdir = map.paths[path].dir;
          v.dir = r.pathCount == 1 ? int16_t(w) : pdir;
          if (opts.someWithoutDirection && n % 4 == 3) v.dir = -1;
          v.lengthM = len;
          v.speedMps = speed;
          v.offsetM = len * (float(c) + 0.5f * w + 0.25f * float(pk)) / float(count);
          snprintf(v.id, sizeof v.id, "SIM-%.8s-%d%c%d", r.id, pk & 7, w ? 'r' : 'f', c % 1000);
          vehicles.push_back(v);
          n++;
        }
      }
    }
  }
  return n;
}

void SimSource::sample(uint32_t nowMs, uint64_t nowS, Feed &out) {
  out.t = nowS ? nowS : uint64_t(nowMs / 1000 + 1);
  out.clearCounts();
  double elapsed = double(opts.timeScale) * double(nowMs) / 1000.0;
  for (size_t k = 0; k < vehicles.size() && out.count < MAX_VEHICLES; k++) {
    const SimVehicle &sv = vehicles[k];
    // double here: after a few days of uptime the distance outgrows float precision.
    float s = float(fmod(double(sv.offsetM) + double(sv.speedMps) * elapsed, double(sv.lengthM)));
    if (sv.reverse) s = sv.lengthM - s;
    float x, y, heading;
    pointAt(sv.path, s, x, y, heading);
    if (sv.reverse) heading = fmodf(heading + 180.0f, 360.0f);
    // GPS noise across the direction of travel.
    float j = opts.jitterM * noise(uint32_t(k), nowMs / 1000);
    float hr = heading * float(M_PI) / 180.0f;
    x += j * cosf(hr);
    y -= j * sinf(hr);

    Vehicle &v = out.v[out.count++];
    memset(&v, 0, sizeof v);
    strcpy(v.id, sv.id);
    strncpy(v.routeId, map.routes[sv.route].id, sizeof v.routeId - 1);
    strcpy(v.label, "SIM");
    v.dir = sv.dir;
    v.hasPos = true;
    map.unproject(x, y, v.lat, v.lon);
    v.hasBearing = true;
    v.bearing = heading;
    v.ts = out.t;
  }
}

int SimSource::pathOf(const char *vehicleId) const {
  for (const SimVehicle &v : vehicles)
    if (strcmp(v.id, vehicleId) == 0) return v.path;
  return -1;
}
