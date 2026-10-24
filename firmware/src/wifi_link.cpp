#include "wifi_link.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>

#include "board.h"

namespace {

const uint32_t AUTO_HOTSPOT_AFTER_MS = 5UL * 60UL * 1000UL;

DNSServer dns;
String savedSsid, savedPass;
bool apOn = false;
bool mdnsUp = false;
uint32_t lastConnectedMs = 0;  // last time we were on Wi-Fi (or boot)

// The last 4 hex digits of the MAC tell boards apart: the hotspot is
// TransitMap-AB12 and the same board on Wi-Fi is transitmap-ab12.local, so
// several boards can share one network.
String macTail(const char *fmt) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char name[24];
  snprintf(name, sizeof name, fmt, mac[4], mac[5]);
  return name;
}

String apName() { return macTail("TransitMap-%02X%02X"); }
String hostName() { return macTail("transitmap-%02x%02x"); }

void startHotspot(bool byHand) {
  if (apOn) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName().c_str());  // open network, as agreed
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());  // captive portal: every name -> us
  apOn = true;
  Serial.printf("hotspot %s on (%s)\n", apName().c_str(), byHand ? "by hand" : "automatic");
}

void stopHotspot() {
  if (!apOn) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apOn = false;
  Serial.printf("hotspot off\n");
}

void joinSaved() {
  if (savedSsid.isEmpty()) return;
  WiFi.begin(savedSsid.c_str(), savedPass.c_str());
}

void updateLed() {
  bool on;
  if (apOn) on = (millis() / 500) % 2;  // blink, 1 Hz
  else on = WiFi.status() == WL_CONNECTED;
  digitalWrite(board::PIN_WIFI_LED, on ? HIGH : LOW);
}

}  // namespace

void wifilink::begin() {
  pinMode(board::PIN_WIFI_LED, OUTPUT);
  digitalWrite(board::PIN_WIFI_LED, LOW);
  Preferences p;
  p.begin("wifi", true);
  savedSsid = p.getString("ssid", "");
  savedPass = p.getString("pass", "");
  p.end();

  WiFi.persistent(false);
  WiFi.setHostname(hostName().c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  lastConnectedMs = millis();
  if (HOTSPOT_ALWAYS_ON || savedSsid.isEmpty()) startHotspot(false);
  if (!savedSsid.isEmpty()) joinSaved();
}

void wifilink::loop() {
  uint32_t now = millis();
  bool up = WiFi.status() == WL_CONNECTED;
  if (up) {
    lastConnectedMs = now;
    if (!mdnsUp && MDNS.begin(hostName().c_str())) {
      MDNS.addService("http", "tcp", 80);
      mdnsUp = true;
    }
  } else {
    if (mdnsUp) {
      MDNS.end();
      mdnsUp = false;
    }
    if (!apOn && now - lastConnectedMs > AUTO_HOTSPOT_AFTER_MS) startHotspot(false);
  }
  if (apOn) dns.processNextRequest();
  updateLed();
}

bool wifilink::connected() { return WiFi.status() == WL_CONNECTED; }
bool wifilink::hotspotOn() { return apOn; }
String wifilink::hotspotName() { return apName(); }
String wifilink::localName() { return hostName() + ".local"; }
String wifilink::ssid() { return savedSsid; }

bool wifilink::hotspotOff() {
  if (HOTSPOT_ALWAYS_ON) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  stopHotspot();
  return true;
}

void wifilink::hotspotOnByHand() { startHotspot(true); }

void wifilink::toggleHotspot() {
  if (HOTSPOT_ALWAYS_ON) {
    startHotspot(true);
    Serial.println("hotspot is always on in this debug build");
    return;
  }
  if (apOn) {
    stopHotspot();
    // Off by hand with no Wi-Fi: give it the full 5 minutes before it comes back.
    lastConnectedMs = millis();
  } else {
    startHotspot(true);
  }
}

void wifilink::setNetwork(const String &ssid, const String &pass) {
  Preferences p;
  p.begin("wifi", false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.end();
  savedSsid = ssid;
  savedPass = pass;
  lastConnectedMs = millis();
  WiFi.disconnect(false, false);
  joinSaved();
}

void wifilink::forgetNetwork() {
  Preferences p;
  p.begin("wifi", false);
  p.clear();
  p.end();
  savedSsid = savedPass = "";
}
