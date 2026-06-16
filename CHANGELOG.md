# Changelog

All notable changes to **bimmerz-box** are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Tags are bare semver (no `v` prefix), e.g. `0.1.0`. The firmware
binary's `esp_app_desc_t::version` field is stamped from `git
describe` at build time, so on a release commit it reads `0.1.0` and
on every other commit it reads the short sha (`-dirty` if the tree
isn't clean).

## [Unreleased]

### Fixed

Correctness pass over the firmware in response to a post-0.1.0 code
review. Each fix individually links to its CR item number.

- **CAN session close UAF (`rpc_can`, CR #10).** `rx_pump_task` read
  `s->twai` outside `s_lock` and dereferenced it in
  `twai_receive_v2`; a concurrent `can.close` could call
  `twai_driver_uninstall_v2` in the same window, freeing the handle
  mid-receive. Restructured to capture the handle under the lock,
  receive outside, and join the pump task before
  `driver_uninstall` fires.
- **DNS server teardown UAF (`dns_server`, CR #7).** The
  captive-portal task self-deleted via `vTaskDelete(NULL)` while
  `stop_dns_server` `vTaskDelete`'d the same handle and freed the
  struct; in the wrong interleave the task accessed freed memory on
  the way out. Replaced with signal-and-join via an exit semaphore
  plus a 500 ms `SO_RCVTIMEO` so the loop notices shutdown promptly.
- **DNS wildcard entry deref (`dns_server`, CR #5).** The IP
  fallback at `parse_dns_request` indexed `h->entry->ip.addr`
  (always entry 0) instead of `h->entry[i].ip.addr`. Latent — only
  fires when a second wildcard rule is registered.
- **DNS multi-question handling (`dns_server`, CR #6).** The
  per-question loop never advanced `cur_qd_ptr` / `cur_ans_ptr`,
  so `qd_count > 1` would re-parse question 0 and overwrite
  answer 0. Reject upfront — captive-portal clients always send
  exactly one question anyway.
- **Boot resilience (`app_main`, CR #11).** A single transient
  ESP-Hosted SDIO glitch on cold boot used to bootloop the dongle
  because every stage went through `ESP_ERROR_CHECK`. Tiered
  handling now: essentials still panic, network bring-up (C6 link,
  Wi-Fi AP) retries 5× with linear backoff, and auxiliary stages
  (ediabasx VM, JSON-RPC routes, settings UI, OTA register, USB
  host) log-and-continue. Documented in the file header.

### Security

- **Captive-accept ring lock (`http_static`, CR #8).**
  `s_accepted_ips`, `s_accepted_next` and `s_accepted_count` were
  mutated by HTTP workers with no synchronisation. esp_http_server
  is single-threaded today so the race never fires in practice, but
  a later config change (bumped worker count, off-task checks)
  would corrupt the ring. Wrapped with `portMUX_TYPE`.
- **Reject `%` in app/dashboard paths (`http_static`, CR #12).**
  `uri_is_safe()` did substring checks for `/../` etc. — correct
  only as long as esp_http_server doesn't URL-decode `req->uri`
  before the handler sees it. Reject any `%` outright so the
  substring scan stays honest under future IDF changes. App and
  dashboard URIs never contain `%` literals in practice.

### Deferred (not in this pre-release window)

- **`transport_kline` static state under a mutex (CR #9).** The
  global `s_session` / `s_last_response_ms` would race the moment
  VM work moves off the httpd worker thread. Fix belongs together
  with the dedicated-VM-task migration documented in
  [`docs/firmware.md`](docs/firmware.md) §17 — same threading-model
  change, lands in the same commit series.

## [0.1.0] — 2026-06-15

Initial public release. The Waveshare ESP32-P4 Module DEV-KIT runs
the full diagnostic stack end-to-end against real K-line and CAN
ECUs; the WiFi6 Devkit variant has a board overlay ready for when
hardware lands; the custom dongle PCB is in schematic-capture phase.

### Hardware

- KiCad project for the custom dongle PCB under [`hardware/`](hardware/)
  with the `Various.pretty` footprint library.
- Three target board variants supported by the firmware:
  - [Waveshare ESP32-P4 Module DEV-KIT](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT)
    (phase 1, bench) — SoM + carrier with onboard C6, SD, Ethernet PHY.
  - [Waveshare ESP32-P4-WiFi6 Devkit](https://docs.waveshare.com/ESP32-P4-WIFI6)
    (phase 2) — single-board variant with same SDIO + SD pinmap, no
    Ethernet PHY.
  - Custom Bimmerz Box dongle PCB (phase 3, fab pending).

### Firmware

- ESP-IDF v5.4.4 project targeting ESP32-P4 with ESP32-C6 as a
  Wi-Fi+BLE co-processor via ESP-Hosted over SDIO Slot 1 (4-bit, 40 MHz).
- Pulls [`ediabasx-embedded`](https://github.com/emdzej/ediabasx-embedded)
  `0.1.0` as a sibling-repo path dependency — full BMW BEST/2
  interpreter + Ediabas wrapper.
- **JSON-RPC 2.0 over WebSocket** at:
  - `/rpc/ediabasx` — diagnostic engine. Wire-compatible with the TS
    server in `~/Projects/my/ediabasx/`, including:
    - CP1252 ↔ UTF-8 transcoding both inbound (args back to the SGBD)
      and outbound (results to the browser), with same-value codepoint
      mapping for undefined CP1252 slots so bytes like `0x81` (the
      `C_FA_LESEN` loop-3 counter) survive round-tripping.
    - `apiJobData` binary-payload channel (`params: [..., {binary:
      "<base64>"}]`) decoded into the VM's binary buffer.
  - `/rpc/uart/<idx>` — raw UART bridge for FTDI-style passthrough.
  - `/rpc/can/<idx>` — TWAI/CAN frame pipe (channels `0` and `1`).
- **Settings UI** at `/settings/` (in-flash, embedded HTML):
  - System status (`/api/info`) — firmware version, chip ID via the
    C6's actual SoftAP MAC, uptime, free heap.
  - Wi-Fi access-point config (SSID / password / channel / AP IP).
  - Ethernet config (off / DHCP / static).
  - SD-card file browser with breadcrumb navigation, upload, recursive
    delete, mkdir.
  - **Firmware OTA** — file picker + XHR-driven progress bar; streams
    `.bin` to `POST /settings/ota/firmware`, validates the ESP image
    header on the first chunk, flips the OTA partition, reboots.
  - **Upload & extract zip** — browser-side decompression via embedded
    `fflate.min.js` so bulk uploads (dashboard build, SGBD packs) land
    in one click.
- **OTA with rollback** (`ota_manager`). After a successful flash, the
  new image boots `PENDING_VERIFY`. A 60 s `esp_timer` grace window
  marks it valid only if the device stays up. Any panic / WDT reset
  inside the window triggers the bootloader to revert to the previous
  slot — bad uploads can't brick the device without USB-JTAG.
- **Dual 7 MB OTA partitions** + dedicated 1 MB `c6_fw` slot for the
  future C6 firmware OTA channel.
- **Captive portal** on first connect — DNS server hijacks every
  lookup to `172.16.7.1`, welcome screen with manual-dismiss UX.
- **File API** (`/api/files`, `/api/files/upload`, `/api/files/raw`,
  `/api/files/mkdir`) backed by FATFS on the SD card. Recursive delete
  for non-empty directories (e.g. `System Volume Information` from
  Windows mounts).
- **SD layout** split into `/sdcard/sys/` (dashboard), `/sdcard/apps/`
  (auto-discovered SPAs), `/sdcard/data/` (DATEN-disk SGBDs).

### Dashboard

- Svelte 5 launcher served at `/` from `/sdcard/sys/dashboard/`.
- Auto-discovers apps from `/sdcard/apps/` via `/api/files`; each app
  exposes a `manifest.json` for tile metadata.
- Splits tile name into stem + accent for the BMW M-tricolour styling
  (default: trailing `X` is the accent; manifest can override).
- Gear icon in the header → `/settings/`.
- BMW M-tricolour palette + 3-band M stripe to match `bimmerz.app`.

### Site (box.bimmerz.app)

- VitePress source under [`site/`](site/), published by
  `.github/workflows/site-deploy.yml`.
- Hero, quickstart, connect / firmware / troubleshooting / apps user
  guides, DIY paths (modules + custom PCB), product pages.
- E46 CAN-on-OBD-II tap-from-IKE wiring note (Cluster `X11175` →
  OBD-II `X19527`, pins 9/10 → 6/14) with the topology + termination
  caveat, plus a parts list (BMW p/n `61138364566` / TE/AMP
  `0-1393431-1` wire taps).

### Continuous integration

- **`firmware-build.yml`** — matrix build on
  `waveshare_p4_module_dev_kit` × `waveshare_p4_wifi6` × `dongle`,
  ESP-IDF v5.4.4. Per-board, version-stamped `.bin` artefacts uploaded
  on every push / PR. On a GitHub Release `published`, the artefacts
  are attached to the release. Sibling `ediabasx-embedded` pinned to
  `0.1.0`, overridable per-run via `workflow_dispatch` input.
- **`site-deploy.yml`** — builds the VitePress site and publishes to
  GitHub Pages.

### Documentation

- [`README.md`](README.md) at the repo root: what's inside, repo
  layout, firmware + dashboard build instructions, OTA / SD-content
  update paths, status callout, sibling-repo links.
- [`docs/firmware.md`](docs/firmware.md) — firmware architecture
  (boot sequence, task model, memory layout, partition table, network
  surface, JSON-RPC server, ediabasx integration, OBD HAL, transport
  components, OTA channels, build configuration, plus the §17 future
  plan for the dedicated-VM-task migration).
- [`docs/hardware.md`](docs/hardware.md) — hardware spec for the
  custom PCB.
- [`docs/api.md`](docs/api.md) — full JSON-RPC method reference.
- [`docs/dev-board-pinout.md`](docs/dev-board-pinout.md) — every GPIO
  the firmware uses on the Module DEV-KIT, with strapping-pin
  warnings + cross-reference to the WiFi6 + dongle variants.

### Known limitations

Documented in-line so callers can decide whether 0.1.0 fits their
threat model and use case. None are blockers for hobby / personal
diagnostic work; all are tracked for follow-up.

- **No authentication on `/settings/*` or `/api/*`.** Anyone on the
  AP can flash firmware, wipe NVS, or read/write the SD card. Treat
  AP Wi-Fi access as physical access until a credential gate lands.
  See [`docs/firmware.md`](docs/firmware.md) §11.2.
- **No OTA image signature verification.** `esp_ota_write` validates
  the ESP image checksum but not provenance. Mitigation path is
  Secure Boot v2 + signed apps; pre-condition is settling on a key
  escrow story.
- **No cooperative break / cancel** for in-flight diagnostic jobs.
  WS disconnect mid-`C_FA_LESEN` lets the VM run the job to
  completion (no leak, just wasted work). Upstream gap in
  ediabasx-embedded; tracked there.
- **VM runs inline on the httpd worker thread.** Both cores are
  available and FreeRTOS is SMP, but the documented dedicated-VM-task
  scheme isn't yet implemented — long jobs block other RPCs.
  Migration plan in [`docs/firmware.md`](docs/firmware.md) §17.
- **C6 firmware OTA** and **asset-bundle OTA** are not yet
  implemented. Partition layout reserves space; today the C6 firmware
  comes preflashed on the Waveshare board, and asset updates are
  per-file via the file API.

### Licensing

- [PolyForm Noncommercial 1.0.0](LICENSE) — free for personal,
  research, hobby use. Commercial use requires a separate licence.
- `.github/FUNDING.yml` — GitHub Sponsors + Buy Me a Coffee.

[Unreleased]: https://github.com/emdzej/bimmerz-box/compare/0.1.0...HEAD
[0.1.0]: https://github.com/emdzej/bimmerz-box/releases/tag/0.1.0
