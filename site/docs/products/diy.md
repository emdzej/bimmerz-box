# bimmerz box — DIY

Build your own. Everything you need — schematic, PCB layout, BOM,
firmware source — is in the
[bimmerz-box repo](https://github.com/emdzej/bimmerz-box) under MIT.

## What you'll need

### Tools

- Soldering iron with a fine tip (T12-K or similar)
- Solder paste + a hot-air rework station OR a reflow oven
- Tweezers (curved + straight)
- A multimeter
- USB-C cable
- A USB-Serial cable OR Espressif debug probe (only if you don't
  trust USB-JTAG flashing on the first boot)

### Files (from the repo)

- **`hardware/BimmerzBox.kicad_sch`** — schematic
- **`hardware/BimmerzBox.kicad_pcb`** — PCB layout
- **`hardware/BimmerzBox.kicad_pro`** — KiCad project
- BOM + interactive position file: generate from KiCad
  (`File → Fabrication Outputs`)

### Parts

| Part                                  | Qty | Note                                         |
|---------------------------------------|-----|----------------------------------------------|
| Waveshare ESP32-P4-Module             | 1   | the SoM (MCU + PSRAM + flash + antenna)      |
| TJA1051T/3 (SO8)                      | 2   | CAN transceivers                             |
| L9637D (SO8)                          | 1   | K-line transceiver — VCC on 3.3 V, no level shifter |
| TH3122 (SO8)                          | 1   | IBus transceiver (optional, only if you want IBus) |
| 74LVC1T45 (SOT-23-6)                  | 1   | level shifter for the IBus 5 V ↔ 3.3 V path (the K-line doesn't need one) |
| microSD socket                        | 1   | push-push, FAT32                             |
| USB-C receptacle                      | 1   | data + power                                 |
| 16-pin OBD-II socket                  | 1   | board-mount or pigtail (your call)           |
| LM2596 (or MP1584) buck converter     | 1   | 12 V → 3.3 V; pick one rated >500 mA         |
| Passives (0805 / 0603 R + C)          | ~40 | exact list in the KiCad BOM                  |
| Status LEDs (0805)                    | 4   | power / WiFi / comm / fault                  |

Order PCBs from JLCPCB / PCBWay / Eurocircuits using the gerbers.
For one-off builds, 4-layer 1.6 mm, 1 oz copper is plenty.

## Build steps

### 1. Reflow the board

Solder paste → place parts → reflow. The Waveshare module's
castellated pads are the trickiest — make sure each pad has paste
and tack the corners first.

Inspect with a loupe before powering on. Look for solder bridges
under the QFN and on the SO8 leads.

### 2. Smoke test

Apply 12 V (current-limited to 200 mA on the bench supply). Should
draw ~80 mA at idle.

- Probe TP_3V3 — expect 3.30 V ± 5 %.
- Power LED should light.

If it draws more, kill it and re-inspect. Most likely a short under
the buck converter or a reversed cap.

### 3. Flash firmware

Plug in USB-C. The Waveshare module exposes USB-JTAG by default on
GPIO 24/25 — the dongle appears as `/dev/cu.usbmodemXXXX` (macOS /
Linux) or a COM port (Windows).

```sh
git clone https://github.com/emdzej/bimmerz-box
cd bimmerz-box/firmware
. $IDF_PATH/export.sh
idf.py set-target esp32p4
idf.py -DBOARD_VARIANT=dongle build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Should boot in 5 seconds. Look for:

```
I (3835) ediabasx_platform: K-line transport registered with VM
I (3850) jsonrpc: WS endpoint up at /rpc/ediabasx
I (3835) wifi_ap: AP up on SSID "BimmerzBox"
```

### 4. Load the SD card

Format a 4–32 GB microSD as FAT32 and copy:

```
/web/dashboard/         — the hub SPA (from bimmerz-box/dashboard/dist)
/web/ediabasx/          — EDIABASX SPA (from ediabasx repo)
/web/inpax/             — INPAX SPA (from inpax repo)
/web/ncsx/, /web/nfsx/, /web/tunex/  (optional)
/data/                  — every BMW data file the apps you've
                          installed expect to find. EDIABAS DATEN
                          disk under /data/ediabas/, NCS-Expert
                          profiles + manifests under /data/ncs/,
                          INPA scripts under /data/inpa/, and any
                          per-tool support files documented in each
                          app's repo.
```

Insert it. Reboot. Connect to the AP, follow the
[quick start](/quickstart).

## Customising

The PCB design is open — fork, add an L-line driver if you need it
for older ECUs, swap to a smaller buck, add a battery for portable
use. The firmware reads the pin map from
`firmware/components/board/include/boards/dongle.h`; change those
defines for any GPIO reassignment.

## Source

- **Schematic + PCB:** [hardware/](https://github.com/emdzej/bimmerz-box/tree/main/hardware)
- **Firmware:** [firmware/](https://github.com/emdzej/bimmerz-box/tree/main/firmware)
- **Dashboard SPA:** [dashboard/](https://github.com/emdzej/bimmerz-box/tree/main/dashboard)

License: MIT.
