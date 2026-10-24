#include "ledmap.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

namespace {

const double DEFAULT_LAT0 = 30.2672, DEFAULT_LON0 = -97.7431;

void copyStr(char *dst, size_t cap, const char *src) {
  if (!src) src = "";
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

bool containsNoCase(const char *hay, const char *needle) {
  size_t n = strlen(needle);
  for (; *hay; hay++) {
    size_t k = 0;
    while (k < n && hay[k] && tolower((unsigned char)hay[k]) == needle[k]) k++;
    if (k == n) return true;
  }
  return false;
}

// A number, or a string holding one. Returns false for null/missing.
bool numberOf(JsonVariantConst v, double &out) {
  if (v.is<double>()) { out = v.as<double>(); return true; }
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    char *end = nullptr;
    out = strtod(s, &end);
    return end != s && *end == '\0';
  }
  return false;
}

// The position this route uses for one LED (its shared entry, else its own).
struct Entry {
  bool found = false, placed = false, skip = false;
  double lat = 0, lon = 0;
};

Entry readEntry(JsonVariantConst e) {
  Entry out;
  out.found = true;
  double la, lo;
  if (numberOf(e["lat"], la) && numberOf(e["lon"], lo)) {
    out.placed = true;
    out.lat = la;
    out.lon = lo;
  }
  out.skip = e["skip"].as<bool>();
  return out;
}

Entry findEntry(JsonVariantConst route, const char *ref) {
  for (JsonVariantConst s : route["shared"].as<JsonArrayConst>())
    if (strcmp(s["ref"] | "", ref) == 0) return readEntry(s);
  for (JsonVariantConst l : route["leds"].as<JsonArrayConst>())
    if (strcmp(l["ref"] | "", ref) == 0) return readEntry(l);
  return Entry();
}

}  // namespace

void LedMap::makeFilter(JsonDocument &f) {
  f["projection_origin"] = true;
  f["max_snap_m"] = true;
  f["snap_fraction"] = true;
  f["notes"] = true;
  JsonObject r = f["routes"].add<JsonObject>();
  for (const char *k : {"route_id", "name", "chain", "gpio", "count", "color"}) r[k] = true;
  JsonObject l = r["leds"].add<JsonObject>();
  for (const char *k : {"i", "ref", "lat", "lon", "skip"}) l[k] = true;
  JsonObject s = r["shared"].add<JsonObject>();
  for (const char *k : {"i", "ref", "chain", "lat", "lon", "skip"}) s[k] = true;
  JsonObject p = r["paths"].add<JsonObject>();
  for (const char *k : {"direction_id", "name", "seq"}) p[k] = true;
}

void LedMap::clear() {
  routes.clear();
  paths.clear();
  points.clear();
  notes.clear();
  for (int i = 0; i < board::NUM_LEDS; i++) {
    owner[i] = -1;
    refs[i][0] = '\0';
  }
}

int LedMap::routeIndex(const char *routeId) const {
  for (size_t k = 0; k < routes.size(); k++)
    if (strcmp(routes[k].id, routeId) == 0) return int(k);
  return -1;
}

int LedMap::ledByRef(const char *ref) const {
  for (int i = 0; i < board::NUM_LEDS; i++)
    if (refs[i][0] && strcmp(refs[i], ref) == 0) return i;
  return -1;
}

void LedMap::project(float lat, float lon, float &x, float &y) const {
  x = (lon - lon0) * kx;
  y = (lat - lat0) * ky;
}

void LedMap::unproject(float x, float y, float &lat, float &lon) const {
  lat = y / ky + lat0;
  lon = x / kx + lon0;
}

