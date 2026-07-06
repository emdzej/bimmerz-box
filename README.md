# bimmerz box

A small Wi-Fi-equipped OBD-II diagnostic dongle for classic BMWs.
Plug it into the car, join its Wi-Fi access point, run the full bimmerz
toolkit (EDIABASX, INPAX, NCSX, NFSX, TUNEX) in any browser tab — no
installer, no cable on the laptop, no Windows VM.

→ **Product site & user docs:** [box.bimmerz.app](https://box.bimmerz.app)
→ **Architecture docs:** [`docs/`](docs/) (firmware, API, hardware,
  dev-board pinout)
→ **Release log:** [`CHANGELOG.md`](CHANGELOG.md). Tags are bare
  semver (no `v` prefix).

## What's inside

- **ESP32-P4** application processor with 32 MB PSRAM — runs the
  diagnostic engines, HTTP server, and JSON-RPC over WebSocket.
- **ESP32-C6** companion radio — Wi-Fi access point + BLE, talks to the
  P4 over SDIO (esp_hosted).
- **Dual TJA1051T** high-speed CAN transceivers (PT-CAN + F-CAN).
- **L9637D** K-line transceiver for older DS2 / KWP2000 ECUs.
- **SD card** holding the web apps, BMW DATEN-disk SGBDs, and trace logs.
- **Dual 7 MB OTA partitions** for live firmware updates over Wi-Fi;
  USB-MSC mode for offline SD content drop-in.

## Repository layout

| Path | What lives here |
|---|---|
| [`firmware/`](firmware/) | ESP-IDF v5.4.4 project. P4 application firmware + a tree of components (board pinmaps, JSON-RPC, CAN/K-line transports, ediabasx VM bridge, OTA manager, settings UI, file API). |
| [`dashboard/`](dashboard/) | Svelte 5 home app served at `/` from the dongle's SD card. Auto-discovers installed apps via `/api/files`. |
| [`hardware/`](hardware/) | KiCad project for the custom dongle PCB. Schematic + layout + the Various.pretty footprint library. |
| [`docs/`](docs/) | Architecture: [`firmware.md`](docs/firmware.md), [`hardware.md`](docs/hardware.md), [`api.md`](docs/api.md), [`dev-board-pinout.md`](docs/dev-board-pinout.md). |
| [`site/`](site/) | VitePress source for [box.bimmerz.app](https://box.bimmerz.app). Deployed by `.github/workflows/site-deploy.yml`. |

## Flash prebuilt binaries (from GitHub releases)

You don't need ESP-IDF to flash — releases ship board-specific artefacts on
[github.com/emdzej/bimmerz-box/releases](https://github.com/emdzej/bimmerz-box/releases).
Each release attaches four `.bin` files and a `flasher_args.json` per
board variant (`waveshare_p4_module_dev_kit`, `waveshare_p4_wifi6`,
`dongle`).

**Prerequisites** — Python 3 and `esptool`:

```sh
pip install esptool
```

**Download the four artefacts** for your board and release (example:
WiFi6 devkit, tag `0.1.0`):

```sh
V=0.1.0
BOARD=waveshare_p4_wifi6
gh release download "$V" --repo emdzej/bimmerz-box \
  --pattern "bootloader-$BOARD-$V.bin" \
  --pattern "partition-table-$BOARD-$V.bin" \
  --pattern "ota_data_initial-$BOARD-$V.bin" \
  --pattern "bimmerz_box-$BOARD-$V.bin"
```

(Or download them by hand from the release page.)

**Identify the serial port**:

- **macOS/Linux**: `ls /dev/cu.usbmodem*` (or `/dev/ttyUSB*` for
  external USB-UART adapters). The WiFi6 devkit and Module DEV-KIT
  both expose the ESP32-P4's native USB-Serial-JTAG through the
  board's USB-C connector — flash and console share that port.
- **Windows**: check *Device Manager → Ports (COM & LPT)* for the new
  entry that appears when you plug in the board.

**Flash** (offsets are fixed by the partition table, identical across
variants):

```sh
python -m esptool --chip esp32p4 -p /dev/cu.usbmodem<...> -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x2000  "bootloader-$BOARD-$V.bin" \
  0x8000  "partition-table-$BOARD-$V.bin" \
  0x10000 "ota_data_initial-$BOARD-$V.bin" \
  0x20000 "bimmerz_box-$BOARD-$V.bin"
```

The `flasher_args-$BOARD-$V.json` attached to the release contains
the same offsets — if you'd rather drive `esptool` from the JSON:

```sh
python -m esptool --chip esp32p4 -p /dev/cu.usbmodem<...> \
  write_flash "@flasher_args-$BOARD-$V.json"
```

**Verify** — join the `BimmerzBox` Wi-Fi AP (open, no password) and
open `http://172.16.7.1/settings/`. The *Firmware* card shows the
running image's version + git hash.

**Subsequent updates** — after the first flash, do OTA updates through
the settings UI (see *Updating a live dongle* below); no cable needed.

### WiFi6 devkit — physical setup

For wiring the WiFi6 devkit to an OBD-II socket (K-line, CAN, and
12 V power via a step-down converter), see
[`docs/wifi6-prototype.md`](docs/wifi6-prototype.md).

## Build the firmware

```sh
cd firmware

# Pick a board variant by layering its overlay onto sdkconfig.defaults.
#
# Phase 1, bench — Waveshare ESP32-P4 Module DEV-KIT
#   https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.waveshare_p4_module_dev_kit" \
       set-target esp32p4
idf.py build

# Phase 2, bench — Waveshare ESP32-P4-WiFi6 Devkit (single-board)
#   https://docs.waveshare.com/ESP32-P4-WIFI6
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.waveshare_p4_wifi6" \
       set-target esp32p4
idf.py build

# Phase 3, final — custom Bimmerz Box dongle PCB
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.dongle" \
       set-target esp32p4
idf.py build

idf.py -p /dev/cu.usbmodem<...> flash monitor
```

The build pulls [`ediabasx-embedded`](https://github.com/emdzej/ediabasx-embedded)
as a sibling repo via a path dependency — clone it next to bimmerz-box:

```
~/Projects/my/
├── bimmerz-box/
└── ediabasx-embedded/   ← checked out alongside
```

## Build the dashboard

```sh
cd dashboard
pnpm install
pnpm dev        # http://localhost:5180 — no dongle backend
pnpm build      # output: dist/, upload to /sdcard/sys/dashboard/
```

See [`dashboard/README.md`](dashboard/README.md) for deploy paths
(settings file browser, `/api/files/upload`, or the *Upload & extract
zip* button — browser-side unzip via embedded fflate).

## Updating a live dongle

- **Firmware OTA:** Open `http://172.16.7.1/settings/` → *Firmware
  update* → pick the `.bin` from `firmware/build/`. Streams into the
  inactive OTA slot, reboots, and auto-rolls-back if the new image
  crashes inside the first 60 s. See [`docs/firmware.md`](docs/firmware.md) §11.
- **SD-card content:** *Settings → Files* file browser, or `POST
  /api/files/upload`, or the *Upload & extract zip* button.

## CI

GitHub Actions in [`.github/workflows/`](.github/workflows/):

- **`firmware-build.yml`** — matrix build (`waveshare_p4_module_dev_kit` + `waveshare_p4_wifi6` + `dongle`) on every
  push / PR touching `firmware/`. Uploads per-board `.bin` artefacts;
  on GitHub Release `published`, attaches them to the release.
- **`site-deploy.yml`** — builds the VitePress site and publishes to
  GitHub Pages (box.bimmerz.app).

## Status

Active development. Bench target (Waveshare ESP32-P4 Module DEV-KIT) is
functional end-to-end: SoftAP comes up, dashboard serves, EDIABASX runs
real SGBD jobs against K-line and CAN ECUs, OTA flashes survive
rollback testing. The custom PCB is in schematic-capture phase — see
[`hardware/`](hardware/).

**Security note.** The dongle's HTTP surface has no authentication and
the OTA channel doesn't verify image signatures yet — anyone on the AP
can flash firmware or wipe NVS. Treat AP Wi-Fi access as physical
access until that gap is closed. See
[`docs/firmware.md`](docs/firmware.md) §11.2.

## Sibling repos

- [`emdzej/ediabasx-embedded`](https://github.com/emdzej/ediabasx-embedded)
  — C11 port of the BMW BEST/2 interpreter, pulled in via path dep.
- [`emdzej/ediabasx`](https://github.com/emdzej/ediabasx) — TypeScript
  reference implementation + server.
- Per-tool apps (`inpax`, `ncsx`, `nfsx`, `tunex`, `dashx`) live in
  their own repos; their built artefacts land on the dongle's SD card
  under `/sdcard/apps/<slug>/`.

## Right to Repair

The [Right to Repair](https://repair.eu) movement advocates for consumers' ability to fix the products they own — from electronics to vehicles — without being locked out by manufacturers through proprietary tools, paywalled documentation, or artificial restrictions.

**I build these tools because I believe repair is a fundamental right, not a privilege.**

Too often, service manuals, diagnostic software, and technical documentation are kept behind closed doors — unavailable to individuals even when they're willing to pay. This wasn't always the case. Products once shipped with schematics and repair guides as standard. The increasing complexity of modern technology doesn't change the fact that capable people exist who can — and should be allowed to — use that information.

These projects exist to preserve access to technical knowledge and ensure that owners aren't left at the mercy of vendors who may discontinue support, charge prohibitive fees, or simply refuse service.

## Support

If you find this project useful, consider [buying me a coffee](https://buymeacoffee.com/emdzej) ☕ or [sponsoring on GitHub](https://github.com/sponsors/emdzej) or if it's your thing: via PayPal

[![Donate with PayPal](https://www.paypalobjects.com/en_US/PL/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/donate/?business=TDBR3A97PLQRQ&no_recurring=0&item_name=%28emdzej%29&currency_code=PLN)

## License

[PolyForm Noncommercial 1.0.0](./LICENSE) — free for noncommercial use (personal projects, research, education, hobby diagnostics on your own car). Commercial use requires a separate licence — open an issue if you need one.

This repository contains no BMW proprietary data. All DATEN files, SGBDs, and IPOs the tools consume must come from a legally-acquired BMW Standard Tools install on the user's own machine.

## Disclaimer

This project is for educational and research purposes only. It is not affiliated with BMW AG.
