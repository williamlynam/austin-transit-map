#include "gh_update.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>

#include <memory>
#include <mutex>
#include <new>

#include "version.h"

namespace {

// The same memory a feed poll needs: one TLS session at a time.
const uint32_t TLS_MIN_BLOCK = 20000;
const uint32_t DAY_MS = 24UL * 3600 * 1000;
const uint32_t FIRST_CHECK_MS = 2UL * 60 * 1000;  // after boot, once Wi-Fi has settled
const uint32_t READ_TIMEOUT_MS = 20000;

std::mutex mu;
ghupdate::Info st;  // guarded by mu
volatile bool wantCheck = false, wantInstall = false;
// Network task only.
String assetUrl;
uint32_t assetSize = 0;
uint32_t nextAutoMs = FIRST_CHECK_MS;  // millis() of the next daily check

void setState(ghupdate::State s, const String &err = String()) {
  std::lock_guard<std::mutex> g(mu);
  st.state = s;
  st.error = err;
  if (s != ghupdate::State::Installing) st.done = st.total = 0;
}

// "v0.1.16" or "0.1.16" to {0, 1, 16}. Missing parts are 0.
void parseVersion(const char *s, long out[3]) {
  out[0] = out[1] = out[2] = 0;
  while (*s && !isdigit(uint8_t(*s))) s++;
  for (int k = 0; k < 3 && *s; k++) {
    out[k] = strtol(s, const_cast<char **>(&s), 10);
    if (*s != '.') break;
    s++;
  }
}

bool newer(const char *latest, const char *installed) {
  long a[3], b[3];
  parseVersion(latest, a);
  parseVersion(installed, b);
  for (int k = 0; k < 3; k++)
    if (a[k] != b[k]) return a[k] > b[k];
  return false;
}

void setupHttp(HTTPClient &http, NetworkClientSecure &client) {
  client.useBuiltinCACertBundle();
  client.setHandshakeTimeout(15);
  http.useHTTP10(true);  // no chunked encoding, so bodies stream as-is
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // release files redirect to GitHub's storage
  http.setConnectTimeout(15000);
  http.setTimeout(uint16_t(READ_TIMEOUT_MS));
  http.setUserAgent("AustinTransitMap/" FIRMWARE_VERSION " (ESP32)");  // GitHub's API refuses requests without one
}

// Reads up to len bytes of a body, waiting for slow TLS records the way the
// feed reader does. Returns how many arrived.
uint32_t readBody(NetworkClient *body, uint8_t *buf, uint32_t len) {
  uint32_t got = 0, last = millis();
  while (got < len) {
    int avail = body->available();
    if (avail > 0) {
      int r = body->read(buf + got, min<size_t>(len - got, size_t(avail)));
      if (r > 0) {
        got += uint32_t(r);
        last = millis();
        continue;
      }
    }
    if (!body->connected() && body->available() <= 0) break;
    if (millis() - last > READ_TIMEOUT_MS) break;
    vTaskDelay(1);
  }
  return got;
}

bool enoughMemory() {
  if (ESP.getMaxAllocHeap() >= TLS_MIN_BLOCK) return true;
  setState(ghupdate::State::Failed, "not enough memory for a secure connection (largest block " +
                                        String(ESP.getMaxAllocHeap() / 1024) + " KB); try again in a minute");
  return false;
}

// Reads the newest release (or pre-release) and picks its OTA file.
bool check(const Settings &s) {
  setState(ghupdate::State::Checking);
  if (!enoughMemory()) return false;
  String repo = s.ghRepo;
  repo.trim();
  if (repo.indexOf('/') <= 0) {
    setState(ghupdate::State::Failed, "set the GitHub repo as owner/name");
    return false;
  }
  // /releases/latest skips pre-releases; the plain list is newest first and includes them.
  String url = "https://api.github.com/repos/" + repo + (s.ghPrerelease ? "/releases?per_page=1" : "/releases/latest");

  std::unique_ptr<NetworkClientSecure> client(new (std::nothrow) NetworkClientSecure);
  if (!client) {
    setState(ghupdate::State::Failed, "not enough memory");
    return false;
  }
  HTTPClient http;
  setupHttp(http, *client);
  if (!http.begin(*client, url)) {
    setState(ghupdate::State::Failed, "bad GitHub repo name");
    return false;
  }
  // HTTP/1.1 here: getString() below handles both a sized and a chunked answer.
  http.useHTTP10(false);
  http.addHeader("Accept", "application/vnd.github+json");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    String e = code == 404   ? "no release found in " + repo + " (the repo must be public and have a release)"
               : code < 0    ? String("can't reach GitHub: ") + http.errorToString(code)
               : code == 403 ? String("GitHub refused (HTTP 403, usually its hourly limit); try again later")
                             : "GitHub answered HTTP " + String(code);
    http.end();
    setState(ghupdate::State::Failed, e);
    return false;
  }

