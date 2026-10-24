// Austin Live Transit Map: ESP32 firmware for Live_Map_4layers rev A.
//
// Core 0: Wi-Fi and the network task (fetch, decode, match; runner.cpp).
// Core 1: this loop: LEDs at 50 Hz, buttons, web page, hotspot.
// Design: firmware-design/FIRMWARE_DESIGN.md, as amended in FIRMWARE_PLAN.md.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "board.h"
#include "buttons.h"
#include "display.h"
#include "ledmap.h"
#include "leds.h"
#include "runner.h"
#include "settings.h"
#include "version.h"
#include "web.h"
#include "wifi_link.h"

namespace {

const char *TZ_AUSTIN = "CST6CDT,M3.2.0,M11.1.0";
const uint32_t FRAME_MS = 20;

LedMap ledmap;
Display display(ledmap);
Settings cfg;  // loop-task copy, refreshed when settings change
volatile bool cfgDirty = false;
Occupancy occ;
uint32_t occSeq = 0;
Rgb frame[board::NUM_LEDS];

String loadMap() {
  File f = LittleFS.open("/ledmap.json", "r");
  if (!f) return "no /ledmap.json on the board; upload one on the web page";
  JsonDocument filter, doc;
  LedMap::makeFilter(filter);
  DeserializationError e = deserializeJson(doc, f, DeserializationOption::Filter(filter));
  f.close();
  if (e) return String("ledmap.json is not valid JSON: ") + e.c_str();
  std::string err;
  if (!ledmap.load(doc.as<JsonVariantConst>(), err)) return err.c_str();
  return "";
}

// Brand colour from the map, unless overridden on the web page.
void applyColors() {
  for (MapRoute &r : ledmap.routes) {
    r.color = r.brand;
    for (const ColorOverride &c : cfg.colors)
      if (c.routeId == r.id) r.color = c.color;
  }
}

bool isNight() {
  if (!cfg.nightEnabled) return false;
  struct tm tm;
  if (!getLocalTime(&tm, 0)) return false;  // clock not set yet: no dimming
  uint16_t m = uint16_t(tm.tm_hour * 60 + tm.tm_min);
  if (cfg.nightStart <= cfg.nightEnd) return m >= cfg.nightStart && m < cfg.nightEnd;
  return m >= cfg.nightStart || m < cfg.nightEnd;  // window crosses midnight
}

uint8_t brightness() {
  uint8_t b = cfg.brightness[cfg.level];
  if (isNight()) b = uint8_t(max<int>(Settings::MIN_BRIGHTNESS, b * cfg.nightPct / 100));
  return b;
}

void onSettingsChanged() { cfgDirty = true; }

void refreshSettings() {
  cfg = settings::get();
  applyColors();
  display.chainStepMs = cfg.chainStepMs;
  display.lineView = cfg.lineView;
  display.setOccupancy(occ);  // recolour what's showing now
  leds::setPowerLimit(cfg.maxMa);
}

void factoryReset() {
  log_w("factory reset");
  for (Rgb &c : frame) c = Rgb(255, 0, 0);
  leds::show(frame, 32);
  settings::factoryReset();
  wifilink::forgetNetwork();
  delay(1000);
  ESP.restart();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.printf("\nAustin Live Transit Map %s\n", FIRMWARE_VERSION);

  if (!LittleFS.begin(true)) Serial.println("LittleFS failed to mount");
  settings::begin();
  cfg = settings::get();

  String mapErr = loadMap();
  if (mapErr.isEmpty()) {
    Serial.printf("LED map: %u routes\n", unsigned(ledmap.routes.size()));
    for (const MapRoute &r : ledmap.routes)
      Serial.printf("  %-4s %-24s %s  %u/%u LEDs placed\n", r.id, r.name, board::CHAINS[r.chain].name, r.placed, r.total);
    if (!ledmap.notes.empty()) Serial.printf("  map note: %s\n", ledmap.notes.c_str());
  } else {
    Serial.printf("LED map not loaded: %s\n", mapErr.c_str());
  }
  applyColors();
  display.chainStepMs = cfg.chainStepMs;
  display.lineView = cfg.lineView;

  leds::begin(cfg.maxMa);
  buttons::begin();
  wifilink::begin();
  configTzTime(TZ_AUSTIN, "pool.ntp.org", "time.nist.gov");
  runner::begin(ledmap);
  web::begin(ledmap, mapErr, onSettingsChanged);
}

void loop() {
  static uint32_t lastFrame = 0;
  wifilink::loop();
  web::loop();
  if (cfgDirty) {
    cfgDirty = false;
    refreshSettings();
  }

  switch (buttons::poll()) {
    case ButtonEvent::ModeShort: {
      Settings s = settings::get();
      // Six steps: Low, Medium, High, then the same three in line view.
      if (s.level < 2) s.level++;
      else {
        s.level = 0;
        s.lineView = !s.lineView;
      }
      settings::set(s);
      refreshSettings();
      Serial.printf("brightness level %u (%u/255), %s\n", s.level, s.brightness[s.level], s.lineView ? "whole line" : "vehicles only");
      break;
    }
    case ButtonEvent::ModeLong:
      wifilink::toggleHotspot();
      break;
    case ButtonEvent::BootLong:
      factoryReset();
      break;
    default:
      break;
  }

  uint32_t now = millis();
  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    if (runner::takeOccupancy(occSeq, occ)) display.setOccupancy(occ);
    uint8_t b = brightness();
    if (cfg.lineView && cfg.mode != DisplayMode::ChainTest) {
      // Vehicles go out brighter; the line is scaled back to the preset.
      uint8_t boosted = uint8_t(min<uint32_t>(255, (uint32_t(b) * cfg.lineVehiclePct + 50) / 100));
      display.lineLevel = boosted ? float(b) / float(boosted) : 1.0f;
      b = boosted;
    }
    display.render(now, cfg.mode, frame);
    leds::show(frame, b);
  }
  delay(1);
}
