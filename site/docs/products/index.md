# Get a bimmerz box

Several paths into the same hardware. All of them run identical
firmware and host the same toolkit — pick the one that fits your
time, money, and "I want to solder" budget.

## At a glance

|                                | [DIY — Module DEV-KIT](./diy-modules)                       | [DIY — WiFi6 devkit](./diy-wifi6)                                   | [DIY — custom PCB](./diy-pcb)                                       | [Ready-to-ship](./ready)                                       |
|--------------------------------|-------------------------------------------------------------|---------------------------------------------------------------------|---------------------------------------------------------------------|----------------------------------------------------------------|
| **Price**                      | TBC                                                         | TBC                                                                 | TBC                                                                 | TBA                                                            |
| **Time to first boot**         | An afternoon                                                | An afternoon                                                        | A weekend — order PCB + parts, reflow, flash                        | Plug in, done                                                  |
| **Soldering required**         | Headers + jumper wires                                      | Jumper wires only                                                   | Yes (0805 passives, SOIC/QFN ICs, the module is castellated)        | None                                                           |
| **PCB fab order**              | No                                                          | No                                                                  | Yes (JLCPCB / PCBWay)                                               | Done for you                                                   |
| **Size**                       | Stack of breakouts; 3D-printed enclosure                    | Devkit + two breakouts; small 3D-printed enclosure                  | OBD-II-dongle (~55 × 46 × 15 mm)                                    | OBD-II-dongle in a moulded case                                |
| **On-board Ethernet / DoIP**   | Yes                                                         | No                                                                  | Yes                                                                 | Yes                                                            |
| **What you get**               | Dev board + click boards + cabling + firmware build         | Single-board devkit + K-line + CAN breakouts + firmware build       | KiCad design, BOM, gerbers, firmware                                | Assembled board, case, cable, factory-flashed firmware         |
| **Customisable**               | Fully — swap any module, any GPIO                           | Fully — swap any breakout, any GPIO                                 | Fully — fork the schematic, add transceivers, change the housing    | Firmware is OTA-updatable; hardware is fixed                   |
| **Warranty**                   | None (it's your jumper wires)                               | None (it's your jumper wires)                                       | None (it's your soldering iron)                                     | Manufacturing defects covered                                  |
| **Support**                    | Community via GitHub                                        | Community via GitHub                                                | Community via GitHub                                                | Community + direct email for shipped units                     |

## Same core silicon on all paths

- **MCU:** ESP32-P4 SoC + 32 MB PSRAM + 16 MB flash. The DIY paths
  use it either as the pre-certified module on the DEV-KIT carrier,
  as a bare ESP32-P4 on the WiFi6 single-board devkit, or reflowed
  directly onto the custom PCB.
- **Wi-Fi + BLE:** ESP32-C6 co-processor over SDIO 4-bit at 40 MHz.
- **K-line:** L9637D transceiver (ISO 9141 / KWP2000 / DS2).
- **CAN:** TJA1051T transceiver(s) — one bus on the DIY paths, two
  independent buses (HS-CAN + MS-CAN) on the custom PCB / ready-to-
  ship.
- **Storage:** microSD socket (FAT32, hot-swappable).
- **Connectors:** 16-pin OBD-II to the car, USB-C for power +
  USB-MSC + flashing.

## Same firmware on all paths

The DIY and ready-to-ship images are identical. Updates roll out via
the dongle's OTA mechanism (admin page → upload `bimmerz_box.bin`).

## Pick a path

- **I want to start hacking the firmware today, keep Ethernet
  available** → [DIY — Module DEV-KIT](./diy-modules) — dev board +
  Click breakouts + jumper wires. No PCB fab.
- **I want the smallest / cheapest bench rig, no Ethernet needed** →
  [DIY — WiFi6 devkit](./diy-wifi6) — single-board devkit + two
  transceiver breakouts. No PCB fab.
- **I want to build the production hardware** →
  [DIY — custom PCB](./diy-pcb) — KiCad, BOM, gerbers, soldering
  notes.
- **I want to plug it in** → [Ready-to-ship](./ready) — pre-orders
  open when the hardware is finalised, timelines unknown for now.

The middle of the road — [DIY hub](./diy) — has all three DIY paths
side by side if you can't decide.
