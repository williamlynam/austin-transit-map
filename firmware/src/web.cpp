#include "web.h"

#include <memory>
#include <new>

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "gh_update.h"
#include "runner.h"
#include "settings.h"
#include "version.h"
#include "web_page.h"
#include "wifi_link.h"

namespace {

WebServer server(80);
LedMap *gmap = nullptr;
String mapErr;
void (*settingsChanged)() = nullptr;

bool restartPending = false;
uint32_t hotspotOffAt = 0;  // turn the hotspot off once the reply has gone out
uint32_t restartAt = 0;
String uploadResult;
bool uploadOk = false;
File uploadFile;

void scheduleRestart() {
  restartPending = true;
  restartAt = millis() + 1500;
}

void sendJson(JsonDocument &doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

// Requests from the hotspot for other hosts (phones probing for a captive
// portal) get sent to the setup steps, or to the settings page once setup is
// done (a board used without Wi-Fi is set up from the hotspot every time).
bool captiveRedirect() {
  if (!wifilink::hotspotOn()) return false;
  String host = server.hostHeader();
  IPAddress ip;
  if (ip.fromString(host) || host.startsWith("transitmap")) return false;
  const char *page = settings::get().setupDone ? "/" : "/setup";
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + page, true);
  server.send(302, "text/plain", "");
  return true;
}

void handleRoot() {
  if (captiveRedirect()) return;
  server.send_P(200, "text/html", WEB_PAGE);
}

void handleSetup() { server.send_P(200, "text/html", SETUP_PAGE); }

void handleStatus() {
  Status s = runner::status();
  JsonDocument doc;
  doc["version"] = FIRMWARE_VERSION;
  doc["uptime_s"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  doc["heap_max"] = ESP.getMaxAllocHeap();
  doc["max_block"] = ESP.getMaxAllocHeap();
  time_t now = time(nullptr);
  if (now > 1700000000) {
    char buf[32];
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S %Z", &tm);
    doc["time"] = buf;
  }
  JsonObject w = doc["wifi"].to<JsonObject>();
  w["connected"] = wifilink::connected();
  w["ssid"] = WiFi.SSID();
  w["saved"] = wifilink::ssid();
  w["rssi"] = WiFi.RSSI();
  w["ip"] = WiFi.localIP().toString();
  w["hotspot"] = wifilink::hotspotOn();
  w["hotspot_name"] = wifilink::hotspotName();
  w["local_name"] = wifilink::localName();
  w["hotspot_locked"] = wifilink::hotspotLocked();
  w["setup_done"] = settings::get().setupDone;

  JsonObject f = doc["feed"].to<JsonObject>();
  f["ok"] = s.feedOk;
  f["error"] = s.lastError;
  f["http"] = s.httpCode;
  f["t"] = s.feedT;
  if (s.lastChangeMs) f["since_change_ms"] = millis() - s.lastChangeMs;
  f["fetch_ms"] = s.fetchMs;
  f["bytes"] = s.bytes;
  f["vehicles"] = s.vehicles;
  f["other_routes"] = s.otherRoutes;
  f["dropped"] = s.dropped;
  f["no_route_id"] = s.noRouteId;
  JsonArray oi = f["other_route_ids"].to<JsonArray>();
  for (int k = 0; k < s.otherIdCount; k++) oi.add(s.otherIds[k]);
  f["next_poll_ms"] = s.nextPollInMs;

  JsonObject m = doc["map"].to<JsonObject>();
  m["error"] = mapErr;
  m["notes"] = gmap->notes.c_str();
  m["routes"] = gmap->routes.size();
  int placed = 0, total = 0;
  JsonArray rs = doc["routes"].to<JsonArray>();
  for (size_t k = 0; k < gmap->routes.size(); k++) {
    const MapRoute &r = gmap->routes[k];
    placed += r.placed;
    total += r.total;
    JsonObject o = rs.add<JsonObject>();
    char hex[8];
    o["id"] = r.id;
    o["name"] = r.name;
    formatHexColor(r.color, hex);
    o["color"] = hex;
    formatHexColor(r.brand, hex);
    o["brand"] = hex;
    o["placed"] = r.placed;
    o["total"] = r.total;
    if (k < 32) {
      o["lit"] = s.routes[k].lit;
      o["hidden"] = s.routes[k].hidden;
      o["off_route"] = s.routes[k].offRoute;
      o["stale"] = s.routes[k].stale;
      o["no_position"] = s.routes[k].noPosition;
    }
  }
  m["placed"] = placed;
  m["total"] = total;

  ghupdate::Info gi = ghupdate::info();
  JsonObject g = doc["github"].to<JsonObject>();
  g["state"] = ghupdate::stateName(gi.state);
  if (!gi.latest.isEmpty()) g["latest"] = gi.latest;
  g["prerelease"] = gi.prerelease;
  if (!gi.error.isEmpty()) g["error"] = gi.error;
  if (gi.checkedAtMs) g["checked_ago_ms"] = millis() - gi.checkedAtMs;
  if (gi.total) {
    g["done"] = gi.done;
    g["total"] = gi.total;
  }
  sendJson(doc);
}

void handleGhCheck() {
  ghupdate::requestCheck();
  server.send(200, "application/json", "{}");
}

void handleGhInstall() {
  ghupdate::requestInstall();
  server.send(200, "application/json", "{}");
}

void handleGetSettings() {
  JsonDocument doc;
  settings::toJson(settings::get(), doc.to<JsonObject>());
  sendJson(doc);
}

void handlePostSettings() {
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain"))) {
    server.send(400, "text/plain", "Not JSON");
    return;
  }
  Settings s = settings::get();
  String err;
  if (!settings::fromJson(in.as<JsonObjectConst>(), s, err)) {
    server.send(400, "text/plain", err);
    return;
  }
  settings::set(s);
  if (settingsChanged) settingsChanged();
  handleGetSettings();
}

void handleScan() {
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    bool dup = false;
    for (JsonObject o : a)
      if (WiFi.SSID(i) == o["ssid"].as<const char *>()) dup = true;
    if (dup || WiFi.SSID(i).isEmpty() || WiFi.SSID(i) == wifilink::hotspotName()) continue;
    JsonObject o = a.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  sendJson(doc);
}

void markSetupDone() {
  Settings s = settings::get();
  if (s.setupDone) return;
  s.setupDone = true;
  settings::set(s);
}

// "Finish setup": remember setup is done, then turn the hotspot off a moment
// later so the reply reaches a phone on the hotspot.
void handleCompleteSetup() {
  if (wifilink::connected()) markSetupDone();
  if (wifilink::hotspotLocked()) {
    // Debug build: settings were saved by the page; the hotspot stays on.
    JsonDocument doc;
    doc["ok"] = true;
    doc["hotspot_locked"] = true;
    doc["url"] = "http://" + wifilink::localName() + "/ (or http://" + WiFi.localIP().toString() + "/)";
    sendJson(doc);
    return;
  }
  if (!wifilink::connected()) {
    server.send(409, "text/plain", "The board isn't on Wi-Fi yet, so the hotspot stays on. Join a network first.");
    return;
  }
  JsonDocument doc;
  doc["ok"] = true;
  doc["url"] = "http://" + wifilink::localName() + "/ (or http://" + WiFi.localIP().toString() + "/)";
  doc["hotspot_was_on"] = wifilink::hotspotOn();
  sendJson(doc);
  hotspotOffAt = millis() + 1500;
}

// The "Setup network" switch on the Wi-Fi screen.
void handleHotspot() {
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain")) || !in["on"].is<bool>()) {
    server.send(400, "text/plain", "Need on");
    return;
  }
  if (in["on"].as<bool>()) {
    wifilink::hotspotOnByHand();
    server.send(200, "application/json", "{}");
    return;
  }
  if (wifilink::hotspotLocked()) {
    server.send(409, "text/plain", "This debug build keeps the setup network on.");
    return;
  }
  if (!wifilink::connected()) {
    server.send(409, "text/plain", "The setup network stays on while the map isn't on Wi-Fi.");
    return;
  }
  server.send(200, "application/json", "{}");
  hotspotOffAt = millis() + 1500;  // after the reply reaches a phone on the hotspot
}

