#include "runner.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <pb_decode.h>

#include <memory>
#include <new>

#include "gh_update.h"
#include "gtfs.h"
#include "settings.h"
#include "sim.h"
#include "wifi_link.h"

namespace {

// A vehicle older than this is dropped (§5); a feed that hasn't changed for
// this long is treated as stale and the map goes dark.
const uint32_t STALE_S = 120;
// A TLS session allocates two record buffers of about 17 KB each, plus the
// handshake. Below this much heap the poll is skipped rather than tried.
// Memory that stays this low for LOW_MEM_RESTART polls in a row is the one
// failure that restarts the board (HTTP errors never do).
const uint32_t TLS_MIN_BLOCK = 20000;
const uint32_t TLS_MIN_FREE = 50000;
const int LOW_MEM_RESTART = 10;
const uint32_t SIM_INTERVAL_MS = 2000;
const uint32_t BACKOFF_S[] = {30, 60, 120};

LedMap *gmap = nullptr;
Engine *engine = nullptr;
SimSource *sim = nullptr;

SemaphoreHandle_t mutex;
Occupancy shared;  // guarded by mutex
Status st;         // guarded by mutex
volatile bool simRestart = true;

// Network-task-only state.
Feed feedA, feedB;
Feed *cur = &feedA, *scratch = &feedB;  // cur = last good feed
Occupancy work;
Placement places[MAX_VEHICLES];
// Simulated mode's buffers (about 20 KB) exist only while it runs, so Live
// mode has that memory for TLS.
struct SimBuffers {
  Feed feed;
  uint8_t pb[8192];  // a simulated feed is ~3 KB
};
SimBuffers *simBufs = nullptr;
int lowMemPolls = 0;

uint64_t epochNow() {
  time_t t = time(nullptr);
  return t > 1700000000 ? uint64_t(t) : 0;
}

void publish(const Feed &feed, bool apply) {
  uint64_t now = epochNow();
  if (apply) engine->update(feed, now, work, places);
  else work.clear();
  xSemaphoreTake(mutex, portMAX_DELAY);
  shared = work;
  st.seq++;
  st.feedT = feed.t;
  st.vehicles = apply ? feed.count : 0;
  st.otherRoutes = feed.otherRoutes;
  st.dropped = feed.dropped;
  st.noRouteId = feed.noRouteId;
  st.otherIdCount = feed.otherIdCount;
  memcpy(st.otherIds, feed.otherIds, sizeof st.otherIds);
  memset(st.routes, 0, sizeof st.routes);
  if (apply) {
    for (int k = 0; k < feed.count; k++) {
      const Placement &p = places[k];
      if (p.route < 0 || p.route >= 32) continue;
      RouteStats &r = st.routes[p.route];
      switch (p.state) {
        case VState::Lit: r.lit++; break;
        case VState::Hidden: r.hidden++; break;
        case VState::OffRoute: r.offRoute++; break;
        case VState::Stale: r.stale++; break;
        case VState::NoPosition: r.noPosition++; break;
      }
    }
  }
  xSemaphoreGive(mutex);
}

void setError(const String &e, int code) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  st.feedOk = e.isEmpty();
  st.lastError = e;
  st.httpCode = code;
  xSemaphoreGive(mutex);
  if (!e.isEmpty()) Serial.printf("feed: %s\n", e.c_str());
}

// --- streaming the HTTP body into nanopb ---

struct StreamCtx {
  NetworkClient *client;
  uint32_t timeoutMs;
  uint32_t bytes;
};

