# Austin Live Transit Map — Firmware & Data Pipeline Design

Board: `Live_Map_4layers` rev A (4-layer PCB, "Austin, TX — Live Rapid Bus and Rail Map")
Date: 2026-10-06 · Status: draft for review

---

## 1. What the hardware gives us

Everything in this section was traced from `Live_Map_4layers.kicad_pcb` (pad nets) and `production/bom.csv`, not assumed.

**"4 layers" is the PCB stackup** (F.Cu signal + GND, In1 GND plane, In2 5 V/3.3 V power, B.Cu GND), not four map layers. Logically the map is **five independent LED chains, one per route**, on a single board.

| Chain | ESP32 GPIO | Buffer | Series R | LEDs | Refs | Route (from map artwork) | Index 0 end → last end |
|---|---|---|---|---|---|---|---|
| LINE1 | **IO16** | U3 | R1013 | 41 | D1–D41 | 550 MetroRail Red Line | Downtown → Leander |
| LINE2 | **IO21** | U4 | R1016 | 37 | D42–D78 | 803 Rapid | Westgate → Domain |
| LINE3 | **IO19** | U5 | R1014 | 44 | D79–D122 | 801 Rapid | Southpark Meadows → Tech Ridge |
| LINE4 | **IO17** | U6 | R1017 | 41 | D123–D163 | 837 Rapid | Downtown → Expo Center |
| LINE5 | **IO18** | U7 | R1015 | 34 | D164–D197 | 800 Rapid | Goodnight Ranch → Barbara Jordan/Mueller |
| | | | | **197** | | | |

The route-to-chain assignment comes from matching LED coordinates on the board to the route artwork; worth a quick confirm at bring-up with the chain-ID test pattern (§9).

Other I/O:

| Function | Net / pin | Notes |
|---|---|---|
| LEDs | XL-2020RGBC-WS2812B (2020 package), 5 V | GRB order, 800 kHz |
| Level shift | SN74LV1T34 ×5, 5 V supply | 3.3 V in, 5 V out, one per chain. 10 k pulldowns R1019–R1023 on IO16–IO21 keep the chains dark during boot |
| WIFI status LED | **IO25** → R1018 10 k → D1005 (amber) | Active high. 10 k gives a very dim LED (~0.1 mA); fine as an indicator, lower R if it's too faint |
| PWR LED | 3.3 V → R1001 10 k → D1001 | Not controllable |
| MODE button | **IO26** (SW1003), active low | **No external pull-up** (C1012 is DNP). Firmware must use `INPUT_PULLUP` |
| IO0 button | IO0 (SW1001), active low | Boot strap. Usable as a second button after boot |
| EN button | EN (SW1002) | Hardware reset |
| USB/UART | CP2102N, auto-reset via Q1001/Q1002 | Standard esptool auto-program |
| Power | USB-C, 5.1 k CC pulldowns, 3 A PTC (F1), AMS1117-3.3 | See power budget (§6) |
| MCU | ESP32-WROOM-32E | No PSRAM; assume 4 MB flash until the exact module variant is confirmed |