  // The whole answer (about 7 KB) is read first and then parsed. Parsing
  // straight from the TLS stream stopped early on the board (IncompleteInput).
  const int MAX_BODY = 32768;
  int len = http.getSize();
  if (len > MAX_BODY) {
    http.end();
    setState(ghupdate::State::Failed, "GitHub's answer is too large (" + String(len / 1024) + " KB)");
    return false;
  }
  String body = http.getString();
  http.end();
  if (body.isEmpty() || (len > 0 && int(body.length()) < len)) {
    setState(ghupdate::State::Failed, "GitHub's answer stopped after " + String(body.length()) + (len > 0 ? " of " + String(len) : String()) + " bytes");
    return false;
  }

  // Only the fields we use, so the release notes never fill memory.
  JsonDocument filter, doc;
  JsonObject f = s.ghPrerelease ? filter.add<JsonObject>() : filter.to<JsonObject>();
  f["tag_name"] = true;
  f["prerelease"] = true;
  JsonObject fa = f["assets"].add<JsonObject>();
  fa["name"] = true;
  fa["size"] = true;
  fa["browser_download_url"] = true;
  DeserializationError je = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  body = String();
  if (je) {
    setState(ghupdate::State::Failed, String("couldn't read GitHub's answer: ") + je.c_str());
    return false;
  }
  JsonObjectConst rel = s.ghPrerelease ? doc[0].as<JsonObjectConst>() : doc.as<JsonObjectConst>();
  const char *tag = rel["tag_name"] | "";
  if (!*tag) {
    setState(ghupdate::State::Failed, "no release found in " + repo);
    return false;
  }

  // transitmap-X-ota.bin; never the debug build.
  assetUrl = "";
  assetSize = 0;
  for (JsonObjectConst a : rel["assets"].as<JsonArrayConst>()) {
    String name = a["name"] | "";
    if (name.endsWith("-ota.bin") && name.indexOf("debug") < 0) {
      assetUrl = a["browser_download_url"] | "";
      assetSize = a["size"] | 0;
      break;
    }
  }

  String latest = tag;
  if (latest.startsWith("v")) latest.remove(0, 1);
  bool isNewer = newer(tag, FIRMWARE_VERSION);
  {
    std::lock_guard<std::mutex> g(mu);
    st.latest = latest;
    st.prerelease = rel["prerelease"] | false;
    st.checkedAtMs = millis();
    if (!st.checkedAtMs) st.checkedAtMs = 1;
  }
  nextAutoMs = millis() + DAY_MS;
  if (isNewer && assetUrl.isEmpty()) {
    setState(ghupdate::State::Failed, "release " + latest + " has no -ota.bin file attached");
    return false;
  }
  setState(isNewer ? ghupdate::State::Available : ghupdate::State::UpToDate);
  Serial.printf("GitHub: newest release %s, installed %s\n", latest.c_str(), FIRMWARE_VERSION);
  return true;
}

