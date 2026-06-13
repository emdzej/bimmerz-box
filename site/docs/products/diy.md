# DIY — pick your path

Two ways to build a bimmerz box yourself. Same firmware, same web
apps, same ECU communication on both — only the hardware
construction differs.

## At a glance

|                            | [From modules](./diy-modules)                              | [Custom PCB](./diy-pcb)                       |
|----------------------------|------------------------------------------------------------|-----------------------------------------------|
| **Construction**           | Dev board + click / breakout boards + jumper wires         | Reflow your own PCB, source SMD parts         |
| **Tools**                  | Soldering iron (for headers / wires)                       | Reflow oven or hot-air station, paste, stencil|
| **Cost**                   | TBC                                                        | TBC                                           |
| **Time to first boot**     | A couple of hours                                          | A weekend                                     |
| **Size**                   | Big — stack of breakouts in a 3D-printed enclosure         | OBD-II-dongle sized (~55 × 46 × 15 mm)        |
| **Looks like**             | A prototype                                                | A finished product                            |
| **Good for**               | Firmware hacking, single units, "I want it on the bench"   | Multiple units, finished installs, gift builds|

## Where to start

- **[DIY from modules](./diy-modules)** — Waveshare ESP32-P4 dev kit +
  MikroE clicks + breakouts. No PCB fab, no SMD reflow. Fastest path
  to a working dongle on your bench.

- **[DIY custom PCB](./diy-pcb)** — KiCad design files, BOM, build
  steps. Reflow your own board. Same physical format as the
  ready-to-ship hardware.

## Common to both

- **Firmware** — same `bimmerz_box.bin` from the firmware repo.
  Build for `BOARD_VARIANT=waveshare_p4_module_dev_kit` on the modules path, or
  `BOARD_VARIANT=dongle` on the PCB path. Both flash the same way.
- **Web apps** — same SD-card layout under `/sdcard/web/<app>/`
  and `/sdcard/data/<app>/`.
- **OTA + updates** — same `/settings/` upload form, same USB-MSC SD
  drop-in mechanism.

## Or, skip DIY

If neither sounds appealing, [ready-to-ship](./ready) gets you the
production hardware assembled, tested, and in an enclosure.
