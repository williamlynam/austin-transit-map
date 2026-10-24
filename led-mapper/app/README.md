# LED Mapper

A companion tool for the Live PCB Map of Austin. You place every board LED on a
real map of Austin, one route at a time, and it saves the `ledmap.json` that
the ESP32 firmware loads (format: `FIRMWARE_DESIGN.md` §7).

## Run it

Open `index.html` in Chrome, Edge or Firefox. There's nothing to install. You
need internet access for the street map tiles; everything else is in this folder.

The map uses Esri street, light-gray and satellite tiles by default (switch
them with the layers button, top right). OpenStreetMap's servers refuse pages
opened from a file, and CARTO needs a key. To use CARTO, paste your key under
**Map tiles** in the sidebar and click **Use CARTO**. The same box takes any other
`{z}/{x}/{y}` tile URL. Keys are stored only in this browser and never go
into ledmap.json.

## Load the transit data

Download CapMetro's static GTFS zip from
<https://data.texas.gov/Transportation/CapMetro-GTFS/r4v4-vz24>, then click
**Load GTFS zip**. The tool keeps only the board's routes (550, 801, 803, 837
and 800). It draws their lines and their bus and rail stations with names, and
remembers them in this browser so you only load the zip once.

## Place the LEDs

1. Pick a route on the left. The current LED is ringed in yellow on the board.
2. Click its spot on the map. The tool places it and moves on to the next LED
   in that route's sequence.
   - A click near one of this route's stations snaps to the station and records
     its name and stop_id.
   - A click near the route line snaps onto the line.
   - **Shift-click** places the LED exactly where you click.
   - An LED that's only on one direction's path (the 837 loop) only snaps to
     that direction's street and stops, even when the other street is closer.
3. To fix one, click the LED on the board or in the Sequence list and click
   again on the map, or drag its marker. **Clear** removes a position.

### Space evenly

To fill a stretch quickly, place the LEDs at both ends, then use **Space
evenly**: pick the **From** and **To** LEDs and every LED between them is placed
at equal distances along the route line. When the current LED isn't placed yet,
From and To default to the nearest placed LEDs on either side of it. On the 837
loop, the order picks the side: D135 → D129 fills D123–D128 (Southbound), and
D129 → D135 fills D130–D134 (Northbound). Set the 837 directions first so the
right GTFS shape is used; a loop fill won't run until they're set. Without GTFS, the LEDs go on a straight line.

**Undo** (or Ctrl+Z) reverses the last change, including a whole fill.

Keyboard: `→`/`←` next/previous, `N` next unplaced, `Delete` clear, `T` test mode, `Ctrl+Z` undo.

Your work autosaves in this browser. **Save ledmap.json** downloads the file
for the firmware. **Open ledmap.json** picks up from a saved file, on any computer.

## The two special cases

**Crossings (D10, D23).** These LEDs are on the 550 chain, but they also show
837 buses (D10) and 801 buses (D23). Each route keeps its own position for them:
place D10 at the rail line while mapping the 550, and again where the 837
street crosses while mapping the 837. On the board, a dashed ring marks an LED
borrowed from another chain.

**837 downtown loop.** The 837 has a Northbound path (D129, D130–D134, D135
onward) and a Southbound path (onward to D135, then D123, D124–D128, D129). The
sequence list tags the loop LEDs "Northbound only" or "Southbound only". Under
**Directions**, set which GTFS `direction_id` is Northbound and which is
Southbound. The headsigns from the GTFS file are shown to help. The firmware
only matches a bus against its own direction's path, so the two sides of the
loop never get mixed up.

## Test mode

Click **Test mode**, choose a direction and click the map. The tool tells you
which LED a vehicle there would light, using the firmware's snapping rule: the
nearest segment between consecutive LEDs on the path, within `max_snap_m`
(400 m), or `snap_fraction` (0.3) of a segment's length when that is wider. If the direction is unknown, a bus near the loop LEDs is hidden rather
than guessed.

## Files

- `index.html`, `css/`, `js/`: the tool (`model.js` holds the map format and snapping, `gtfs.js` the zip reader).
- `data/ledmap_seed.js`: the board's LEDs, chains and paths, taken from `ledmap_seed.json` in the firmware design folder.
- `data/board_render.jpg`, `data/board.js`: the board artwork and its KiCad coordinates.
- `tools/render_board.py`: makes `board_render.jpg` from a `.kicad_pcb` (Python 3 + Pillow). After a board change, run `python tools/render_board.py <board>.kicad_pcb data/board_render.jpg`. If the LEDs moved, the seed needs regenerating too.
- `vendor/`: Leaflet 1.9.4 and JSZip 3.10.1.
- `samples/`: a synthetic GTFS zip and the `ledmap.json` exported from it. These are made-up coordinates for trying the tool and testing the firmware's parser. Don't flash them as a real map.
