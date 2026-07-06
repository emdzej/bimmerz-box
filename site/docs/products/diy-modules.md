# DIY — Module DEV-KIT

The lazy-but-still-DIY path. Stack ready-made dev boards and
breakouts, wire them together, flash the same firmware that ships
on the production unit. No PCB fab, no SMD reflow, no oscilloscope
required.

Tradeoffs vs the [custom PCB build](./diy-pcb): bigger (a stack of
breakouts won't fit in an OBD-II-sized enclosure), uglier, more
wire-routing to think about. Same firmware, same web apps, same
ECU communication — so if you don't mind it living in a 3D-printed
hobby box on the passenger floor, this is the path.

::: tip Two supported dev boards
This page covers the **Waveshare ESP32-P4 Module DEV-KIT** (SoM +
carrier, on-board Ethernet PHY, mikroBUS header). If you prefer the
smaller single-board **[Waveshare ESP32-P4-WiFi6 Devkit](./diy-wifi6)**
(no Ethernet PHY, no mikroBUS, but on-board C6 Wi-Fi 6) — the firmware
supports both as parallel targets. See the
[WiFi6 devkit DIY page](./diy-wifi6) for that variant.
:::

::: warning Draft
The exact parts list is still being finalised. Treat the table
below as the **shape** rather than a verified BOM — confirm pin
mappings against the latest `firmware/components/board/include/boards/waveshare_p4_module_dev_kit.h`
before you wire anything up.
:::

## What you'll need

### The brain

**Waveshare ESP32-P4 Module DEV-KIT** — the canonical development
target the firmware is written against. The Waveshare ESP32-P4
Module on a breakout board with USB-C, microSD, RJ45, headers
broken out.

- [Waveshare product page](https://www.waveshare.com/esp32-p4-module-dev-kit.htm)
  · [official docs](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT)
- Includes a mikroBUS-style header for click boards.
- The sibling supported board is the
  [Waveshare ESP32-P4-WiFi6 Devkit](https://docs.waveshare.com/ESP32-P4-WIFI6)
  (single-board variant; same C6 + SD pinmap, no Ethernet PHY). The
  firmware ships a board overlay for it too — see
  `sdkconfig.defaults.waveshare_p4_wifi6` and the
  [WiFi6 devkit DIY page](./diy-wifi6).

### K-line transceiver

**MikroE ISO 9141 Click** — the **L9637D013TR** breakout board (ST
L9637D in SO-8 automotive-grade, on a mikroBUS carrier). Drops
straight into the dev kit's mikroBUS header. Set its jumper to
**3.3 V** (the L9637D is happy at 3.3 V, no level shifter needed).

- [MikroE product page — ISO 9141 Click (L9637D013TR)](https://www.mikroe.com/iso-9141-click)

For older BMW chassis you'll also want a **20-pin-to-OBD-II adapter
cable** so you can reach the under-bonnet diagnostic socket.

### CAN transceivers (×2)

Anything with a **TJA1051T/3** or compatible CAN transceiver on a
breakout. Two boards — one per CAN bus the dongle exposes.

Options (any of these work):

- **MikroE CAN FD Click** (uses MCP2542FD — also fine for classical CAN at the rates we use)
- **MikroE CAN Isolator Click** (galvanically isolated — overkill but nice for bench work)
- **Any generic TJA1051T breakout from AliExpress / Adafruit** with a 3.3 V logic side

You'll wire TX / RX / STBY from the dev board's GPIO header to the
breakout's logic side. Confirm the breakout's logic voltage matches
3.3 V before connecting.

### Storage

- **microSD card** — 16 GB SDHC, FAT32. The dev kit has the slot.

### OBD-II cable

- **OBD-II to bare wires** (or to your screw-terminal block of
  choice). Cheap on AliExpress. You'll wire pin 6 / 14 to one CAN
  click, pin 7 to the K-line click, pin 16 (battery +12 V) to a
  buck converter (or USB-C PD trigger) feeding the dev kit.

### Power

The OBD-II port provides permanent 12 V on pin 16. Two ways to get
it to the dev kit:

- **Buck converter** — any 12 V → 5 V module rated ≥1 A. Feed the
  dev kit's USB-C with the regulated 5 V via a USB-C breakout or a
  bare cable.
- **USB-C PD trigger board** that takes 12 V in directly (not all
  do — check the datasheet) and outputs 5 V on the USB-C side.

Bench testing — skip the OBD-II side, plug the dev kit straight
into your laptop's USB-C and use the cable's 5 V.

### Optional

- **Status LEDs + dropping resistors** if you want the four-LED
  panel the production hardware will have. Not required for
  functionality.
- **Small enclosure / 3D-printed box** to keep the stack tidy.

## Wiring

The full pin map for the dev kit lives in
[`docs/dev-board-pinout.md`](https://github.com/emdzej/bimmerz-box/blob/main/docs/dev-board-pinout.md).
The bits you'll wire on the modules path:

| Breakout              | Breakout pin | Dev kit GPIO | Notes                              |
|-----------------------|--------------|--------------|------------------------------------|
| ISO 9141 Click (K-line)| TX           | GPIO 5       | UART1 TX                           |
| ISO 9141 Click        | RX           | GPIO 4       | UART1 RX                           |
| ISO 9141 Click        | K (bus)      | OBD-II pin 7 |                                    |
| ISO 9141 Click        | VCC          | 3V3          | **3.3 V jumper on the click**      |
| CAN breakout #0       | TX           | (TBD — currently `(-1)` on waveshare_p4_module_dev_kit.h) | mikroBUS / GPIO header pin |
| CAN breakout #0       | RX           | (TBD)        |                                    |
| CAN breakout #0       | STBY         | (TBD)        | Drive low for normal mode          |
| CAN breakout #0       | CAN-H / L    | OBD-II 6 / 14 |                                   |
| CAN breakout #1       | (same)       | (TBD)        | Second bus — pin allocation TBC    |

The CAN GPIO assignments on the **dev board** are currently `(-1)`
in `waveshare_p4_module_dev_kit.h` because there's no on-board transceiver. If you
wire up CAN breakouts you'll edit `waveshare_p4_module_dev_kit.h` to point to whichever
free GPIOs you've cabled, then re-flash. See
[Free GPIOs on this board](https://github.com/emdzej/bimmerz-box/blob/main/docs/dev-board-pinout.md#free-gpios-on-this-board)
for the safe pin list.

## Flash + boot

Same as the custom PCB build:

```sh
git clone https://github.com/emdzej/bimmerz-box
cd bimmerz-box/firmware
. $IDF_PATH/export.sh
idf.py set-target esp32p4
idf.py -DBOARD_VARIANT=waveshare_p4_module_dev_kit build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Note the `BOARD_VARIANT=waveshare_p4_module_dev_kit` — that selects the dev-board pin
map. If you've moved CAN pins to non-default GPIOs, hand-edit
`waveshare_p4_module_dev_kit.h` first.

Once flashed, follow the [quick start](/quickstart) — the box hosts
the same Wi-Fi AP and the same web apps regardless of how its
hardware is laid out.

## Cost

**TBC.** A finalised module list with verified prices will land
once the bench-tested combinations are pinned down. As a rough
sanity check the whole stack should come in under what the custom
PCB build costs — you skip the PCB fab cycle, the SMD parts, and
the soldering-station investment, paying instead for the markup on
pre-assembled modules.

## When to pick this path

- You want to **start hacking the firmware** without waiting for a
  PCB fab cycle.
- You're prototyping a **new transceiver or sensor** before
  committing to a board respin.
- You **don't have an SMD soldering setup** and don't want one.
- You're building **one or two** units, not running a small batch.

If you're building three or more, or want anything resembling a
finished product, the [custom PCB path](./diy-pcb) is cheaper per
unit and looks like a real device.
