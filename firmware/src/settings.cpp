#include "settings.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

namespace {
const char *PATH = "/config.json";
Settings current;
SemaphoreHandle_t mutex;

const char *modeName(DisplayMode m) {
  switch (m) {
    case DisplayMode::Simulated: return "simulated";
    case DisplayMode::ChainTest: return "chain_test";
    default: return "live";
  }
}

bool parseMode(const char *s, DisplayMode &m) {
  if (!strcmp(s, "live")) m = DisplayMode::Live;
  else if (!strcmp(s, "simulated")) m = DisplayMode::Simulated;
  else if (!strcmp(s, "chain_test")) m = DisplayMode::ChainTest;
  else return false;
  return true;
}

uint16_t hhmm(const char *s, uint16_t fallback) {
  int h, m;
  if (s && sscanf(s, "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) return uint16_t(h * 60 + m);
  return fallback;
}

void save(const Settings &s) {
  JsonDocument doc;
  settings::toJson(s, doc.to<JsonObject>());
  File f = LittleFS.open(PATH, "w");
  if (!f) {
    log_e("can't write %s", PATH);
    return;
  }
  serializeJsonPretty(doc, f);
  f.close();
}
}  // namespace

void Settings::clamp() {
  if (pollS < MIN_POLL_S) pollS = MIN_POLL_S;
  if (pollS > MAX_POLL_S) pollS = MAX_POLL_S;
  if (level > 2) level = 1;
  if (nightPct > 100) nightPct = 100;
  if (maxMa < 200) maxMa = 200;
  if (maxMa > 3000) maxMa = 3000;  // 3 A PTC on the board
  if (connectS < 5) connectS = 5;
  if (readS < 5) readS = 5;
  if (simTimeScale < 0.1f) simTimeScale = 0.1f;
  if (simTimeScale > 60.0f) simTimeScale = 60.0f;
  if (chainStepMs < 20) chainStepMs = 20;
  if (chainStepMs > 2000) chainStepMs = 2000;
  for (SimRoute &r : simRoutes) {
    r.perHour = constrain(r.perHour, 0.0f, 30.0f);
    r.mph = constrain(r.mph, 0.0f, 80.0f);
  }
  if (feedUrl.isEmpty()) feedUrl = DEFAULT_FEED_URL;
}

void settings::toJson(const Settings &s, JsonObject o) {
  o["mode"] = modeName(s.mode);
  JsonArray b = o["brightness"].to<JsonArray>();
  for (uint8_t v : s.brightness) b.add(v);
  o["level"] = s.level;
  o["line_view"] = s.lineView;
  o["line_vehicle_pct"] = s.lineVehiclePct;
  JsonObject n = o["night"].to<JsonObject>();
  n["enabled"] = s.nightEnabled;
  char t[12];
  snprintf(t, sizeof t, "%02u:%02u", s.nightStart / 60, s.nightStart % 60);
  n["start"] = t;
  snprintf(t, sizeof t, "%02u:%02u", s.nightEnd / 60, s.nightEnd % 60);
  n["end"] = t;
  n["percent"] = s.nightPct;
  o["max_ma"] = s.maxMa;
  JsonObject f = o["feed"].to<JsonObject>();
  f["url"] = s.feedUrl;
  f["poll_s"] = s.pollS;
  f["connect_s"] = s.connectS;
  f["read_s"] = s.readS;
  o["chain_ms"] = s.chainStepMs;
  JsonObject sim = o["sim"].to<JsonObject>();
  sim["time_scale"] = s.simTimeScale;
  JsonObject sr = sim["routes"].to<JsonObject>();
  for (const SimRoute &r : s.simRoutes) {
    JsonObject x = sr[r.routeId].to<JsonObject>();
    x["per_hour"] = r.perHour;
    x["mph"] = r.mph;
  }
  o["setup_done"] = s.setupDone;
  JsonObject c = o["colors"].to<JsonObject>();
  for (const ColorOverride &co : s.colors) {
    char hex[8];
    formatHexColor(co.color, hex);
    c[co.routeId.c_str()] = hex;
  }
}

bool settings::fromJson(JsonObjectConst o, Settings &s, String &err) {
  if (o["mode"].is<const char *>() && !parseMode(o["mode"], s.mode)) {
    err = "unknown mode";
    return false;
  }
  JsonArrayConst b = o["brightness"];
  if (b.size() == 3)
    for (int k = 0; k < 3; k++) s.brightness[k] = uint8_t(constrain(b[k].as<int>(), int(Settings::MIN_BRIGHTNESS), 255));
  if (o["level"].is<int>()) s.level = o["level"];
  if (o["line_view"].is<bool>()) s.lineView = o["line_view"];
  if (o["line_vehicle_pct"].is<int>()) s.lineVehiclePct = uint16_t(constrain(o["line_vehicle_pct"].as<int>(), 100, 400));
  JsonObjectConst n = o["night"];
  if (!n.isNull()) {
    if (n["enabled"].is<bool>()) s.nightEnabled = n["enabled"];
    s.nightStart = hhmm(n["start"], s.nightStart);
    s.nightEnd = hhmm(n["end"], s.nightEnd);
    if (n["percent"].is<int>()) s.nightPct = uint8_t(constrain(n["percent"].as<int>(), 0, 100));
  }
  if (o["max_ma"].is<int>()) s.maxMa = o["max_ma"];
  JsonObjectConst f = o["feed"];
  if (!f.isNull()) {
    if (f["url"].is<const char *>()) s.feedUrl = f["url"].as<const char *>();
    if (f["poll_s"].is<int>()) s.pollS = f["poll_s"];
    if (f["connect_s"].is<int>()) s.connectS = f["connect_s"];
    if (f["read_s"].is<int>()) s.readS = f["read_s"];
  }
  if (o["chain_ms"].is<int>()) s.chainStepMs = uint16_t(constrain(o["chain_ms"].as<int>(), 20, 2000));
  if (o["sim_time_scale"].is<float>()) s.simTimeScale = o["sim_time_scale"];  // older builds
  JsonObjectConst sim = o["sim"];
  if (!sim.isNull()) {
    if (sim["time_scale"].is<float>()) s.simTimeScale = sim["time_scale"];
    for (JsonPairConst kv : sim["routes"].as<JsonObjectConst>()) {
      SimRoute *r = nullptr;
      for (SimRoute &x : s.simRoutes)
        if (!strcmp(x.routeId, kv.key().c_str())) r = &x;
      if (!r) {
        if (strlen(kv.key().c_str()) >= sizeof r->routeId) continue;
        s.simRoutes.push_back({});
        r = &s.simRoutes.back();
        strcpy(r->routeId, kv.key().c_str());
        r->perHour = 4;
        r->mph = 15;
      }
      if (kv.value()["per_hour"].is<float>()) r->perHour = kv.value()["per_hour"];
      if (kv.value()["mph"].is<float>()) r->mph = kv.value()["mph"];
    }
  }
  if (o["setup_done"].is<bool>()) s.setupDone = o["setup_done"];
  JsonObjectConst c = o["colors"];
  if (!c.isNull()) {
    // The colours object replaces the old one; an empty value clears a route.
    s.colors.clear();
    for (JsonPairConst kv : c) {
      const char *hex = kv.value() | "";
      if (!*hex) continue;
      Rgb rgb;
      if (!parseHexColor(hex, rgb)) {
        err = String("bad colour for route ") + kv.key().c_str();
        return false;
      }
      s.colors.push_back({kv.key().c_str(), rgb});
    }
  }
  s.clamp();
  return true;
}

void settings::begin() {
  mutex = xSemaphoreCreateMutex();
  Settings s;
  File f = LittleFS.open(PATH, "r");
  if (f) {
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, f);
    f.close();
    String err;
    if (e) log_w("%s unreadable (%s), using defaults", PATH, e.c_str());
    else if (!fromJson(doc.as<JsonObjectConst>(), s, err)) log_w("%s: %s", PATH, err.c_str());
  }
  s.clamp();
  current = s;
}

Settings settings::get() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  Settings s = current;
  xSemaphoreGive(mutex);
  return s;
}

void settings::set(const Settings &s) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  current = s;
  xSemaphoreGive(mutex);
  save(s);
}

void settings::factoryReset() { LittleFS.remove(PATH); }