None of the LED GPIOs are strapping pins, and IO16/IO17 are free on the WROOM-32E (they're only taken on WROVER modules with PSRAM).

---

## 2. Architecture overview

```
 CapMetro GTFS-rt          ESP32 (direct, default)
 VehiclePositions.pb  ──HTTPS──►  stream decode (nanopb)
   every 15 s                        │ keep only route_id ∈ {550,800,801,803,837}
                                     ▼
                              vehicle list (≤ ~80 entries)
                                     │ snap lat/lon → LED index per route
                                     ▼
                              LED state (per chain)  ──►  FastLED/RMT ×5 ──► 197 LEDs
```

Optional: a lightweight proxy that does the decode/filter/snap off-device and serves a tiny response (§3.4). The firmware supports both through one setting: the feed URL plus a format flag.

**Toolchain:** PlatformIO (Arduino framework on arduino-esp32 3.x / ESP-IDF 5). The Arduino IDE also works; PlatformIO is recommended because nanopb code generation and LittleFS uploads are scriptable.

**Tasks (FreeRTOS):**

| Task | Core | Period | Job |
|---|---|---|---|
| `net` | 0 | 15 s | Wi-Fi, fetch, decode, filter, publish snapshot |
| `render` | 1 | 50 Hz | Map snapshot → LEDs, fades/animation, `FastLED.show()` |
| `ui` | 1 | 20 ms | Buttons, status LED patterns, mode changes |
| web/portal | 0 | on demand | Config portal, map upload, OTA |

`net` publishes an immutable vehicle snapshot (double buffer + pointer swap under a mutex), so `render` never blocks on the network.

---

## 3. CapMetro feed access & decoding

### 3.1 Feeds

CapMetro publishes GTFS-realtime through the Texas/City of Austin open-data portal. The feeds are public, need no API key, and update about every 15 s.

| Feed | URL (default; make it configurable) |
|---|---|
| VehiclePositions (protobuf) | `https://data.texas.gov/download/eiei-9rpf/application%2Foctet-stream` |
| VehiclePositions (JSON) | `https://data.texas.gov/download/cuc7-ywmd/text%2Fplain` |
| Static GTFS (zip; companion tool only) | `https://data.texas.gov/download/r4v4-vz24/application%2Fx-zip-compressed` |

data.gov also lists the same dataset id (`eiei-9rpf`) under `data.austintexas.gov`. Both hosts have been used; the firmware must **follow redirects** (Socrata download links can 302 to blob storage) and keep the URL in config, not hard-coded.

> Verify at bring-up: I couldn't reach either host from my sandbox, so I haven't checked live feed size or the exact `route_id` strings. CapMetro has historically used plain route numbers (`"550"`, `"801"`). Check `routes.txt` in the static GTFS and the first decoded vehicles, and don't build any logic on assumed IDs.

### 3.2 Decode on the ESP32 (default)

- Generate C structs from Google's `gtfs-realtime.proto` with **nanopb**.
- **Stream, don't buffer.** Wrap the `WiFiClientSecure` stream in a `pb_istream_t` whose read callback pulls from the socket. Decode `FeedMessage.entity` with a **repeated-field callback** that decodes one `FeedEntity` at a time into a small stack struct, keeps it only if `vehicle.trip.route_id` is in the active route set, and then discards it. Peak RAM is one entity (~300 B) plus the TLS buffers, whatever the feed size.
- A `.options` file bounds strings so nothing is malloc'd: `route_id max_size:12`, `trip_id max_size:48`, `vehicle.id max_size:16`, `label max_size:16`. Turn unused fields (trip updates, alerts, `multi_carriage_details`) into callbacks that skip them.
- Fields used: `trip.route_id`, `trip.direction_id`, `vehicle.id`, `position.latitude/longitude/bearing`, `timestamp`, `current_status`, `header.timestamp`.
- **TLS:** use the arduino-esp32 CA certificate bundle (`esp_crt_bundle_attach`), not a pinned cert, so a certificate rotation on the portal doesn't brick the map. TLS costs about 40–50 KB of heap per connection, which fits without PSRAM.
- **Skip unchanged data:** if `header.timestamp` equals the last one seen, stop reading and close early.

The JSON feed works too (ArduinoJson with a `DeserializationOption::Filter`), but it's several times larger on the wire and slower to parse. Keep it only as a fallback.

### 3.3 Vehicle record (after filtering)

```c
struct Vehicle {
  uint8_t  route;        // index into the ledmap route table
  int8_t   dir;          // 0/1, -1 unknown
  float    lat, lon;
  uint16_t bearing;      // optional
  uint32_t ts;           // vehicle timestamp (epoch s)
  char     id[16];
};
```

Expect roughly 40–80 active vehicles across the five routes at peak. Allocate a fixed `Vehicle[128]` ×2 for double buffering (~9 KB).

### 3.4 Optional proxy

If direct decode turns out to be flaky (feed size growth, TLS heap fragmentation), a ~50-line service does the same work off-device. It could be a Cloudflare Worker, a small Python script on a Raspberry Pi, or anything else that runs on a timer. It fetches the .pb every 15 s, filters to the five routes, optionally snaps vehicles to LED indices with the same map file, and serves:

```json
{"t":1791253871,"v":[["801",0,30.2672,-97.7431],["550",1,30.4021,-97.8312]]}
```

(or, if pre-snapped, `{"t":…,"on":[[chain,index,dir],…]}`). Set `feed.format = "proxy_json"` in config. **Recommendation:** ship direct-decode as the default (no extra infrastructure to keep alive) and keep the proxy path for fallback and debugging.

---

## 4. Mapping lat/lon to an LED

Each LED gets its lat/lon by **manual placement** in the companion tool; there is no auto-mapping. Snapping doesn't run over the physical chain. It runs over **paths**: an ordered list of LED refs for a route, optionally one per direction (§7). For most routes the path is simply the chain order. Two board features need more than that.

**4.1 Shared crossing LEDs (D10, D23).** These are the only LEDs that serve two routes. Both sit on the 550 chain (LINE1):

| LED | Physical chain | Also on path of | Inserted between |
|---|---|---|---|
| D10 | LINE1 i=9 (550) | 837 | D138 ↔ D139 |
| D23 | LINE1 i=22 (550) | 801 | D112 ↔ D113 |

The borrowing route lists the LED under its own `shared` array with **its own lat/lon**, i.e. where that route's street is at the crossing. That can differ from the 550 entry's lat/lon at the rail line. The firmware resolves the ref to its physical chain and index, so an 837 bus snapped there lights an LED driven by IO16. Rendering is per physical LED, so a train and a bus can be on D10 at once (§5).

**4.2 Route 837 downtown loop (D123–D135).** The loop is a one-way pair of streets, so LED order depends on direction:

```
Northbound: D129 → D130 → D131 → D132 → D133 → D134 → D135 → D136 … D163
Southbound: D163 … D136 → D135 → D123 → D124 → D125 → D126 → D127 → D128 → D129
```

The two sides of the loop are only ~8 mm apart on the board and a block or two apart on the street, so geometry alone can't pick the side reliably. The 837 route therefore has **two direction paths**, and a bus is snapped only against the path for its direction:

1. **`trip.direction_id`** from the feed selects the path whose `direction_id` matches (§7). Which value is Northbound gets confirmed at bring-up from the static GTFS `trips.txt` headsigns.
2. **Fallback, if `direction_id` is missing:** use `position.bearing` (0–90° or 270–360° counts as NB, otherwise SB). This only matters on the loop LEDs.
3. **Fallback, if neither is available:** project against all of the route's paths, but only accept the result if the snapped LED appears in *every* path (the shared trunk, D135 onward). On the loop LEDs (D123–D134), the bus is hidden until its direction is known. The sides are too close to guess between, which is why Will scoped the loop to NB/SB.

The same per-direction mechanism is available on any route, but only 837 uses it today.

**4.3 Snapping algorithm.** For each vehicle, snap only against consecutive pairs of its own route's path for its direction, so overlapping corridors downtown never cross-assign:

1. Convert to local metres with an equirectangular projection centred on Austin: `x = (lon − lon0)·cos(lat0)·111320`, `y = (lat − lat0)·110540`. That's plenty accurate at city scale, with no trig per point at runtime.
2. **Project onto each segment** `path[k] → path[k+1]` and take the segment with the smallest perpendicular distance. This beats plain nearest-LED at corners and switchbacks (the 803 West 38th jog, the 800 Easton Park hairpin).
3. Choose `path[k]` or `path[k+1]` by the projection parameter `t` (`t < 0.5 → path[k]`). Keep `t` for optional sub-LED interpolation (crossfade the two neighbouring LEDs).
4. **Reject** if the distance exceeds `max_snap_m` (default 400 m). That covers vehicles deadheading, at the garage, or detoured off-route.
5. If an LED has `"skip": true` in the map (e.g. one that sits on a landmark rather than the route), it is excluded from snapping.

Cost: ~200 segments × ~60 vehicles ≈ 12 k multiply-adds every 15 s, which is negligible.

**Hysteresis:** keep each `vehicle.id`'s previous LED. Only move it if the new projection is more than 0.6 of a segment away. This stops a bus at a stop between two LEDs from flickering back and forth.

---

## 5. Display behaviour

- **Vehicle colour by route**, configurable in `config.json`. Defaults: 550 red-orange, Rapid routes in distinct hues so the overlapping downtown runs stay legible.
- **Direction:** direction 0 at full route colour, direction 1 at a tinted variant (configurable, or turn it off).
- **Two or more vehicles on one LED:** state is kept per *physical* LED as a small list of occupants (route, direction). One occupant gets its colour. Several from the same route get a brighter colour. Different routes (only possible on D10 and D23: a 550 train with an 837 or 801 bus) **alternate** between route colours every ~0.75 s with a short crossfade, so both stay readable. A blend would turn red + blue into a muddy third colour.
- **Background track glow** (optional, default off): every LED on a route at about 2% of route colour so the lines read in a dark room.
- **Motion:** when a vehicle changes LED, crossfade over ~1.5 s rather than jumping. Optionally "creep" along the segment using `t` and the time since the last update.
- **Stale data:** drop a vehicle when `now − ts > 120 s`. If the whole feed is stale for more than 2 minutes, fade all vehicles to dark and pulse the WIFI LED.
- **Night mode:** brightness schedule from NTP time (`TZ = "CST6CDT,M3.2.0,M11.1.0"`). Default 22:00–06:00 at 15%. Also, 550 doesn't run late, so an empty Red Line overnight is correct, not an error.

---

## 6. LED driver & power

**Driver: FastLED ≥ 3.9 with the ESP32 RMT5 backend** (arduino-esp32 3.x). There are 5 chains and the ESP32 has 8 RMT TX channels, so each chain gets its own channel and all five shift out in parallel (≈1.3 ms for the longest, 44-LED chain).

```cpp
FastLED.addLeds<WS2812B, 16, GRB>(line1, 41);  // 550
FastLED.addLeds<WS2812B, 21, GRB>(line2, 37);  // 803
FastLED.addLeds<WS2812B, 19, GRB>(line3, 44);  // 801
FastLED.addLeds<WS2812B, 17, GRB>(line4, 41);  // 837
FastLED.addLeds<WS2812B, 18, GRB>(line5, 34);  // 800
```

In real code the pins and counts come from the map file at boot. FastLED needs compile-time pins, so the firmware instantiates all five controllers with the board's fixed pins and sizes the buffers from the map's `count`.

Why not Adafruit NeoPixel: it drives one strip at a time and is more prone to Wi-Fi-interrupt glitches. **NeoPixelBus** (`NeoEsp32RmtNWs2812xMethod`, one RMT channel per chain) is the fallback if FastLED shows flicker with Wi-Fi active. Keep the LED code behind a small `LedOut` interface so the swap is one file.

**Power budget.** 197 × 60 mA ≈ 11.8 A at full white, against a 3 A fuse and whatever USB-C source is attached (5.1 k CC = default USB power; many chargers give 1.5–3 A, a PC port 0.5–0.9 A). Normal display (≈60 lit LEDs at modest brightness) draws a few hundred mA, but test patterns can trip the PTC. So:

- `FastLED.setMaxPowerInVoltsAndMilliamps(5, cfg.max_ma)`, with **default 1500 mA** and a user setting.
- Global brightness default 64/255.
- The full-white self-test is capped by the same limiter.

---

## 7. LED map file format (`/ledmap.json` on LittleFS)

Produced by the companion software, uploaded through the web portal (or with `pio run -t uploadfs`). It's JSON for easy diffing and hand edits; the firmware parses it once at boot into compact arrays (~3 KB) and frees the JSON.

```json
{
  "version": 1,
  "board": "Live_Map_4layers rev A",
  "projection_origin": [30.2672, -97.7431],
  "max_snap_m": 400,
  "routes": [
    {
      "route_id": "550",
      "name": "MetroRail Red Line",
      "chain": "LINE1",
      "gpio": 16,
      "count": 41,
      "color": "#FF3B1F",
      "color_dir1": "#FF8A5C",
      "leds": [
        {"i": 0, "ref": "D1", "x_mm": 207.51, "y_mm": 167.0,
         "lat": 30.2644, "lon": -97.7397, "stop_id": "5021", "label": "Downtown"},
        {"i": 1, "ref": "D2", "x_mm": 211.51, "y_mm": 167.0,
         "lat": 30.2649, "lon": -97.7360}
      ]
    }
  ]
}
```

**Shared LEDs and paths** (the schema the companion tool exports):

```json
{
  "route_id": "837",
  "chain": "LINE4",
  "leds":   [ { "i": 0, "ref": "D123", "lat": …, "lon": … }, … ],
  "shared": [ { "ref": "D10", "chain": "LINE1", "i": 9, "lat": …, "lon": … } ],
  "paths": [
    { "direction_id": 0, "name": "Northbound",
      "seq": ["D129","D130","D131","D132","D133","D134","D135","D136","D137","D138","D10","D139", "…", "D163"] },
    { "direction_id": 1, "name": "Southbound",
      "seq": ["D163", "…", "D139","D10","D138","D137","D136","D135","D123","D124","D125","D126","D127","D128","D129"] }
  ]
}
```

(`…` is shorthand for this doc; the real file lists every ref. Which `direction_id` is NB is a placeholder until it's confirmed from GTFS.)

- `leds` stays in chain order and holds the route's own physical LEDs.
- `shared` lists LEDs on another chain that this route borrows, with this route's own lat/lon. 801 has `{"ref":"D23","chain":"LINE1","i":22,…}`.
- `paths[].seq` is the snapping order. Each ref's position is looked up first in this route's `shared`, then in its `leds`. A route with no direction split has one path with `"direction_id": null`. If `paths` is absent, the path is `leds` in chain order.
- The firmware rejects a map in which a `seq` ref resolves to nothing, or a `shared` entry's `chain`/`i` doesn't match the ref's physical location.

Rules:

- `leds` is in **chain order** (`i` = index from the DIN end). `ref`, `x_mm` and `y_mm` tie each entry back to the KiCad footprint, so the companion tool can draw the board and you can find a physical LED.
- `lat`/`lon` are required for snapping. `stop_id`/`label` are optional, for the companion UI and for future "light the stop" modes.
- Optional per-LED `"skip": true` excludes it from snapping.
- `gpio` and `count` must match the firmware's compiled pin table. The firmware validates them and refuses a mismatched map, showing a red blink on the WIFI LED and an error on the portal.
- The firmware also accepts unknown keys, so the companion tool can add fields without a firmware change.

**A seed file is already generated from the PCB**: `ledmap_seed.json` (alongside this doc). It has all 197 LEDs with chain, index, ref and board coordinates, with `lat`/`lon` left null. The companion tool's job is to fill those in. Every LED is placed by hand (no auto-mapping). The tool walks one route at a time and advances to the next LED in the path after each placement. Stop and station names for the routes on the board come from static GTFS `stops.txt`.

---

## 8. Wi-Fi, configuration & updates

- **Provisioning:** first boot, or **MODE held for 5 s**, starts a captive portal AP `TransitMap-XXXX` (WiFiManager or a small custom portal on `WebServer`). Credentials are stored in NVS.
- **Normal mode web UI** at `http://transitmap.local` (mDNS) offers:
  - status: last feed time, vehicles per route, heap, RSSI
  - brightness, max mA, night schedule, colours, direction tint, track glow
  - feed URL and format (direct pb / json / proxy)
  - upload `ledmap.json`
  - OTA firmware upload
- **Files:** `/config.json` (settings) and `/ledmap.json` (map) on LittleFS.
- **Partitions (4 MB):** `nvs` 20 KB, `otadata`, `app0` 1.5 MB, `app1` 1.5 MB, `littlefs` ~960 KB.
- **Fetch policy:** poll every 15 s (configurable, min 10 s). On error, back off 15 → 30 → 60 → 120 s. Use a 10 s HTTP timeout. A task watchdog on `net` reboots the board if it wedges.

**Buttons**

| Button | Short press | Long press |
|---|---|---|
| MODE (IO26) | Cycle brightness presets | 5 s: config portal |
| IO0 | Cycle display mode (live / track glow / chain test) | 10 s: factory reset (wipe NVS + config, keep map) |

**WIFI LED (IO25):** slow blink = connecting, solid = live data, double blink = feed error/stale, fast blink = portal active.

---

## 9. Bring-up & milestones

1. **LED bring-up:** chain-ID pattern. Each chain lights in its own colour, then a chase from index 0 confirms order and the route↔GPIO table above. (The `led_smoketest` sketch in Downloads may already cover part of this.)
2. **Network:** Wi-Fi, HTTPS fetch, nanopb stream decode, serial log of filtered vehicles. Confirm the real `route_id` strings and the feed size here.
3. **Mapping:** load `ledmap.json`, snap, render live. Tune `max_snap_m` and hysteresis.
4. **Portal/OTA/upload:** provisioning, settings, map upload, OTA.
5. **Polish:** fades, night mode, stale handling, power limit validation with a USB power meter.

**Suggested repo layout**

```
firmware/
  platformio.ini
  partitions.csv
  proto/gtfs-realtime.proto, gtfs-realtime.options
  src/  main.cpp  net_feed.cpp  gtfs_decode.cpp  snap.cpp  render.cpp
        led_out.cpp  ui.cpp  web.cpp  config.cpp
  data/ ledmap.json  config.json
```

---

## 10. Open questions

1. Confirm the exact ESP32-WROOM-32E flash size (N4/N8/N16) from the JLC order. That decides the partition table.
2. Route colours: CapMetro brand colours, or your own palette?
3. Show direction (tinted colour) or one colour per route?
4. Are you happy to run without a proxy, or would you like one for remote debugging anyway?