bool LedMap::load(JsonVariantConst doc, std::string &err) {
  clear();
  std::string bad;
  auto fail = [&](const std::string &msg) {
    if (!bad.empty()) bad += "; ";
    bad += msg;
  };

  JsonArrayConst jroutes = doc["routes"].as<JsonArrayConst>();
  if (jroutes.isNull()) {
    err = "not a ledmap.json (no \"routes\" list)";
    return false;
  }
  if (jroutes.size() > 32) {
    err = "too many routes";
    return false;
  }

  JsonArrayConst origin = doc["projection_origin"].as<JsonArrayConst>();
  double o0, o1;
  if (origin.size() == 2 && numberOf(origin[0], o0) && numberOf(origin[1], o1)) {
    lat0 = float(o0);
    lon0 = float(o1);
  } else {
    lat0 = float(DEFAULT_LAT0);
    lon0 = float(DEFAULT_LON0);
  }
  kx = cosf(lat0 * float(M_PI) / 180.0f) * 111320.0f;
  ky = 110540.0f;
  double ms = 0;
  maxSnapM = numberOf(doc["max_snap_m"], ms) && ms > 0 ? float(ms) : 400.0f;
  double sf = 0;
  snapFraction = numberOf(doc["snap_fraction"], sf) && sf >= 0 && sf <= 1 ? float(sf) : 0.3f;
  notes = doc["notes"] | "";

  // Pass 1: physical LEDs, chain checks, route table.
  bool chainUsed[board::NUM_CHAINS] = {};
  for (JsonVariantConst jr : jroutes) {
    MapRoute r = {};
    const char *rid = jr["route_id"] | "";
    copyStr(r.id, sizeof r.id, rid);
    copyStr(r.name, sizeof r.name, jr["name"] | "");
    if (!*rid) { fail("a route has no route_id"); continue; }
    if (routeIndex(rid) >= 0) { fail(std::string("route ") + rid + " is listed twice"); continue; }

    const char *chain = jr["chain"] | "";
    int c = -1;
    for (int k = 0; k < board::NUM_CHAINS; k++)
      if (strcmp(board::CHAINS[k].name, chain) == 0) c = k;
    if (c < 0) { fail(std::string(rid) + ": unknown chain \"" + chain + "\""); continue; }
    if (chainUsed[c]) { fail(std::string(rid) + ": chain " + chain + " is used by two routes"); continue; }
    chainUsed[c] = true;
    const board::Chain &bc = board::CHAINS[c];
    r.chain = uint8_t(c);
    if (!jr["gpio"].isNull() && jr["gpio"].as<int>() != bc.gpio)
      fail(std::string(rid) + ": gpio " + std::to_string(jr["gpio"].as<int>()) + " but " + chain + " is IO" + std::to_string(bc.gpio));
    JsonArrayConst leds = jr["leds"].as<JsonArrayConst>();
    if (leds.isNull()) { fail(std::string(rid) + ": no \"leds\" list"); continue; }
    if ((!jr["count"].isNull() && jr["count"].as<int>() != bc.count) || leds.size() != bc.count)
      fail(std::string(rid) + ": " + chain + " has " + std::to_string(bc.count) + " LEDs on the board, the map has " +
           std::to_string(jr["count"].isNull() ? leds.size() : jr["count"].as<int>()));

    r.brand = Rgb(255, 255, 255);
    parseHexColor(jr["color"] | "", r.brand);
    r.color = r.brand;

    int ri = int(routes.size());
    for (JsonVariantConst l : leds) {
      const char *ref = l["ref"] | "";
      int i = l["i"] | -1;
      if (!*ref || i < 0 || i >= bc.count) { fail(std::string(rid) + ": LED \"" + ref + "\" has a bad index"); continue; }
      if (strlen(ref) >= REF_LEN) { fail(std::string(rid) + ": LED name \"" + ref + "\" is too long"); continue; }
      int led = bc.offset + i;
      if (owner[led] >= 0) { fail(std::string(rid) + ": " + chain + "[" + std::to_string(i) + "] is listed twice"); continue; }
      if (ledByRef(ref) >= 0) { fail(std::string("LED ") + ref + " is listed twice"); continue; }
      owner[led] = int8_t(ri);
      strcpy(refs[led], ref);
    }
    routes.push_back(r);
  }
  if (!bad.empty()) {
    err = bad;
    clear();
    return false;
  }

  // Pass 2: shared LEDs and paths.
  for (size_t ri = 0; ri < routes.size(); ri++) {
    JsonVariantConst jr = jroutes[ri];
    MapRoute &r = routes[ri];
    for (JsonVariantConst s : jr["shared"].as<JsonArrayConst>()) {
      const char *ref = s["ref"] | "";
      int led = ledByRef(ref);
      if (led < 0) { fail(std::string(r.id) + " shares " + ref + ", which is not on the board"); continue; }
      const board::Chain &oc = board::CHAINS[routes[owner[led]].chain];
      int i = s["i"] | -1;
      if (strcmp(s["chain"] | "", oc.name) != 0 || i != led - oc.offset)
        fail(std::string(r.id) + " shares " + ref + " as " + (s["chain"] | "?") + "[" + std::to_string(i) + "], but it is " +
             oc.name + "[" + std::to_string(led - oc.offset) + "]");
    }

    // Without explicit paths the chain order serves both directions.
    std::vector<std::string> ownSeq;
    JsonArrayConst jpaths = jr["paths"].as<JsonArrayConst>();
    bool explicitPaths = !jpaths.isNull() && jpaths.size() > 0;
    if (!explicitPaths) {
      const board::Chain &bc = board::CHAINS[r.chain];
      for (int i = 0; i < bc.count; i++) ownSeq.push_back(refs[bc.offset + i]);
    }
    int npaths = explicitPaths ? int(jpaths.size()) : 1;
    if (npaths > 8) { fail(std::string(r.id) + ": too many paths"); continue; }

    r.firstPath = uint8_t(paths.size());
    r.pathCount = uint8_t(npaths);
    for (auto &w : r.inAllPaths) w = 0xFFFFFFFFu;
    uint32_t named[(board::NUM_LEDS + 31) / 32] = {};
    uint32_t placedBits[(board::NUM_LEDS + 31) / 32] = {};

    for (int pk = 0; pk < npaths; pk++) {
      MapPath p = {};
      std::vector<const char *> seq;
      if (explicitPaths) {
        JsonVariantConst jp = jpaths[pk];
        double d;
        p.dir = numberOf(jp["direction_id"], d) ? int16_t(d) : -1;
        copyStr(p.name, sizeof p.name, jp["name"] | "");
        for (JsonVariantConst s : jp["seq"].as<JsonArrayConst>()) seq.push_back(s | "");
      } else {
        p.dir = -1;
        copyStr(p.name, sizeof p.name, "Both");
        for (auto &s : ownSeq) seq.push_back(s.c_str());
      }
      p.north = containsNoCase(p.name, "north");
      p.south = containsNoCase(p.name, "south");
      p.first = uint16_t(points.size());

      uint32_t inThis[(board::NUM_LEDS + 31) / 32] = {};
      for (const char *ref : seq) {
        int led = ledByRef(ref);
        Entry e = led >= 0 ? findEntry(jr, ref) : Entry();
        // An LED this route may name: its own, or one in its shared list.
        bool own = led >= 0 && owner[led] == int(ri);
        bool sharedHere = false;
        for (JsonVariantConst s : jr["shared"].as<JsonArrayConst>())
          if (strcmp(s["ref"] | "", ref) == 0) sharedHere = true;
        if (!e.found || !(own || sharedHere)) {
          fail(std::string(r.id) + " path \"" + p.name + "\" names " + ref + ", which is neither its own LED nor in its shared list");
          continue;
        }
        inThis[led >> 5] |= 1u << (led & 31);
        named[led >> 5] |= 1u << (led & 31);
        if (!e.placed) continue;
        placedBits[led >> 5] |= 1u << (led & 31);
        if (e.skip) continue;
        MapPoint pt;
        pt.led = uint16_t(led);
        project(float(e.lat), float(e.lon), pt.x, pt.y);
        points.push_back(pt);
      }
      for (size_t w = 0; w < sizeof inThis / sizeof inThis[0]; w++) r.inAllPaths[w] &= inThis[w];
      p.count = uint16_t(points.size() - p.first);
      paths.push_back(p);
    }
    r.placed = r.total = 0;
    for (int led = 0; led < board::NUM_LEDS; led++) {
      if (!(named[led >> 5] & (1u << (led & 31)))) continue;
      r.total++;
      if (placedBits[led >> 5] & (1u << (led & 31))) r.placed++;
    }
  }

  if (!bad.empty()) {
    err = bad;
    clear();
    return false;
  }
  return true;
}
