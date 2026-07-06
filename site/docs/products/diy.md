# DIY — pick your path

Three ways to build a bimmerz box yourself. Same firmware, same web
apps, same ECU communication on all three — only the hardware
construction differs.

## At a glance

|                        | [Module DEV-KIT](./diy-modules)                            | [WiFi6 devkit](./diy-wifi6)                           | [Custom PCB](./diy-pcb)                       |
|------------------------|------------------------------------------------------------|-------------------------------------------------------|-----------------------------------------------|
| **Construction**       | Dev board + click / breakout boards + jumper wires         | Single-board devkit + jumpered transceiver breakouts  | Reflow your own PCB, source SMD parts         |
| **Tools**              | Soldering iron (for headers / wires)                       | Soldering iron (for jumper wires)                     | Reflow oven or hot-air station, paste, stencil|
| **Cost**               | TBC                                                        | TBC                                                   | TBC                                           |
| **Time to first boot** | A couple of hours                                          | A couple of hours                                     | A weekend                                     |
| **Size**               | Big — stack of breakouts in a 3D-printed enclosure         | Small — devkit + two breakouts fit in a matchbox      | OBD-II-dongle sized (~55 × 46 × 15 mm)        |
| **Ethernet / DoIP**    | Yes (LAN8720A on the carrier)                              | No (not broken out on the WiFi6 board)                | Yes (on the PCB)                              |
| **mikroBUS header**    | Yes (Click boards plug in)                                 | No (wire the same breakouts with jumpers)             | N/A                                           |
| **Looks like**         | A prototype                                                | A prototype                                           | A finished product                            |
| **Good for**           | Firmware hacking with DoIP experiments                     | Compact bench rig, no Ethernet needed                 | Multiple units, finished installs, gift builds|

## Where to start

- **[DIY — Module DEV-KIT](./diy-modules)** — Waveshare ESP32-P4
  Module + carrier (SoM + Ethernet PHY + mikroBUS) with MikroE clicks
  and breakouts. Fastest path if you want Ethernet / DoIP experiments
  on the bench.

- **[DIY — WiFi6 devkit](./diy-wifi6)** — Waveshare ESP32-P4-WiFi6
  Devkit (single board, on-board C6 Wi-Fi 6, no Ethernet PHY) with
  external K-line + CAN transceivers on jumpers. Smallest / cheapest
  bench setup that runs the full firmware.

- **[DIY — custom PCB](./diy-pcb)** — KiCad design files, BOM, build
  steps. Reflow your own board. Same physical format as the
  ready-to-ship hardware.

## Common to all three

- **Firmware** — same `bimmerz_box.bin` from the firmware repo. Pick
  the board overlay for your target (`waveshare_p4_module_dev_kit`,
  `waveshare_p4_wifi6`, or `dongle`); all three flash the same way.
- **Web apps** — same SD-card layout: sibling apps under
  `/sdcard/apps/<slug>/`, dashboard hub at `/sdcard/sys/dashboard/`,
  read-only vehicle data under `/sdcard/data/`.
- **OTA + updates** — same `/settings/` upload form, same USB-MSC SD
  drop-in mechanism.

## Or, skip DIY

If none of these sound appealing, [ready-to-ship](./ready) gets you
the production hardware assembled, tested, and in an enclosure.
