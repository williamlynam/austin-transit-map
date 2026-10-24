# ESP32 firmware: plan (rev 2, with Will's answers)

Status: agreed by Will 2026-10-06; firmware being written. Only the core pieces are drafted (map loader, LED matching, feed decoder), and they will be adjusted to this plan. Nothing has been built or flashed, and the KiCad folder hasn't been changed.

## Decisions from Will (2026-10-06)

- Flash: 4 MB (two 1.5 MB app slots for OTA, about 900 KB for files).
- Colours: CapMetro brand colours, one colour per route. There's no direction tint.
- D10 and D23: the train always wins. A 550 train on the LED shows Red Line colour. A bus shows only when no train is there.
- No track glow mode.
- Live is the default. Simulated feed and chain test are chosen on the web page, not with a button.
- Simulated feed will grow later (timetables, so it's roughly realistic). It's built as a swappable "vehicle source" so that can be added without touching the rest.
- Polling: every 30 s by default, adjustable from 30 to 60 s.
- MODE button: short press cycles three brightness levels. Long press turns the setup hotspot on or off.
- The board runs normally with the hotspot off once it's on your Wi-Fi.
- Home Assistant: to explore. Not in the first version, but the code leaves room for it.

## Lessons from the previous attempt, and how this plan handles them

| Lesson | In this firmware |
|---|---|
| KiCad position export is Y-up, board file is Y-down | Firmware never uses board mm, only lat/lon from the LED Mapper. Nothing to flip. |
| Artwork isn't a map; no transform fits it | No fitting anywhere. Every LED's lat/lon comes from the file. |
| Strand order isn't route order (801: D112 → D23 → D113) | Matching walks each route's `paths[].seq` exactly as written. It never uses chain order when paths exist. |
| Projecting LEDs independently scrambles a loop | Matching runs over consecutive pairs in path order and never re-sorts. Each 837 direction is its own path. |
| Direction must beat distance | Direction picks the path (direction_id, then bearing). Distance only picks the LED along that path. With no direction, a bus on the 837 loop stays dark rather than being guessed onto the nearer street. |
| ESP32 is float32 end to end; feed lat/lon are float32 | Matching uses float32 on the board. The PC test accepts the ~0.6 m difference from the preview's double maths and only flags real LED disagreements. |
| `WebServer` has no `keepAlive()` | Not used. |
| A feed status code proves the path works; HTTP failures must never restart | Network errors only back off and retry, and never reboot. The only reboot is when free memory is too low to open a TLS connection. |
| Connect budget covers DNS + TCP + TLS together; 6 s was too small | 15 s connect budget, 20 s to read the body. Both are settings. |

## Features

**Feed**
- Polls CapMetro VehiclePositions (protobuf, streamed so feed size doesn't matter). The feed URL is a setting.
- Protobuf only; the JSON and proxy formats were removed in 0.1.7 to save memory.
- On error, backs off 30 → 60 → 120 s and keeps retrying. Skips decoding when the feed's timestamp hasn't changed.

**Display**
- One brand colour per route. The colours are read from ledmap.json, which the LED Mapper fills from GTFS `routes.txt` route_color. They can be overridden on the web page.
- Vehicles jump straight to each new LED (Will, 2026-10-07: no fading).
- Two vehicles of the same route on one LED make it brighter.
- On shared LEDs the train shows on top.
- Vehicles older than 2 minutes are dropped. If the whole feed is stale, the map goes dark.
- Night mode is 15% brightness from 22:00 to 06:00 (Austin time from NTP), adjustable on the web page.
- The power limit defaults to 1500 mA.

**Modes** (chosen on the web page, and kept after a reboot)
1. Live (default)
2. Simulated feed: made-up vehicles drive every route using the map's coordinates. They're encoded as a real GTFS-rt feed and go through the same decoder and matcher. It needs no Wi-Fi and exercises D10, D23 and the 837 loop. Timetables can be added later.
3. Chain test: each chain lights in its own colour with a chase from LED 0, to check wiring.

**Buttons and status LED**
- MODE (IO26): a short press cycles low, medium and high brightness. Holding it 3 s turns the hotspot on or off.
- IO0: holding 10 s does a factory reset (forgets Wi-Fi and settings, keeps the LED map). Short press does nothing.
- Wi-Fi LED (Will, 2026-10-06): blinks while the hotspot is on, off when not connected to Wi-Fi, solid when connected. Hotspot blinking takes priority.

**Wi-Fi and hotspot**
- With no Wi-Fi saved, the hotspot "TransitMap-XXXX" turns on by itself and serves a page to enter your network.
- (Changed 2026-10-06) The hotspot stays on after the board joins Wi-Fi. Only a MODE long press turns it off, and it turns itself on again if Wi-Fi drops for 5 minutes. The settings page is at http://transitmap.local on your network.
- A long press on MODE turns the hotspot on for setup without disconnecting from Wi-Fi.

**Web page**
- Status: vehicles per route, last feed time, free memory, Wi-Fi signal.
- Settings: brightness levels, night schedule, power limit, colours, poll interval, feed URL, mode.
- Upload a new ledmap.json.
- Over-the-air firmware update.

**Home Assistant (later)**
- Most likely path: MQTT with Home Assistant discovery. The map would appear as a light (on/off, brightness) with a mode selector and sensors such as vehicles per route.
- That needs an MQTT broker, such as the Mosquitto add-on. Not built in v1.

**Build and test**
- PlatformIO, Arduino framework on ESP32 core 3.x, one RMT channel sending the 5 chains in turn (0.1.11; FastLED only for colour maths).
- A PC test runs the simulated feed through the firmware's own matching code and compares the result with the live preview tool.

## Settled (2026-10-06)

1. The hotspot is open, with no password.
2. The hotspot turns itself back on if the saved Wi-Fi is unreachable for 5 minutes.
3. Colours default to CapMetro brand colours from the map file. Each route also gets a colour picker on the web page, and a manual choice overrides the brand colour.
