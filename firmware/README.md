# Austin Live Transit Map firmware

ESP32 firmware for the Live_Map_4layers rev A board. It polls CapMetro's live GTFS-realtime vehicle feed, matches each vehicle to an LED using the LED Mapper's `ledmap.json`, and lights the 197 LEDs on the five route chains.

The design is in `../firmware-design/FIRMWARE_DESIGN.md`, as changed by Will in `FIRMWARE_PLAN.md`.

## Quick start

1. Install [PlatformIO](https://platformio.org/install) (the VS Code extension or `pip install platformio`).
2. Open this folder and plug the board in over USB-C.
3. Flash the firmware: `pio run -t upload`
4. Put the LED map on the board: `pio run -t uploadfs`. This writes `data/ledmap.json` and also wipes saved settings, though not Wi-Fi.
5. On first boot the board opens an open hotspot, **TransitMap-XXXX**. Join it from a phone, and the settings page opens (or go to http://192.168.4.1). Pick your Wi-Fi and press *Join network*.
6. Once the board is on your Wi-Fi, the settings page is also at **http://transitmap-xxxx.local**, where xxxx is the same 4 characters as the board's hotspot name (TransitMap-XXXX). Each board has its own address, so several can share one network. The hotspot stays on until you hold MODE for 3 s.

> **`data/ledmap.json` is Will's LED map** (exported from the LED Mapper) and is the template for every build. To change it, upload a new one with *Replace map file* on the web page, or copy it over `data/ledmap.json` and run `uploadfs`.

### Without PlatformIO

`prebuilt/` (in the zip) or the GitHub release has ready-made images. Flash them with esptool or https://espressif.github.io/esptool-js/:

| File | Address |
|---|---|
| `transitmap-1.0.0-factory.bin` (bootloader + partitions + app) | `0x0` |
| `littlefs-map.bin` (the LED map) | `0x390000` |

`transitmap-1.0.0-ota.bin` is the same firmware for *Firmware update* on the web page.

## Using it


**Modes**: Live (default) shows CapMetro vehicles. Simulated feed drives made-up vehicles along the map's own coordinates through the real decoder and matcher, and needs no Wi-Fi.

**Buttons**
- **MODE**: a short press cycles low, medium and high brightness. Hold it for 3 s to turn the setup hotspot on or off.
- **IO0**: hold it for 10 s for a factory reset. This forgets Wi-Fi and settings, keeps the LED map, flashes red and restarts.

**WIFI LED**: blinks while the hotspot is on, is off when not connected, and is solid when connected.

**Hotspot**: opens by itself when no Wi-Fi is saved, or when the saved Wi-Fi has been unreachable for 5 minutes. It never closes by itself. It stays on after the board joins Wi-Fi until you hold MODE for 3 s. **Complete Setup** at the bottom of the web page saves every setting and turns the hotspot off. It refuses while the board isn't on Wi-Fi, so you can't lock yourself out. **Setup flow (0.1.1, Will's approved mockup)**: joining the hotspot opens a setup page (`/setup`): pick a network, enter the password, wait on *Connecting*, then *Finish setup* turns the hotspot off. *Set up without Wi-Fi* switches the map to Simulated and keeps the hotspot on; from then on, joining the hotspot opens the settings page instead. The settings page (`/`, also http://transitmap-xxxx.local) has Brightness, Show (Live/Simulated) and Route colors, plus Wi-Fi (with the *Setup network* switch), Firmware and Advanced screens. Advanced holds brightness levels, night dimming, power limit, per-route simulation (vehicles per hour each way, mph, speed-up), feed settings, the LED map and the chain test. **Vehicles by route** (0.1.1) shows, per route, how many vehicles are on the map, hidden, off route, stale or without GPS, plus the route IDs in the feed that the map does not have. Previews: `docs/flow-preview.png`, `docs/advanced-preview.png`, `docs/vehicles-preview.png`.


**Debug build (hotspot always on)**: build the `transitmap_debug` env (no prebuilt file is shipped). It sets `HOTSPOT_ALWAYS_ON=1`, so the hotspot turns on at boot and stays on; MODE long press and Complete Setup leave it on, and the status line says "always on: debug build". To go back to normal behavior, build or upload the regular `transitmap` firmware. No code change is needed.

**Web page**:
- Status: Wi-Fi, feed health, vehicles per route (lit, hidden, off route, stale), free memory.
- Mode.
- Route colors: CapMetro brand colors from the map by default, with a picker per route to override them.
- Brightness levels, night dimming and power limit.
- Feed URL, poll interval (30–60 s) and timeouts.
- Wi-Fi.
- LED map upload and download.
- Firmware update, from a file or from the newest GitHub release (see below).

### Updates from GitHub

*Firmware → From GitHub → Check now* reads the newest release of the repo set in
*Advanced → Firmware updates* (default `williamlynam/austin-transit-map`, which
must be public). If it is newer than the installed version, *Install* downloads
its `transitmap-X-ota.bin`, checks it and restarts. The board also checks once a
day and shows *Update available* on the home page, but it never installs on its
own. Pre-releases are only offered with *Include pre-releases* on.

Releases are made by `.github/workflows/release.yml`: push a change to
`src/version.h` to `main` and GitHub builds the firmware and posts release
`v<version>` with the OTA, factory and map files (or run it from the Actions
tab). A version with a `-` (`0.2.0-rc1`) becomes a pre-release.

## How it behaves

- **Feed**: polls `data.texas.gov` VehiclePositions every 30 s by default. The protobuf is streamed through nanopb one entity at a time, so feed size doesn't matter. If the feed's timestamp hasn't changed, decoding stops after the header.
- **Errors**: network or HTTP errors back off (30, 60, 120 s) and retry. They never restart the board. The only automatic restart is when memory is too low to open a TLS connection. The connect budget is 15 s (DNS + TCP + TLS), with 20 s to read the body.
- **Matching**: the direction picks the path, and distance only picks the LED along it. `direction_id` is used first, then bearing (0–90° or 270–360° counts as Northbound). With neither, a vehicle on the 837 loop stays dark rather than being guessed onto the nearer street. Paths are walked exactly as `paths[].seq` lists them and never re-sorted. Vehicles outside their line's corridor are ignored: 400 m (`max_snap_m`), or 30% of the gap between two LEDs when that is wider (`snap_fraction`), so trains between the Red Line's far-apart LEDs aren't lost. A vehicle moves to a new LED only once it is 0.6 of a segment past the old one. Matching is float32, like the feed.
- **Colors**: one color per route.
  - On D10 and D23 the 550 train always shows over the crossing bus.
  - Two vehicles of the same route on one LED show brighter.
  - Vehicles jump straight to each new LED (no fading).
- **Stale data**: vehicles older than 2 minutes are dropped. If the feed hasn't changed for 2 minutes, the map goes dark.
- **Night**: 15% of the current brightness from 22:00 to 06:00 Austin time (from NTP). This is adjustable.
- **Power**: limited to 1500 mA at 5 V by default. The board has a 3 A PTC.

## Layout

```
platformio.ini        three builds: transitmap (live by default), transitmap_sim (simulated by default), transitmap_debug (hotspot always on)
partitions.csv        4 MB: 2 × 1.75 MB app (OTA), 384 KB LittleFS
data/ledmap.json      map uploaded by `uploadfs` (Will's LED map)
proto/                gtfs-realtime.proto + nanopb options; tools/gen_proto.sh regenerates
lib/nanopb/           nanopb 0.4.9.2 runtime + generated gtfs-realtime.pb.c/h
lib/transit_core/     hardware-free code, also compiled on a PC by the tests
  board.h               pin and chain table
  ledmap.*              ledmap.json loader and validation (shared LEDs, per-direction paths)
  engine.*              vehicle → LED matching, same rules as the live preview
  gtfs.*                GTFS-rt protobuf decode and encode (for the simulator)
  source.h, sim.*       vehicle sources; the simulator (timetables can be another source later)
  display.*             colors (fading off by default)
  rgb.*                 color helpers
src/                  ESP32 side
  main.cpp              setup, 50 Hz LED loop, buttons
  runner.*              network task: fetch/simulate → decode → match → publish
  wifi_link.*           Wi-Fi, hotspot, WIFI LED
  web.*, web_page.h     settings page and JSON API
  settings.*            /config.json
  gh_update.*           firmware updates from the newest GitHub release
  leds.*, buttons.*     LED output (one RMT channel, chains sent in turn), MODE/IO0
test/host/            PC tests
```

## Tests

`python test/host/run_tests.py [path/to/ledmap.json]` builds the core with g++ (with AddressSanitizer) and checks:
- map validation;
- the GTFS-rt encode/decode round trip;
- 400 frames of the simulated feed through the real decoder and matcher: every vehicle with a direction lights an LED on its own path, 837 buses reach D10 and 801 buses reach D23;
- the train-on-top rule and no-fade default;
- the unchanged-feed shortcut.

If Node.js is installed, it also compares every LED choice with the live preview's engine (`../live-preview`). Near-ties under 2 m, from float32 against double, are allowed. Run it again with your real map once it's exported.

## Not done yet

- Home Assistant: the likely route is MQTT with auto-discovery (a light entity plus sensors). It needs a broker.
- Timetable-driven simulation: add a new `VehicleSource` next to `SimSource`.