// Streams the OTA file into the spare app slot. Restarts on success.
void install() {
  if (!enoughMemory()) return;
  {
    std::lock_guard<std::mutex> g(mu);
    st.state = ghupdate::State::Installing;
    st.error = "";
    st.done = 0;
    st.total = assetSize;
  }
  std::unique_ptr<NetworkClientSecure> client(new (std::nothrow) NetworkClientSecure);
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[2048]);
  if (!client || !buf) {
    setState(ghupdate::State::Failed, "not enough memory");
    return;
  }
  HTTPClient http;
  setupHttp(http, *client);
  if (!http.begin(*client, assetUrl)) {
    setState(ghupdate::State::Failed, "bad download address");
    return;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    setState(ghupdate::State::Failed, code < 0 ? String("download failed: ") + http.errorToString(code)
                                                : "download failed: HTTP " + String(code));
    return;
  }
  int len = http.getSize();
  if (len <= 0) {
    http.end();
    setState(ghupdate::State::Failed, "download has no size");
    return;
  }
  if (!Update.begin(size_t(len), U_FLASH)) {
    http.end();
    setState(ghupdate::State::Failed, String("can't start the update: ") + Update.errorString());
    return;
  }
  {
    std::lock_guard<std::mutex> g(mu);
    st.total = uint32_t(len);
  }
  NetworkClient *body = http.getStreamPtr();
  uint32_t got = 0;
  while (got < uint32_t(len)) {
    uint32_t n = readBody(body, buf.get(), min<uint32_t>(2048, uint32_t(len) - got));
    if (!n || Update.write(buf.get(), n) != n) break;
    got += n;
    std::lock_guard<std::mutex> g(mu);
    st.done = got;
  }
  http.end();
  if (got < uint32_t(len)) {
    String e = Update.hasError() ? String(Update.errorString()) : "connection dropped at " + String(got / 1024) + " of " + String(len / 1024) + " KB";
    Update.abort();
    setState(ghupdate::State::Failed, "download failed: " + e);
    return;
  }
  // end() checks the image (its checksum and that it is an ESP32 app) before switching to it.
  if (!Update.end(true)) {
    setState(ghupdate::State::Failed, String("update rejected: ") + Update.errorString());
    return;
  }
  Serial.println("GitHub: update installed, restarting");
  {
    std::lock_guard<std::mutex> g(mu);
    st.done = st.total;
    st.error = "";
  }
  delay(1500);  // lets the page see the finished download
  ESP.restart();
}

}  // namespace

void ghupdate::requestCheck() { wantCheck = true; }
void ghupdate::requestInstall() { wantInstall = true; }

ghupdate::Info ghupdate::info() {
  std::lock_guard<std::mutex> g(mu);
  return st;
}

const char *ghupdate::stateName(State s) {
  switch (s) {
    case State::Checking: return "checking";
    case State::UpToDate: return "up_to_date";
    case State::Available: return "available";
    case State::Installing: return "installing";
    case State::Failed: return "failed";
    default: return "idle";
  }
}

void ghupdate::service(const Settings &s, bool online) {
  if (!online) {
    if (wantCheck || wantInstall) {
      wantCheck = false;
      wantInstall = false;
      setState(State::Failed, "the map isn't on Wi-Fi");
    }
    return;
  }
  // Daily check: only finds the update and shows it on the page; installing
  // is always by hand.
  bool due = s.ghDailyCheck && int32_t(millis() - nextAutoMs) >= 0;
  if (wantInstall) {
    wantInstall = false;
    wantCheck = false;
    // Always re-check first, so the file installed is the newest one.
    if (check(s)) {
      if (info().state == State::Available) install();
      else setState(State::UpToDate);
    }
  } else if (wantCheck || due) {
    wantCheck = false;
    // A failed check tries again in an hour, not straight away.
    if (!check(s)) nextAutoMs = millis() + 3600UL * 1000;
  }
}