void handleWifi() {
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain")) || !in["ssid"].is<const char *>()) {
    server.send(400, "text/plain", "Need ssid");
    return;
  }
  server.send(200, "application/json", "{}");
  wifilink::setNetwork(in["ssid"].as<String>(), in["pass"] | "");
}

// --- uploads ---

void mapUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadOk = false;
    uploadResult = "";
    uploadFile = LittleFS.open("/ledmap.new", "w");
    if (!uploadFile) uploadResult = "Couldn't write to the board's storage";
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && uploadFile.write(up.buf, up.currentSize) != up.currentSize) {
      uploadFile.close();
      uploadResult = "Not enough room on the board for this map file";
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    if (!uploadResult.isEmpty()) return;
    // Validate before replacing the working map. The test map goes on the
    // heap: it is about 5 KB, and on the 8 KB loop stack it overflowed and
    // reset the board mid-request ("Failed to fetch" in the browser).
    File f = LittleFS.open("/ledmap.new", "r");
    JsonDocument filter, doc;
    LedMap::makeFilter(filter);
    DeserializationError e = deserializeJson(doc, f, DeserializationOption::Filter(filter));
    f.close();
    if (e) {
      uploadResult = String("Not valid JSON: ") + e.c_str();
      return;
    }
    std::unique_ptr<LedMap> test(new (std::nothrow) LedMap);
    if (!test) {
      uploadResult = "Not enough memory to check the map";
      return;
    }
    std::string err;
    if (!test->load(doc.as<JsonVariantConst>(), err)) {
      uploadResult = String("Map rejected: ") + err.c_str();
      return;
    }
    LittleFS.remove("/ledmap.json");
    LittleFS.rename("/ledmap.new", "/ledmap.json");
    uploadOk = true;
    uploadResult = "Map saved. Restarting…";
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    uploadResult = "Upload aborted";
  }
}

