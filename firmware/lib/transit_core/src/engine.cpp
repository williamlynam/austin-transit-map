#include "engine.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void Occupancy::clear() { memset(n, 0, sizeof n); }

void Occupancy::add(uint16_t led, uint8_t routeIndex) {
  if (led >= board::NUM_LEDS || n[led] >= MAX_PER_LED) return;
  route[led][n[led]++] = routeIndex;
}

// Closest point to p on segment a-b, all in metres (Geo.onSegment).
static float onSegment(float px, float py, const MapPoint &a, const MapPoint &b, float &t) {
  float dx = b.x - a.x, dy = b.y - a.y;
  float len2 = dx * dx + dy * dy;
  t = len2 ? ((px - a.x) * dx + (py - a.y) * dy) / len2 : 0;
  t = fmaxf(0.0f, fminf(1.0f, t));
  float qx = a.x + t * dx, qy = a.y + t * dy;
  return hypotf(px - qx, py - qy);
}

bool snapToPath(const LedMap &map, const MapPath &path, float lat, float lon, SnapHit &hit) {
  const MapPoint *pts = &map.points[path.first];
  int n = path.count;
  float px, py;
  map.project(lat, lon, px, py);
  bool found = false;
  if (n == 1) {
    hit = {int16_t(pts[0].led), hypotf(px - pts[0].x, py - pts[0].y), 0, 0};
    found = true;
  }
  if (n == 1) return hit.d <= map.maxSnapM;
  // Each segment's corridor is max_snap_m wide, or snap_fraction of its own
  // length when that is wider: real track between two LEDs kilometres apart
  // curves well away from the straight line joining them.
  for (int k = 0; k + 1 < n; k++) {
    float t;
    float d = onSegment(px, py, pts[k], pts[k + 1], t);
    float len = hypotf(pts[k + 1].x - pts[k].x, pts[k + 1].y - pts[k].y);
    if (d > fmaxf(map.maxSnapM, map.snapFraction * len)) continue;
    if (!found || d < hit.d) {
      hit = {int16_t(t < 0.5 ? pts[k].led : pts[k + 1].led), d, k, t};
      found = true;
    }
  }
  return found;
}

// §4.2: direction_id picks the path; without it, bearing (0–90° or 270–360° is
// Northbound); without either, -1 = try every path.
int Engine::choosePath(const MapRoute &r, const Vehicle &v, How &how) const {
  if (r.pathCount == 1) { how = How::SinglePath; return r.firstPath; }
  if (v.dir >= 0) {
    for (int k = 0; k < r.pathCount; k++)
      if (map.paths[r.firstPath + k].dir == v.dir) { how = How::DirectionId; return r.firstPath + k; }
  }
  if (v.hasBearing && isfinite(v.bearing)) {
    float b = fmodf(fmodf(v.bearing, 360.0f) + 360.0f, 360.0f);
    bool nb = b <= 90 || b >= 270;
    for (int k = 0; k < r.pathCount; k++) {
      const MapPath &p = map.paths[r.firstPath + k];
      if (nb ? p.north : p.south) { how = How::Bearing; return r.firstPath + k; }
    }
  }
  how = How::Unknown;
  return -1;
}

// §4.3 hysteresis: keep the previous LED unless the new projection is more
// than 0.6 of a segment away from it.
bool Engine::hold(const Vehicle &v, int route, int path, SnapHit &hit) const {
  if (!opts.hysteresis) return false;
  const Prev *p = nullptr;
  for (int k = 0; k < nprev; k++)
    if (strcmp(prev[k].id, v.id) == 0) { p = &prev[k]; break; }
  if (!p || p->route != route || p->path != path || p->led == hit.led) return false;
  const MapPath &mp = map.paths[path];
  int j = -1;
  for (int k = 0; k < mp.count; k++)
    if (map.points[mp.first + k].led == p->led) { j = k; break; }
  if (j < 0) return false;
  if (fabsf(hit.k + hit.t - j) > opts.hyst) return false;
  hit.led = p->led;
  return true;
}

void Engine::update(const Feed &feed, uint64_t nowS, Occupancy &out, Placement *place) {
  out.clear();
  Prev next[MAX_VEHICLES];
  int nnext = 0;
  int boardCount = 0;

  for (int vi = 0; vi < feed.count; vi++) {
    Vehicle v = feed.v[vi];
    Placement &pl = place[vi];
    pl = {VState::OffRoute, How::Unknown, -1, -1, -1, false, false, 0};
    int ri = map.routeIndex(v.routeId);
    if (ri < 0) continue;  // decoder already filtered; kept for safety
    boardCount++;
    if (!v.id[0]) snprintf(v.id, sizeof v.id, "%s?%d", v.routeId, boardCount);
    pl.route = int8_t(ri);
    const MapRoute &r = map.routes[ri];

    uint64_t ts = v.ts ? v.ts : feed.t;
    if (!v.hasPos || (v.lat == 0 && v.lon == 0)) { pl.state = VState::NoPosition; continue; }
    if (nowS && ts && nowS > ts && nowS - ts > opts.staleS) { pl.state = VState::Stale; continue; }

    How how;
    int path = choosePath(r, v, how);
    pl.how = how;
    SnapHit hit;
    bool ok = false, hidden = false;
    if (path >= 0) {
      ok = snapToPath(map, map.paths[path], v.lat, v.lon, hit);
    } else {
      for (int k = 0; k < r.pathCount; k++) {
        SnapHit h;
        if (snapToPath(map, map.paths[r.firstPath + k], v.lat, v.lon, h) && (!ok || h.d < hit.d)) {
          hit = h;
          path = r.firstPath + k;
          ok = true;
        }
      }
      // §4.2 fallback 3: only accept an LED that every path shares (not the loop).
      if (ok && !r.inAll(hit.led)) hidden = true;
    }
    if (!ok) { pl.state = VState::OffRoute; continue; }
    pl.path = int8_t(path);
    pl.distM = hit.d;
    if (hidden) { pl.state = VState::Hidden; pl.led = hit.led; continue; }

    pl.held = hold(v, ri, path, hit);
    pl.led = hit.led;
    pl.state = VState::Lit;
    pl.borrowed = map.ownerRoute(hit.led) != ri;

    if (nnext < MAX_VEHICLES) {
      Prev &n = next[nnext++];
      memcpy(n.id, v.id, sizeof n.id);
      n.route = int8_t(ri);
      n.path = int8_t(path);
      n.led = hit.led;
    }
    out.add(uint16_t(hit.led), uint8_t(ri));
  }
  memcpy(prev, next, sizeof(Prev) * nnext);
  nprev = nnext;
}
