---
layout: home

hero:
  name: bimmerz box
  text: Your BMW's diagnostic toolkit, on a Wi-Fi dongle.
  tagline: Plug into the OBD-II port, connect your phone or laptop to its Wi-Fi, run EDIABASX / INPAX / NCSX / NFSX in a browser tab. No installer, no Windows VM, no cable on your laptop.
  image:
    src: /icon.svg
    alt: bimmerz box
  actions:
    - theme: brand
      text: Quick start
      link: /quickstart
    - theme: alt
      text: Get one
      link: /products/

features:
  - title: One dongle, every protocol
    details: K-line (ISO 9141 / KWP2000), DS2, dual high-speed CAN, IBus. Switch between them in the dashboard.
  - title: Browser-native
    details: The dongle hosts its own web apps from an SD card. Any modern browser on phone, tablet, or laptop is the UI.
  - title: Wi-Fi access point
    details: No pairing, no Bluetooth profile drama. The dongle exposes its own AP and a captive portal that drops you straight into the dashboard.
  - title: Open hardware + firmware
    details: ESP32-P4 module, KiCad schematic, all source under MIT. Build your own, modify ours, or buy a ready-to-ship unit.
  - title: All the bimmerz tools
    details: EDIABASX (diagnostic jobs), INPAX (live values), NCSX (coding), NFSX (flashing), TUNEX (tuning) — all run against the same dongle.
  - title: Updates over the air
    details: Dual 7 MB OTA partitions on the device, USB-MSC asset drop-in for the SD card. Push a firmware build, refresh the dashboard, done.
---

## What's in the box

bimmerz box is a small Wi-Fi-equipped OBD-II dongle. It plugs into the
car's diagnostic port, brings up its own Wi-Fi access point, and serves
the full bimmerz toolkit — EDIABASX, INPAX, NCSX, NFSX, TUNEX — as web
apps you load in any browser tab.

Inside it's an **ESP32-P4** application processor with 32 MB PSRAM
(handling the diagnostic engines and the HTTP / WebSocket server), an
**ESP32-C6** co-processor for Wi-Fi + BLE, **dual high-speed CAN**
transceivers (TJA1051T), a **K-line transceiver** (L9637D) for older DS2
and KWP2000 ECUs, and an **SD card** holding the web apps + your BMW
DATEN-disk SGBDs.

## How it works

1. **Plug it in.** OBD-II port, ignition on (or running).
2. **Join its Wi-Fi.** Network name `BimmerzBox`, default password
   `bimmerzbox`. A captive-portal window pops up with a welcome screen.
3. **Open the dashboard.** Browser → `http://172.16.7.1/`. You see a
   tile for each installed app.
4. **Pick a tool.** EDIABASX for fault codes / jobs, INPAX for live
   values, NCSX for coding, NFSX for flashing, TUNEX for editing
   firmware images.

That's the whole user-facing model. The dongle does the heavy lifting
(running BEST2 SGBD bytecode, framing DS2 / KWP2000 telegrams, decoding
CAN frames); the browser is just a screen.

## Three ways to get one

- **[DIY from modules](./products/diy-modules)** — Waveshare ESP32-P4
  dev kit + MikroE click breakouts + jumper wires. No PCB fab, no SMD
  reflow. Up and running in an afternoon. ~€100 in modules.
- **[DIY custom PCB](./products/diy-pcb)** — KiCad design + firmware
  source, all open. Reflow your own board for an OBD-II-sized finished
  unit. ~€60–80 in parts.
- **[Ready-to-ship](./products/ready)** — assembled, tested, comes with
  a case and a labelled cable. Plug-and-play.

All three land you with the same firmware, same web apps, same ECU
communication. They differ only in how the hardware is put together.

## Status

Active development. The Waveshare ESP32-P4 Module DEV-KIT is the
canonical dev target — the firmware boots end-to-end against it. The
custom PCB is in schematic capture. EDIABASX + INPAX have been
verified against a real BMW MS42.0 DME over K-line; CAN is wired in
firmware and waits on the production PCB.

Follow along at [github.com/emdzej/bimmerz-box](https://github.com/emdzej/bimmerz-box).