bool streamRead(pb_istream_t *s, pb_byte_t *buf, size_t count) {
  StreamCtx *ctx = static_cast<StreamCtx *>(s->state);
  size_t got = 0;
  uint32_t last = millis();
  while (got < count) {
    int avail = ctx->client->available();
    if (avail > 0) {
      int r = ctx->client->read(buf + got, min(count - got, size_t(avail)));
      if (r > 0) {
        got += size_t(r);
        last = millis();
        continue;
      }
    }
    if (!ctx->client->connected() && ctx->client->available() <= 0) {
      // Clean end of the body between two fields: tell nanopb it's EOF.
      if (got == 0) s->bytes_left = 0;
      return false;
    }
    if (millis() - last > ctx->timeoutMs) return false;
    vTaskDelay(1);
  }
  ctx->bytes += count;
  return true;
}

String hostOf(const String &url) {
  int a = url.indexOf("://");
  if (a < 0) return String();
  int b = url.indexOf('/', a + 3);
  return url.substring(a + 3, b < 0 ? url.length() : b);
}

// The router's IPv4 DNS (from DHCP) stays first, with a public backup so a
// router that stops answering doesn't stop the feed. Some networks also
// advertise IPv6 DNS servers, which replace these a few minutes after joining;
// the board has no IPv6 route, so lookups through them fail. Any server that
// is missing, IPv6, or the hotspot's own address is put back to IPv4.
IPAddress lastGoodDns;  // the router's IPv4 DNS, remembered from DHCP

bool usableDns(const IPAddress &ip) {
  return ip.type() == IPv4 && ip != IPAddress(0, 0, 0, 0) && ip != WiFi.softAPIP();
}

void ensureDns() {
  IPAddress d0 = WiFi.STA.dnsIP(0);
  if (usableDns(d0)) lastGoodDns = d0;
  else {
    IPAddress gw = WiFi.STA.gatewayIP();
    IPAddress fix = usableDns(lastGoodDns) ? lastGoodDns : usableDns(gw) ? gw : IPAddress(1, 1, 1, 1);
    WiFi.STA.dnsIP(0, fix);
    Serial.printf("DNS: main server was %s, set to %s\n", d0.toString().c_str(), fix.toString().c_str());
  }
  if (!usableDns(WiFi.STA.dnsIP(1))) WiFi.STA.dnsIP(1, IPAddress(8, 8, 8, 8));
}

