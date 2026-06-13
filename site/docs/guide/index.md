# User guide

Everything past the [quick start](/quickstart) — what the dongle does,
how the apps work, and how to keep it healthy.

## How the box is laid out

The dongle hosts a small HTTP server at `http://172.16.7.1/` on its
own Wi-Fi AP. That server serves four kinds of thing:

1. **The hub dashboard** at `/` — a grid of tiles linking to each
   installed app.
2. **The web apps** at `/<app>/` — each app is a single-page
   application stored on the SD card under `/sdcard/web/<app>/`.
   Today: `ediabasx`, `inpax`, `ncsx`, `nfsx`, `tunex`.
3. **The admin page** at `/settings/` — system settings + an SD-card
   file browser for uploading SGBDs, web apps, and DATEN-disk files.
4. **The RPC endpoints** at `/rpc/...` — WebSocket APIs that the web
   apps use to drive the dongle's diagnostic engines. You won't
   touch these directly unless you're writing your own tool.

The browser is the UI. The dongle does all the actual work — running
BEST2 SGBD bytecode, framing DS2 / KWP2000 telegrams, decoding CAN.

## What's where

| Path                 | What                                            |
|----------------------|-------------------------------------------------|
| `/`                  | Hub dashboard                                   |
| `/ediabasx/`         | EDIABASX — diagnostic jobs                      |
| `/inpax/`            | INPAX — live values                             |
| `/ncsx/`             | NCSX — coding                                   |
| `/nfsx/`             | NFSX — flashing                                 |
| `/tunex/`            | TUNEX — image editing                           |
| `/settings/`            | System settings + SD-card file browser          |
| `/data/...`          | Read-only access to `/sdcard/data/` (SGBDs etc) |

The default Wi-Fi credentials (`BimmerzBox` / `bimmerzbox`) and the
admin password can all be changed in `/settings/`. See
[Connect a device](./connect) for the full setup walkthrough.

## Topics

- [**Connecting a device**](./connect) — Wi-Fi setup, captive portal,
  swapping the SSID/password, and the cable side of the OBD-II
  connection.
- [**Using the apps**](./apps) — what each tool does, when you'd
  reach for it, and how to load BMW data files.
- [**Firmware updates**](./firmware) — OTA, USB-MSC for assets, and
  how to revert.
- [**Troubleshooting**](./troubleshooting) — power, captive-portal,
  ECU comms, and getting logs out.
