# Live Preview

Shows, in real time, which LEDs on the board CapMetro's buses and trains would
light. It loads the `ledmap.json` saved by the LED Mapper, reads CapMetro's
GTFS-realtime VehiclePositions feed, and applies the firmware's rules from
`FIRMWARE_DESIGN.md` (§4 snapping, §5 display) to every vehicle.

## Run it

CapMetro's feed doesn't allow a web page to read it directly, so a small script
fetches it for the page. You need Python 3 (python.org; nothing else to install).

- **Windows:** double-click `run-preview.bat`.
- **Anywhere:** `python serve.py` (or `py serve.py` on Windows).

Your browser opens `http://localhost:8737`. Leave the window running while you
use the page, and press Ctrl+C in it to stop. The street map tiles need internet
access, as in the LED Mapper.

Then click **Open ledmap.json** and pick the file you saved from the LED Mapper.
The page remembers it for next time. Until then it shows the synthetic sample
map, whose positions are made up.

## What you see

- **Board** (middle): the PCB with each LED lit the way the firmware would light
  it. Click an LED to see which vehicles are on it.
- **Street map** (right): each vehicle at its reported position, with an arrow
  for its bearing and a dashed line to the LED it snaps to. LEDs on the street
  map grow when lit. Grey markers are off route or stale; a dashed outline means
  hidden (see below). Hover a marker for the details.
- **Routes**: how many vehicles on each route light an LED, plus any that are
  hidden, off route or stale, and a warning if a route still has unplaced LEDs.
- **Vehicles**: every vehicle that lights an LED. `*` after an LED means it's
  borrowed from another chain (an 837 bus on D10, an 801 bus on D23). Tick
  *Include stale, off-route and hidden* to see the rest too.
- **Not on the board**: route IDs in the feed that match no route in the map.
  If a board route turns up here spelled differently (say `801-1` instead of
  `801`), the map's `route_id` needs to change to match.

## The rules it applies

These follow the firmware design, so the preview and the board should agree LED
for LED.

1. Only vehicles whose `route_id` is a route in the map are used.
2. A vehicle whose position is more than 120 s old is dropped as stale.
3. **Direction path.** A route with one path uses it. The 837 has a Northbound
   and a Southbound path; the vehicle's `direction_id` picks the path whose
   `direction_id` matches. Without a `direction_id`, the bearing decides
   (0–90° or 270–360° is Northbound). With neither, every path is tried, and a
   vehicle whose nearest LED is on only one side of the loop is **hidden**
   rather than guessed.
4. **Snapping** is the same code as the LED Mapper's test mode: the nearest
   segment between consecutive placed LEDs on that path, lighting the nearer
   end, and only within that segment's corridor: the map's `max_snap_m` (400 m), or
   `snap_fraction` (0.3) of the segment's length when that is wider, so long
   gaps between LEDs (the Red Line's 3 to 5 km ones) still catch curving track.
   Farther away is **off route**. Unplaced and skipped LEDs are left out of the path.
5. **Hysteresis**: a vehicle keeps its previous LED until its new position is
   more than 0.6 of a segment away from it, so a bus waiting between two LEDs
   doesn't flicker. You can switch it off under Display.
6. **Colour**: one colour per route (the `color` in ledmap.json), whatever the
   direction. Two vehicles of one route on an LED make it brighter. On the two
   shared LEDs (D10 and D23) the train always shows on top: a bus lights them
   only when no train is there. Changes fade over about 1.5 s.

## Other sources

- **Demo** drives made-up buses along your map's own paths at 5× speed, including
  a few awkward cases: an 837 with no `direction_id`, one with neither direction
  nor bearing, an off-route bus, a stale one and a route that isn't on the
  board. Useful before the map is finished or when the feed is down.
- **Replay files** steps through saved feeds. **Save feed** downloads the feed
  the page last received, exactly as received, and `python serve.py --record feeds`
  saves every new feed into a `feeds` folder while it runs, to replay later or
  to send along when something looks wrong.

## Settings

- The page fetches the feed every 30 s by default, like the firmware; you can set 30–60 s.
- The feed URL box takes any VehiclePositions URL, protobuf or JSON. `/feed`
  (the default) goes through `serve.py`, and `/feed.json` fetches CapMetro's
  JSON version instead.
- `serve.py --feed URL` changes the URL the script fetches, if CapMetro moves
  the feed. The default is
  `https://data.texas.gov/download/eiei-9rpf/application%2Foctet-stream`.
- `serve.py --port N` uses another port. It only listens on this computer.

## Files

- `index.html`, `css/`, `js/app.js`: the page.
- `js/feed.js`: reads the protobuf and JSON feeds (only the fields the firmware uses).
- `js/engine.js`: the firmware's rules, as listed above.
- `js/demo.js`: the made-up demo buses.
- `js/geo.js`, `js/model.js`: copies of the LED Mapper's snapping and map code, so
  both tools pick the same LED. If you change them in the mapper, copy them here too.
- `data/`: the board render and the sample map. `vendor/`: Leaflet.
- `serve.py`, `run-preview.bat`: the local page server and feed relay.