// Look the feed's host up before connecting, with retries: a lookup can time
// out while the router is slow to answer. A good answer is cached, so the
// connection that follows doesn't wait again.
bool resolveHost(const String &host, String &why) {
  IPAddress ip;
  for (int k = 0; k < 3; k++) {
    ensureDns();
    if (WiFi.hostByName(host.c_str(), ip) == 1) return true;
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
  why = "can't look up " + host + " (DNS servers " + WiFi.STA.dnsIP(0).toString() + ", " + WiFi.STA.dnsIP(1).toString() + ")";
  return false;
}

// "connection refused" from HTTPClient covers DNS, TCP and TLS failures
// alike. Say which: the TLS error if there was one, what the feed's host
// resolves to, and how much memory a TLS session had to work with.
String failureDetail(NetworkClientSecure &client, const String &url) {
  String d;
  char tls[100] = "";
  if (client.lastError(tls, sizeof tls)) d += String("; TLS: ") + tls;
  String host = hostOf(url);
  IPAddress ip;
  if (!host.isEmpty()) d += WiFi.hostByName(host.c_str(), ip) == 1 ? "; " + host + " is " + ip.toString() : "; can't look up " + host;
  d += "; memory " + String(ESP.getFreeHeap() / 1024) + " KB free, largest block " + String(ESP.getMaxAllocHeap() / 1024) + " KB";
  return d;
}

// One poll of the live feed. Returns true if the feed was read (changed or not).
bool fetchLive(const Settings &s) {
  uint32_t freeHeap = ESP.getFreeHeap(), block = ESP.getMaxAllocHeap();
  if (freeHeap < TLS_MIN_FREE || block < TLS_MIN_BLOCK) {
    setError("not enough memory for a secure connection (" + String(freeHeap / 1024) + " KB free, largest block " +
                 String(block / 1024) + " KB)",
             0);
    if (++lowMemPolls >= LOW_MEM_RESTART) {
      log_e("memory low for %d polls, restarting", lowMemPolls);
      delay(100);
      ESP.restart();
    }
    return false;
  }
  lowMemPolls = 0;

  ensureDns();
  String host = hostOf(s.feedUrl), why;
  if (!host.isEmpty() && !resolveHost(host, why)) {
    setError(why, 0);
    return false;
  }
  uint32_t t0 = millis();
  std::unique_ptr<NetworkClientSecure> client(new NetworkClientSecure);
  client->useBuiltinCACertBundle();
  client->setHandshakeTimeout(s.connectS);
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the body can be streamed as-is
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // Socrata redirects to blob storage
  http.setConnectTimeout(int32_t(s.connectS) * 1000);
  http.setTimeout(uint16_t(min<uint32_t>(s.readS * 1000UL, 65000UL)));
  http.setUserAgent("AustinTransitMap/1.0 (ESP32)");
  if (!http.begin(*client, s.feedUrl)) {
    setError("bad feed URL", 0);
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    String e = code < 0 ? String("connection failed: ") + http.errorToString(code) : String("HTTP ") + code;
    if (code < 0) e += failureDetail(*client, s.feedUrl);
    setError(e, code);
    http.end();
    return false;
  }

  NetworkClient *body = http.getStreamPtr();
  int len = http.getSize();
  std::string err;
  StreamCtx ctx{body, uint32_t(s.readS) * 1000, 0};
  pb_istream_t in = {&streamRead, &ctx, len > 0 ? size_t(len) : SIZE_MAX, nullptr};
  DecodeResult r = decodeFeedPb(&in, *gmap, cur->t, *scratch, err);
  uint32_t bytes = ctx.bytes;
  http.end();

  uint32_t took = millis() - t0;
  xSemaphoreTake(mutex, portMAX_DELAY);
  st.httpCode = code;
  st.fetchMs = took;
  st.bytes = bytes;
  xSemaphoreGive(mutex);

  if (r == DecodeResult::Error) {
    setError(err.c_str(), code);
    return false;
  }
  setError("", code);
  if (r == DecodeResult::Ok) {
    std::swap(cur, scratch);
    xSemaphoreTake(mutex, portMAX_DELAY);
    st.lastChangeMs = millis();
    xSemaphoreGive(mutex);
    Serial.printf("feed t=%llu: %u board vehicles, %u other, %lu bytes, %lu ms\n", cur->t, cur->count, cur->otherRoutes, (unsigned long)bytes, (unsigned long)took);
  }
  return true;
}

bool sameSimRoutes(const std::vector<SimRoute> &a, const std::vector<SimRoute> &b) {
  if (a.size() != b.size()) return false;
  for (size_t k = 0; k < a.size(); k++)
    if (strcmp(a[k].routeId, b[k].routeId) || a[k].perHour != b[k].perHour || a[k].mph != b[k].mph) return false;
  return true;
}

void runSimulated(const Settings &s) {
  // New vehicles per hour or speeds on the web page: lay the vehicles out again.
  if (!sameSimRoutes(sim->opts.routes, s.simRoutes)) simRestart = true;
  if (simRestart) {
    simRestart = false;
    sim->opts.routes = s.simRoutes;
    sim->opts.timeScale = s.simTimeScale;
    int n = sim->setup();
    engine->reset();
    Serial.printf("simulated feed: %d vehicles\n", n);
  }
  sim->opts.timeScale = s.simTimeScale;
  if (!simBufs) simBufs = new (std::nothrow) SimBuffers;
  if (!simBufs) {
    setError("not enough memory for the simulated feed", 0);
    return;
  }
  Feed &simFeed = simBufs->feed;
  sim->sample(millis(), epochNow(), simFeed);
  // Through the real decoder, exactly like a live feed.
  size_t n = encodeFeedPb(simFeed, simBufs->pb, sizeof simBufs->pb);
  if (!n) {
    setError("simulated feed too large", 0);
    return;
  }
  pb_istream_t in = pb_istream_from_buffer(simBufs->pb, n);
  std::string err;
  if (decodeFeedPb(&in, *gmap, 0, *scratch, err) != DecodeResult::Ok) {
    setError(err.c_str(), 0);
    return;
  }
  std::swap(cur, scratch);
  xSemaphoreTake(mutex, portMAX_DELAY);
  st.lastChangeMs = millis();
  st.bytes = n;
  xSemaphoreGive(mutex);
  setError("", 0);
  publish(*cur, true);
}

void netTask(void *) {
  uint32_t nextPoll = 0;
  int failures = 0;
  bool showing = false;  // vehicles from the live feed are on the map
  DisplayMode lastMode = DisplayMode::ChainTest;
  for (;;) {
    Settings s = settings::get();
    // A GitHub check or install runs here, between polls, so it never needs
    // TLS memory at the same time as the feed.
    ghupdate::service(s, wifilink::connected());
    if (s.mode != lastMode) {
      if (s.mode == DisplayMode::Simulated) simRestart = true;
      else {
        delete simBufs;
        simBufs = nullptr;
      }
      engine->reset();
      cur->clearCounts();
      cur->t = 0;
      publish(*cur, false);
      showing = false;
      nextPoll = millis();
      lastMode = s.mode;
    }

    if (s.mode == DisplayMode::Simulated) {
      runSimulated(s);
      vTaskDelay(pdMS_TO_TICKS(SIM_INTERVAL_MS));
      continue;
    }
    if (s.mode != DisplayMode::Live || gmap->empty()) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    uint32_t now = millis();
    if (wifilink::connected() && int32_t(now - nextPoll) >= 0) {
      xSemaphoreTake(mutex, portMAX_DELAY);
      st.lastFetchMs = now;
      xSemaphoreGive(mutex);
      if (fetchLive(s)) {
        failures = 0;
        nextPoll = millis() + s.pollS * 1000UL;
      } else {
        // Back off and keep trying; never restart for a network problem.
        uint32_t wait = BACKOFF_S[min(failures, 2)];
        failures++;
        nextPoll = millis() + max<uint32_t>(wait, s.pollS) * 1000UL;
      }
      // Re-run matching even when the feed didn't change, so vehicles age out.
      xSemaphoreTake(mutex, portMAX_DELAY);
      bool stale = st.lastChangeMs == 0 || millis() - st.lastChangeMs > STALE_S * 1000UL;
      xSemaphoreGive(mutex);
      publish(*cur, !stale);
      showing = !stale;
    } else if (showing) {
      // Wi-Fi down or waiting to poll: still go dark once the data is stale.
      xSemaphoreTake(mutex, portMAX_DELAY);
      bool stale = millis() - st.lastChangeMs > STALE_S * 1000UL;
      xSemaphoreGive(mutex);
      if (stale) {
        publish(*cur, false);
        showing = false;
      }
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    st.nextPollInMs = int32_t(nextPoll - millis()) > 0 ? nextPoll - millis() : 0;
    xSemaphoreGive(mutex);
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

}  // namespace

void runner::begin(LedMap &map) {
  gmap = &map;
  engine = new Engine(map);
  engine->opts.staleS = STALE_S;
  sim = new SimSource(map);
  mutex = xSemaphoreCreateMutex();
  shared.clear();
  work.clear();
  // 12 KB stack: TLS handshakes and the decoder's nested messages need room.
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
}

bool runner::takeOccupancy(uint32_t &lastSeq, Occupancy &out) {
  bool changed = false;
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (st.seq != lastSeq) {
    out = shared;
    lastSeq = st.seq;
    changed = true;
  }
  xSemaphoreGive(mutex);
  return changed;
}

Status runner::status() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  Status s = st;
  xSemaphoreGive(mutex);
  return s;
}

void runner::restartSimulation() { simRestart = true; }
