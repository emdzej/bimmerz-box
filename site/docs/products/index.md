# Get a bimmerz box

Two ways into the same hardware. Both run the same firmware and host
the same toolkit — pick the path that fits your time vs money budget.

## At a glance

|                                | DIY                                                                 | Ready-to-ship                                                  |
|--------------------------------|---------------------------------------------------------------------|----------------------------------------------------------------|
| **Price**                      | ~€60–80 in parts (BOM)                                              | TBA (waitlist)                                                  |
| **Time to first boot**         | A weekend — order PCB + parts, solder, flash                         | Plug in, done                                                  |
| **Soldering required**         | Yes (0805 passives, SOIC/QFN ICs, the module is castellated)        | None                                                           |
| **What you get**               | KiCad design, BOM, gerbers, firmware                                | Assembled board, case, cable, factory-flashed firmware         |
| **Customisable**               | Fully — fork the schematic, add transceivers, change the housing    | Firmware is OTA-updatable; hardware is fixed                   |
| **Warranty**                   | None (it's your soldering iron)                                     | Manufacturing defects covered                                  |
| **Support**                    | Community via GitHub                                                | Community + direct email for shipped units                     |

## Same hardware on both paths

- **MCU:** Waveshare ESP32-P4 Module — ESP32-P4 SoC + 32 MB octal
  PSRAM + 16 MB flash + crystal + antenna + RF shield, all in one
  pre-certified module.
- **Wi-Fi + BLE:** ESP32-C6 co-processor, SDIO 4-bit at 40 MHz.
- **K-line:** L9637D transceiver (handles ISO 9141 / KWP2000 / DS2).
- **CAN:** dual TJA1051T transceivers — HS-CAN (PT-CAN) +
  MS-CAN (K-CAN) on independent buses.
- **Storage:** microSD socket (FAT32, hot-swappable).
- **Connectors:** 16-pin OBD-II to the car, USB-C for power +
  USB-MSC + flashing.

## Same firmware on both paths

The DIY and ready-to-ship images are identical. Updates roll out via
the dongle's OTA mechanism (admin page → upload `bimmerz_box.bin`).

## Pick a path

- **I want to build it** → [DIY](./diy) — KiCad, BOM, soldering
  notes, where to source parts.
- **I want to plug it in** → [Ready-to-ship](./ready) — waitlist,
  delivery, pre-flashed configuration.
