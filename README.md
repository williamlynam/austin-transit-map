# Austin Live Transit Map

A PCB map of Austin with 197 addressable LEDs that shows where CapMetro's
Red Line train and the 801, 803, 837 and 800 buses are right now, from the
live GTFS-realtime feed.

| Folder | What it is |
|---|---|
| [`firmware/`](firmware/) | ESP32 firmware (PlatformIO). Start with its README. |
| [`led-mapper/`](led-mapper/) | Browser tool for placing every LED on the real map and exporting `ledmap.json`. |
| [`live-preview/`](live-preview/) | Browser tool that runs a `ledmap.json` against the live feed and shows which LEDs light. |
| [`firmware-design/`](firmware-design/) | The original firmware design document. |

Ready-made firmware images (`transitmap-X-ota.bin` for the web page's
*Firmware update*, `transitmap-X-factory.bin` and `littlefs-map.bin` for a
fresh board) are attached to each [release](../../releases) instead of
being kept in `firmware/prebuilt/`.
