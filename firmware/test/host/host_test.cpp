// PC test of the firmware's hardware-free core (lib/transit_core): the same
// files the ESP32 compiles, built with g++ by run_tests.py.
//
//   host_test <ledmap.json> <out_dir>
//
// Checks map loading and validation, the GTFS-rt encode/decode round trip, the
// simulated feed through the real decoder and matcher, the D10/D23 shared LEDs,
// the 837 loop, and the colour rules. It also writes the simulated frames and
// the firmware's answers so crosscheck.js can compare them with the live
// preview's engine.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <ArduinoJson.h>
#include <pb_decode.h>

#include "display.h"
#include "engine.h"
#include "gtfs.h"
#include "ledmap.h"
#include "sim.h"

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                         \
  do {                                           \
    checks++;                                    \
    if (!(cond)) {                               \
      failures++;                                \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                       \
      printf("\n");                              \
    }                                            \
  } while (0)

static std::string readFile(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static bool loadMap(const std::string &text, LedMap &map, std::string &err) {
  JsonDocument filter, doc;
  LedMap::makeFilter(filter);
  DeserializationError e = deserializeJson(doc, text, DeserializationOption::Filter(filter));
  if (e) {
    err = e.c_str();
    return false;
  }
  return map.load(doc.as<JsonVariantConst>(), err);
}

static const char *stateName(VState s) {
  switch (s) {
    case VState::Lit: return "lit";
    case VState::Hidden: return "hidden";
    case VState::Stale: return "stale";
    case VState::NoPosition: return "no position";
    default: return "off route";
  }
}

// Decode a feed through the pb path, exactly as the board does.
static bool roundTrip(const Feed &in, const LedMap &map, Feed &out) {
  static uint8_t buf[16384];
  size_t n = encodeFeedPb(in, buf, sizeof buf);
  if (!n) return false;
  pb_istream_t s = pb_istream_from_buffer(buf, n);
  std::string err;
  return decodeFeedPb(&s, map, 0, out, err) == DecodeResult::Ok;
}

static void testValidation(const std::string &text) {
  // A shared entry pointing at the wrong index must be refused.
  {
    JsonDocument doc;
    deserializeJson(doc, text);
    for (JsonObject r : doc["routes"].as<JsonArray>())
      for (JsonObject s : r["shared"].as<JsonArray>()) s["i"] = s["i"].as<int>() + 1;
    std::string bad, err;
    serializeJson(doc, bad);
    LedMap m;
    CHECK(!loadMap(bad, m, err), "map with a wrong shared index was accepted");
    CHECK(err.find("shares") != std::string::npos, "unexpected error text: %s", err.c_str());
  }
  // A route on the wrong GPIO must be refused.
  {
    JsonDocument doc;
    deserializeJson(doc, text);
    doc["routes"][0]["gpio"] = 4;
    std::string bad, err;
    serializeJson(doc, bad);
    LedMap m;
    CHECK(!loadMap(bad, m, err), "map with a wrong gpio was accepted");
  }
  // A path naming an LED that is neither own nor shared must be refused.
  {
    JsonDocument doc;
    deserializeJson(doc, text);
    for (JsonObject r : doc["routes"].as<JsonArray>())
      if (r["paths"].size()) {
        r["paths"][0]["seq"].add("D170");
        break;
      }
    std::string bad, err;
    serializeJson(doc, bad);
    LedMap m;
    CHECK(!loadMap(bad, m, err), "path naming a foreign LED was accepted");
  }
  // Garbage.
  {
    LedMap m;
    std::string err;
    CHECK(!loadMap("{\"hello\":1}", m, err), "non-map JSON accepted");
  }
}

static void testDisplay(const LedMap &map) {
  Display d(map);
  int r550 = map.routeIndex("550"), r837 = map.routeIndex("837"), r801 = map.routeIndex("801");
  int d10 = map.ledByRef("D10"), d23 = map.ledByRef("D23");
  if (r550 < 0 || r837 < 0 || r801 < 0 || d10 < 0 || d23 < 0) {
    CHECK(false, "test map lacks 550/837/801/D10/D23");
    return;
  }
  uint8_t both[] = {uint8_t(r837), uint8_t(r550)};
  CHECK(d.colorFor(uint16_t(d10), both, 2) == map.routes[r550].color, "train must be on top at D10");
  uint8_t bus[] = {uint8_t(r837)};
  CHECK(d.colorFor(uint16_t(d10), bus, 1) == map.routes[r837].color, "bus alone at D10 shows its own colour");
  uint8_t both23[] = {uint8_t(r550), uint8_t(r801)};
  CHECK(d.colorFor(uint16_t(d23), both23, 2) == map.routes[r550].color, "train must be on top at D23");
  uint8_t two[] = {uint8_t(r801), uint8_t(r801)};
  CHECK(d.colorFor(uint16_t(d23), two, 2) == map.routes[r801].color, "two buses of one route look like one");

  // Default: no fading, an LED shows its colour on the very first frame.
  Occupancy occ;
  occ.clear();
  occ.add(uint16_t(d10), uint8_t(r550));
  d.setOccupancy(occ);
  Rgb frame[board::NUM_LEDS];
  d.render(0, DisplayMode::Live, frame);
  CHECK(frame[d10] == map.routes[r550].color, "no fade: lit at once");
  occ.clear();
  d.setOccupancy(occ);
  d.render(20, DisplayMode::Live, frame);
  CHECK(frame[d10] == Rgb(), "no fade: dark at once");

  // Line view: the whole route in its colour, a vehicle as a white dot.
  d.lineView = true;
  occ.add(uint16_t(d23), uint8_t(r801));
  d.setOccupancy(occ);
  d.render(40, DisplayMode::Live, frame);
  CHECK(frame[d23] == Rgb(255, 255, 255), "line view: vehicle is white");
  CHECK(frame[d10] == map.routes[r550].color, "line view: empty LED shows its route colour");
  d.lineLevel = 0.5f;
  d.render(50, DisplayMode::Live, frame);
  CHECK(frame[d10] == scale(map.routes[r550].color, 0.5f) && frame[d23] == Rgb(255, 255, 255),
        "line view: line scaled back, vehicle stays full");
  d.lineLevel = 1.0f;
  d.lineView = false;
  occ.clear();
  d.setOccupancy(occ);
  d.render(60, DisplayMode::Live, frame);

  // With fadeMs set: a lit LED reaches full colour after fadeMs, and back to dark.
  Display d2(map);
  d2.fadeMs = 1500;
  occ.add(uint16_t(d10), uint8_t(r550));
  d2.setOccupancy(occ);
  d2.render(0, DisplayMode::Live, frame);
  d2.render(750, DisplayMode::Live, frame);
  CHECK(frame[d10].r > 0 && frame[d10].r < map.routes[r550].color.r, "half-way through a fade-in");
  d2.render(1600, DisplayMode::Live, frame);
  CHECK(frame[d10] == map.routes[r550].color, "fade-in complete");
  occ.clear();
  d2.setOccupancy(occ);
  d2.render(3200, DisplayMode::Live, frame);
  CHECK(frame[d10] == Rgb(), "fade-out complete");
}

// Long gaps between LEDs get a corridor of snap_fraction of their length,
// short ones keep max_snap_m (the Red Line NW trains that went off route).
static void testCorridor(const LedMap &map) {
  const MapRoute &r = map.routes[map.routeIndex("550")];
  const MapPath &path = map.paths[r.firstPath];
  const MapPoint *pts = &map.points[path.first];
  int longK = -1, shortK = -1;
  float longLen = 0;
  for (int k = 0; k + 1 < path.count; k++) {
    float len = hypotf(pts[k + 1].x - pts[k].x, pts[k + 1].y - pts[k].y);
    if (len > longLen) { longLen = len; longK = k; }
    if (len > 300 && len < 1000 && shortK < 0) shortK = k;
  }
  auto offset = [&](int k, float m, float &lat, float &lon) {
    const MapPoint &a = pts[k], &b = pts[k + 1];
    float dx = b.x - a.x, dy = b.y - a.y, len = hypotf(dx, dy);
    map.unproject((a.x + b.x) / 2 - dy / len * m, (a.y + b.y) / 2 + dx / len * m, lat, lon);
  };
  CHECK(longK >= 0 && longLen > 4000, "550 should have a gap over 4 km, longest %.0f m", double(longLen));
  if (longK < 0 || shortK < 0) return;
  float lat, lon;
  SnapHit hit;
  // A point beside the middle of the long gap, but farther than that from any other segment.
  float wide = map.snapFraction * longLen * 0.8f;
  offset(longK, wide, lat, lon);
  bool ok = snapToPath(map, path, lat, lon, hit);
  bool otherNear = false;
  for (int k = 0; k + 1 < path.count; k++) {
    if (k == longK) continue;
    float t, px, py;
    map.project(lat, lon, px, py);
    float dx = pts[k + 1].x - pts[k].x, dy = pts[k + 1].y - pts[k].y, l2 = dx * dx + dy * dy;
    t = fmaxf(0, fminf(1, ((px - pts[k].x) * dx + (py - pts[k].y) * dy) / l2));
    if (hypotf(px - pts[k].x - t * dx, py - pts[k].y - t * dy) < map.maxSnapM) otherNear = true;
  }
  CHECK(ok && (otherNear || hit.k == longK), "%.0f m off the %.0f m gap after %s should snap to it", double(wide),
        double(longLen), map.ref(pts[longK].led));
  offset(longK, map.snapFraction * longLen * 1.3f + map.maxSnapM, lat, lon);
  CHECK(!snapToPath(map, path, lat, lon, hit) || hit.k != longK, "far beyond the long gap's corridor should not snap to it");
  offset(shortK, map.maxSnapM + 60, lat, lon);
  CHECK(!snapToPath(map, path, lat, lon, hit) || hit.k != shortK, "a short gap keeps the %.0f m corridor", double(map.maxSnapM));
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s ledmap.json out_dir\n", argv[0]);
    return 2;
  }
  std::string text = readFile(argv[1]);
  std::string outDir = argv[2];
  LedMap map;
  std::string err;
  bool ok = loadMap(text, map, err);
  CHECK(ok, "map failed to load: %s", err.c_str());
  if (!ok) return 1;
  printf("map: %zu routes, %zu paths, %zu placed points\n", map.routes.size(), map.paths.size(), map.points.size());
  for (const MapRoute &r : map.routes) printf("  %-4s %-26s %s %u/%u placed\n", r.id, r.name, board::CHAINS[r.chain].name, r.placed, r.total);

  testValidation(text);
  testDisplay(map);
  testCorridor(map);

  // Shared LEDs resolve to their physical chain.
  int d10 = map.ledByRef("D10"), d23 = map.ledByRef("D23");
  CHECK(d10 == 9 && d23 == 22, "D10/D23 should be LINE1[9]/[22], got %d/%d", d10, d23);
  CHECK(map.ownerRoute(uint16_t(d10)) == map.routeIndex("550"), "D10 owned by 550");

  // --- simulated feed through the real decoder and matcher ---
  SimSource sim(map);
  sim.opts.someWithoutDirection = true;  // keep exercising the bearing fallback
  int nsim = sim.setup();
  printf("simulated vehicles: %d\n", nsim);
  CHECK(nsim > 10, "too few simulated vehicles");
  Engine engine(map);
  static Feed simFeed, decoded;
  static Placement place[MAX_VEHICLES];
  Occupancy occ;
  int lit = 0, hidden = 0, offRoute = 0, offRouteNoDir = 0, wrongPath = 0, onD10by837 = 0, onD23by801 = 0, loopHidden = 0;
  int i837 = map.routeIndex("837"), i801 = map.routeIndex("801");

  std::ofstream frames(outDir + "/frames.json");
  frames << "[\n";
  const int FRAMES = 400;
  for (int f = 0; f < FRAMES; f++) {
    uint32_t ms = uint32_t(f) * 2000;
    uint64_t now = 1791250000ULL + ms / 1000;
    sim.sample(ms, now, simFeed);
    CHECK(roundTrip(simFeed, map, decoded), "frame %d: GTFS-rt round trip failed", f);
    CHECK(decoded.count == simFeed.count, "frame %d: %u vehicles in, %u out", f, simFeed.count, decoded.count);
    for (int k = 0; k < decoded.count && k < simFeed.count; k++) {
      const Vehicle &a = simFeed.v[k], &b = decoded.v[k];
      CHECK(!strcmp(a.id, b.id) && !strcmp(a.routeId, b.routeId) && a.dir == b.dir && a.lat == b.lat && a.lon == b.lon &&
                a.hasBearing == b.hasBearing && a.bearing == b.bearing && a.ts == b.ts,
            "frame %d vehicle %d changed in the round trip", f, k);
    }
    engine.update(decoded, now, occ, place);

    frames << (f ? ",\n" : "") << "{\"t\":" << decoded.t << ",\"now\":" << now << ",\"vehicles\":[";
    for (int k = 0; k < decoded.count; k++) {
      const Vehicle &v = decoded.v[k];
      const Placement &p = place[k];
      char line[512];
      snprintf(line, sizeof line,
               "%s{\"id\":\"%s\",\"route_id\":\"%s\",\"dir\":%s,\"lat\":%.9g,\"lon\":%.9g,\"bearing\":%.9g,\"ts\":%llu,"
               "\"fw_state\":\"%s\",\"fw_ref\":%s%s%s,\"fw_d\":%.3f}",
               k ? "," : "", v.id, v.routeId, v.dir >= 0 ? std::to_string(v.dir).c_str() : "null", double(v.lat),
               double(v.lon), double(v.bearing), (unsigned long long)v.ts, stateName(p.state), p.led >= 0 ? "\"" : "",
               p.led >= 0 ? map.ref(uint16_t(p.led)) : "null", p.led >= 0 ? "\"" : "", double(p.distM));
      frames << line;

      if (p.state == VState::Lit) lit++;
      if (p.state == VState::Hidden) hidden++;
      // Without direction_id the bearing picks the path (§4.2: 270–90° is
      // Northbound). Heading west on the 837 loop's southbound side that picks
      // the wrong path, so those can fall off route; that's the design's
      // fallback, not a matching bug, and it's reported separately.
      if (p.state == VState::OffRoute) (v.dir >= 0 ? offRoute : offRouteNoDir)++;
      // A vehicle that reported its direction must light an LED on its own path.
      int simPath = sim.pathOf(v.id);
      if (p.state == VState::Lit && v.dir >= 0 && simPath >= 0) {
        const MapPath &mp = map.paths[simPath];
        bool on = false;
        for (int q = 0; q < mp.count; q++) on |= map.points[mp.first + q].led == p.led;
        if (!on) {
          wrongPath++;
          printf("  frame %d: %s lit %s, not on its path %s\n", f, v.id, map.ref(uint16_t(p.led)), mp.name);
        }
      }
      if (p.state == VState::Lit && p.route == i837 && p.led == d10) onD10by837++;
      if (p.state == VState::Lit && p.route == i801 && p.led == d23) onD23by801++;
      if (p.state == VState::Hidden && p.route == i837) loopHidden++;
      if (p.state == VState::Hidden) CHECK(!map.routes[p.route].inAll(uint16_t(p.led)), "hidden vehicle on a trunk LED");
    }
    frames << "]}";
  }
  frames << "\n]\n";
  frames.close();

  printf("simulated frames: %d lit, %d hidden (loop, no direction), %d off route with direction_id, %d off route on bearing fallback\n",
         lit, hidden, offRoute, offRouteNoDir);
  printf("  837 bus on D10: %d times, 801 bus on D23: %d times, 837 hidden on loop: %d\n", onD10by837, onD23by801, loopHidden);
  CHECK(wrongPath == 0, "%d vehicles lit an LED off their own direction's path", wrongPath);
  CHECK(offRoute == 0, "%d simulated vehicles with direction_id fell off route", offRoute);
  CHECK(onD10by837 > 0, "no 837 bus ever lit D10");
  CHECK(onD23by801 > 0, "no 801 bus ever lit D23");

  // --- random vehicles around the network (cross-checked against the preview) ---
  std::mt19937 rng(12345);
  std::uniform_real_distribution<float> U(0, 1);
  std::ofstream rnd(outDir + "/random.json");
  rnd << "[\n";
  Engine e2(map);
  e2.opts.hysteresis = false;  // each sample stands alone
  static Feed rf;
  for (int f = 0; f < 40; f++) {
    rf.t = 1791250000ULL;
    rf.count = 0;
    for (int k = 0; k < MAX_VEHICLES; k++) {
      Vehicle &v = rf.v[rf.count++];
      memset(&v, 0, sizeof v);
      const MapRoute &r = map.routes[size_t(U(rng) * map.routes.size()) % map.routes.size()];
      const MapPath &p = map.paths[r.firstPath + int(U(rng) * r.pathCount) % r.pathCount];
      if (p.count < 2) {
        rf.count--;
        continue;
      }
      int q = int(U(rng) * (p.count - 1));
      const MapPoint &a = map.points[p.first + q], &b = map.points[p.first + q + 1];
      float t = U(rng), x = a.x + t * (b.x - a.x) + (U(rng) - 0.5f) * 600, y = a.y + t * (b.y - a.y) + (U(rng) - 0.5f) * 600;
      snprintf(v.id, sizeof v.id, "R%d-%d", f % 1000, k % 1000);
      strcpy(v.routeId, r.id);
      v.hasPos = true;
      map.unproject(x, y, v.lat, v.lon);
      float u = U(rng);
      v.dir = u < 0.6f ? int16_t(U(rng) < 0.5f ? 0 : 1) : -1;
      v.hasBearing = U(rng) < 0.5f;
      v.bearing = v.hasBearing ? U(rng) * 360 : 0;
      v.ts = rf.t;
    }
    e2.update(rf, 0, occ, place);
    rnd << (f ? ",\n" : "") << "{\"t\":" << rf.t << ",\"now\":null,\"vehicles\":[";
    for (int k = 0; k < rf.count; k++) {
      const Vehicle &v = rf.v[k];
      const Placement &p = place[k];
      char line[512];
      snprintf(line, sizeof line,
               "%s{\"id\":\"%s\",\"route_id\":\"%s\",\"dir\":%s,\"lat\":%.9g,\"lon\":%.9g,\"bearing\":%s,\"ts\":%llu,"
               "\"fw_state\":\"%s\",\"fw_ref\":%s%s%s,\"fw_d\":%.3f}",
               k ? "," : "", v.id, v.routeId, v.dir >= 0 ? std::to_string(v.dir).c_str() : "null", double(v.lat),
               double(v.lon), v.hasBearing ? std::to_string(v.bearing).c_str() : "null", (unsigned long long)v.ts,
               stateName(p.state), p.led >= 0 ? "\"" : "", p.led >= 0 ? map.ref(uint16_t(p.led)) : "null",
               p.led >= 0 ? "\"" : "", double(p.distM));
      rnd << line;
    }
    rnd << "]}";
  }
  rnd << "\n]\n";
  rnd.close();

  // A real GTFS-rt file for the preview's own decoder to read.
  {
    static uint8_t buf[16384];
    sim.sample(60000, 1791250060ULL, simFeed);
    size_t n = encodeFeedPb(simFeed, buf, sizeof buf);
    std::ofstream pb(outDir + "/sim_feed.pb", std::ios::binary);
    pb.write(reinterpret_cast<const char *>(buf), std::streamsize(n));
  }

  // Unchanged-feed shortcut: same header timestamp stops after the header.
  {
    static uint8_t buf[16384];
    size_t n = encodeFeedPb(simFeed, buf, sizeof buf);
    pb_istream_t s = pb_istream_from_buffer(buf, n);
    std::string e;
    static Feed tmp;
    CHECK(decodeFeedPb(&s, map, simFeed.t, tmp, e) == DecodeResult::Unchanged, "unchanged feed not detected");
  }

  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