void mapUploadDone() {
  LittleFS.remove("/ledmap.new");
  server.send(uploadOk ? 200 : 400, "text/plain", uploadResult.isEmpty() ? "No file" : uploadResult);
  if (uploadOk) scheduleRestart();
}

void fwUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadOk = false;
    uploadResult = "";
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) uploadResult = Update.errorString();
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadResult.isEmpty() && Update.write(up.buf, up.currentSize) != up.currentSize) uploadResult = Update.errorString();
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadResult.isEmpty() && Update.end(true)) {
      uploadOk = true;
      uploadResult = "Firmware updated. Restarting…";
    } else if (uploadResult.isEmpty()) {
      uploadResult = Update.errorString();
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    uploadResult = "Upload aborted";
  }
}

void fwUploadDone() {
  server.send(uploadOk ? 200 : 400, "text/plain", uploadOk ? uploadResult : "Update failed: " + uploadResult);
  if (uploadOk) scheduleRestart();
}

void handleMapDownload() {
  File f = LittleFS.open("/ledmap.json", "r");
  if (!f) {
    server.send(404, "text/plain", "No map on the board");
    return;
  }
  server.streamFile(f, "application/json");
  f.close();
}

void handleNotFound() {
  if (captiveRedirect()) return;
  server.send(404, "text/plain", "Not found");
}

}  // namespace

void web::begin(LedMap &map, const String &mapError, void (*onSettings)()) {
  gmap = &map;
  mapErr = mapError;
  settingsChanged = onSettings;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/setup", HTTP_GET, handleSetup);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/wifi/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_POST, handleWifi);
  server.on("/api/complete-setup", HTTP_POST, handleCompleteSetup);
  server.on("/api/hotspot", HTTP_POST, handleHotspot);
  server.on("/api/ledmap", HTTP_POST, mapUploadDone, mapUpload);
  server.on("/api/update", HTTP_POST, fwUploadDone, fwUpload);
  server.on("/api/github/check", HTTP_POST, handleGhCheck);
  server.on("/api/github/install", HTTP_POST, handleGhInstall);
  server.on("/ledmap.json", HTTP_GET, handleMapDownload);
  server.onNotFound(handleNotFound);
  server.begin();
}

void web::loop() {
  server.handleClient();
  if (hotspotOffAt && int32_t(millis() - hotspotOffAt) >= 0) {
    hotspotOffAt = 0;
    if (!wifilink::hotspotOff()) Serial.println("hotspot off: Wi-Fi dropped, hotspot left on");
  }
  if (restartPending && int32_t(millis() - restartAt) >= 0) ESP.restart();
}
