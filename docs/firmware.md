# Bimmerz Box — Firmware Architecture

## 1. Overview

The dongle runs ESP-IDF on the ESP32-P4 application processor with the
ESP32-C6 attached as a Wi-Fi/BLE co-processor over SDIO. The P4 hosts:

- The native C port of ediabasx (the diagnostic engine).
- A JSON-RPC 2.0 server over WebSocket, byte-compatible with the existing
  TypeScript server in `~/Projects/my/ediabasx/packages/ediabasx-server/`.
- An HTTP static server that delivers the inpax / ncsx / nfsx / tunex /
  ediabasx single-page apps from the SD card.
- HAL layers for K-line, L-line, CAN, IBUS, and DoIP transports.
- OTA managers for P4 firmware, C6 firmware, and asset payloads.

### 1.1 Bring-up sequence

Phase 1 — **[Waveshare ESP32-P4 Module DEV-KIT](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT)**
(SoM + carrier). P4 + C6 + SD + Ethernet PHY all present, so the firmware
comes up to a fully functioning HTTP + WS server before any custom
hardware exists. Diagnostic transceivers (L9637D, TJA1051T, TH3122…)
get wired up on breakout boards / mikroBUS clicks off this board for
validation against real ECUs.

Phase 2 — **[Waveshare ESP32-P4-WiFi6 Devkit](https://docs.waveshare.com/ESP32-P4-WIFI6)**
(single-board variant). Same C6 + SD pin map as the Module DEV-KIT, no
separate carrier required. No Ethernet PHY (LCD-derived design).
Closer to the final dongle's form factor — useful for verifying the
firmware works on a single-board target before the custom PCB lands.

Phase 3 — **Custom Bimmerz Box dongle PCB.** No firmware surprises
expected — same peripherals, same drivers, just rerouted GPIO
assignments via a board support definition.

All three variants are selectable at build time via a Kconfig choice
(see §14.1); the only thing that changes between them is which
`boards/<variant>.h` gets included.

## 2. Repository layout

```
bimmerz-box/
├── docs/
│   ├── hardware.md           # this spec's sibling
│   └── firmware.md           # this file
├── firmware/
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   ├── sdkconfig.defaults.waveshare_p4_module_dev_kit   # board overrides (phase 1)
│   ├── sdkconfig.defaults.waveshare_p4_wifi6            # board overrides (phase 2)
│   ├── sdkconfig.defaults.dongle                        # board overrides (phase 3)
│   ├── partitions.csv
│   ├── idf_component.yml     # pulls ediabasx-embedded from its own repo
│   ├── main/
│   │   ├── CMakeLists.txt
│   │   └── app_main.c        # boot orchestration only (~150 LOC)
│   └── components/
│       ├── ediabasx_platform/    # ESP-side transport.h + loader bridges
│       ├── obd_hal/              # pin-8 arbiter, mode enum, GPIO wiring
│       ├── transport_kline/      # L9637D UART driver + KWP timing
│       ├── transport_can/        # TWAI wrapper
│       ├── transport_ibus/       # TH3122 driver
│       ├── transport_doip/       # DoIP framing over lwIP
│       ├── jsonrpc/              # JSON-RPC 2.0 dispatch + WS endpoint
│       ├── http_static/          # SPA hosting from /sdcard/web/
│       ├── storage/              # SD/FATFS mount, file conventions
│       ├── ota_manager/          # firmware + asset OTA
│       ├── wifi_ap/              # AP bring-up via ESP-Hosted
│       ├── c6_host/              # ESP-Hosted master, C6 fw OTA
│       ├── usb_msc/              # USB mass-storage for asset drop-in
│       └── log_bus/              # ring buffer + JSON-RPC log broadcast
└── tools/
    └── pack-assets/              # build-time helper to pack SPAs into a signed bundle
```

`ediabasx-embedded` is **not** vendored here. It is fetched by the ESP-IDF
component manager from its own repository at build time, pinned to a
tag. Local patches to ediabasx-embedded are upstreamed; do not maintain
divergent forks inside `firmware/components/`.

## 3. Boot sequence

`app_main.c` runs the following stages. Each stage's outcome is reflected
on the front-panel LEDs (see [hardware spec §9.1](hardware.md#91-leds))
so boot failures are diagnosable without a console.

| # | Stage | LED state | Failure indication |
|---|---|---|---|
| 1 | Bootloader → ESP-IDF init | (all off → PWR solid) | dead board |
| 2 | PSRAM init, heap caps configured | PWR solid | PSRAM bad |
| 3 | Mount SD (FATFS) | PWR solid, FAULT slow blink on error | SD missing / corrupt |
| 4 | NVS init, config load | PWR solid | NVS partition bad |
| 5 | OBD HAL → IDLE (transceivers off, interlocks safe) | PWR solid | GPIO init failed |
| 6 | C6 link via ESP-Hosted | PWR + WIFI blink | C6 firmware bad / SDIO |
| 7 | Wi-Fi AP up | WIFI solid | radio init failed |
| 8 | ediabasx VM init (load default SGBD path) | (no change) | PSRAM exhausted |
| 9 | HTTP + WS server start | COMM ready | esp_http_server failed |
| 10 | Ready, accepting clients | PWR+WIFI solid, COMM blinks on bus | — |

Failures on stages 3, 6, 7, 9 retry with exponential backoff. Failures
on stages 2, 5, 8 panic and reset.

## 4. Task model

| Task | Stack | Priority | Affinity | Job |
|---|---|---|---|---|
| `vm` | 16 KB (PSRAM) | 5 | core 1 | Single ediabasx VM. Drains job queue. Holds the only `edxn_ediabas_t` mutex. |
| `httpd` | 8 KB | 4 | core 0 | esp_http_server main loop. Routes REST + upgrades WS connections. |
| `jsonrpc-N` | 8 KB | 4 | core 0 | One per WebSocket connection. Decodes JSON-RPC, enqueues VM-touching calls, runs inline calls locally. |
| `log_bus` | 4 KB | 3 | core 0 | Drains in-memory ring buffer → fan out to UART console + WS notification subscribers. |
| `transport_rx` | 4 KB | 6 (highest) | core 1 | Aggregates RX events from CAN, K-line UART, DoIP socket. High priority because of bus timing. |
| `connectivity` | 4 KB | 2 | core 0 | Monitors Wi-Fi/C6 health, reconnects on failure, updates status LEDs. |
| `ota` | 8 KB (PSRAM) | 3 | core 0 | Spawned on demand for firmware or asset OTA. Self-terminates. |

**Concurrency invariants:**

- All `edxn_ediabas_t` access goes through `vm` task. Other tasks enqueue
  work via FreeRTOS queue, block on a per-call response semaphore.
- The `obd_hal` module wraps its state in a single mutex; mode changes
  are atomic and serialized.
- `transport_rx` posts decoded frames to per-transport mailboxes that
  the `vm` task drains while executing comm opcodes.
- `log_bus` is single-consumer (the broadcast task) and many-producer
  (any task can `LOG_*()`). No locks on the producer side; ring buffer
  uses lockless single-consumer semantics.

## 5. Memory layout

### 5.1 Internal SRAM

- FreeRTOS task stacks for high-priority tasks (`transport_rx`).
- Interrupt vectors, hot caches.
- ESP-IDF's standard internal heap for ISR-allocations and DMA buffers.

### 5.2 PSRAM (32 MB)

- General-purpose heap (`MALLOC_CAP_SPIRAM`).
- WebSocket frame buffers (up to 64 KB per connection).
- Parsed-PRG LRU cache (~8 MB target, evicts on memory pressure).
- HTTP send buffers for large asset transfers.
- Log ring buffer (~1 MB).
- `vm` task stack and the `edxn_ediabas_t` struct itself.

Heap caps are configured so that small allocations land in internal SRAM
by default, and any allocation > 1 KB goes to PSRAM unless the caller
explicitly requests `MALLOC_CAP_INTERNAL`.

### 5.3 Internal flash (16 MB)

Partition table (`partitions.csv`):

```
# Name        Type   SubType  Offset    Size       Notes
nvs           data   nvs      0x9000    0x6000     # 24 KB
phy_init      data   phy      0xf000    0x1000     # 4  KB
otadata       data   ota      0x10000   0x2000     # 8  KB
ota_0         app    ota_0    0x20000   0x700000   # 7  MB
ota_1         app    ota_1    0x720000  0x700000   # 7  MB
c6_fw         data   0x40     0xe20000  0x100000   # 1  MB — C6 radio image
nvs_keys      data   nvs_keys 0xf20000  0x1000     # 4  KB — encryption keys if used
spare         data   0x41     0xf30000  0xd0000    # ~832 KB — reserved for future use
```

No factory partition. First flash writes to `ota_0` and `otadata`
marks it bootable. Subsequent OTAs alternate slots, with watchdog-
triggered rollback to the previously-good slot on boot failure.

Expected app image size at v1: ~2–3 MB (ESP-IDF + lwIP + esp_http_server
+ ediabasx + JSON-RPC + drivers). 7 MB slot gives roughly 2× headroom
for the lifetime of the v1 hardware.

### 5.4 SD card

```
/sdcard/
├── web/                      # SPA bundles, served at /<app>/
│   ├── inpax/
│   ├── ncsx/
│   ├── nfsx/
│   ├── tunex/
│   └── ediabasx/
├── data/                     # vehicle data, served at /data/* (read-only HTTP)
│   ├── sgbd/                 # .prg / .grp / SG files (BMW DATEN directory)
│   ├── nfs/                  # flash data files for nfsx
│   ├── tune/                 # TunerPro .xdf definitions etc. for tunex
│   └── ...                   # any other static vehicle data consumed by web apps
├── config/                   # dongle-side runtime config
│   ├── user.yml              # runtime overrides for NVS values
│   └── ota-trust.pem         # asset OTA signing public key (overridable)
├── logs/                     # rotating log files (capped, oldest evicted)
└── staging/                  # transient OTA staging area
```

The `data/` directory is the umbrella for everything that the web apps
(via `@emdzej/bimmerz-vfs`) load by HTTP path. The dongle exposes it
read-only at the `/data/` URL prefix — see §6.

## 6. Network surface

Single TCP listener on port 80. Routes:

| Method | Path | Handler |
|---|---|---|
| GET | `/` | serves dashboard from `/sdcard/sys/dashboard/index.html` |
| GET | `/<app>/` | serve `/sdcard/apps/<app>/index.html` |
| GET | `/<app>/assets/<file>` | serve from `/sdcard/apps/<app>/assets/`, with `Cache-Control: public, max-age=31536000, immutable` |
| GET | `/<app>/<anything-else>` | fallback to `index.html` (SPA hash-routing) |
| GET | `/data/<path>` | serve from `/sdcard/data/<path>` — SGBD, DATEN, and other vehicle data files. Read-only. Supports range requests for large files. Path traversal blocked. |
| GET | `/settings/` | in-flash settings UI (HTML, embedded) |
| GET | `/settings/fflate.min.js` | embedded zip lib used by *Upload & extract zip* |
| POST | `/settings/ota/firmware` | streaming P4 firmware upload — see §11.1 |
| GET | `/api/info` | dongle metadata (firmware version, chip ID, uptime) |
| GET, POST | `/api/config` | NVS-backed configuration snapshot / partial update |
| POST | `/api/restart` | `esp_restart()` |
| POST | `/api/factory-reset` | `nvs_flash_erase()` then restart |
| GET, DELETE | `/api/files` | list / recursive-delete SD card paths |
| GET | `/api/files/raw` | download a single file (range requests OK) |
| POST | `/api/files/upload` | upload a single file (`?path=<absolute>`) |
| POST | `/api/files/mkdir` | create directory |
| **WS** | `/rpc/ediabasx` | JSON-RPC 2.0 over WebSocket — replaces port 6802. All RPC endpoints live under the `/rpc/` prefix (out of the `/<app>/` static-app namespace) so future siblings (`/rpc/j2534`, `/rpc/nfsx`, …) drop in without conflicting with web app routing. |
| **WS** | `/rpc/uart/<idx>` | raw UART bridge (idx 0..N) |
| **WS** | `/rpc/can/<idx>` | TWAI/CAN bridge (idx 0..1) |

Gzip-precompressed assets (`*.gz`) are served when the client sends
`Accept-Encoding: gzip`. Vite's build pipeline already produces these.

`Cache-Control: no-cache` on HTML so SPA updates take effect on next
reload. `immutable, max-age=31536000` on hashed assets — workbox in
the SPAs handles client-side cache invalidation correctly under this
policy.

### 6.1 Client configuration

The web apps already expose a runtime-configurable `serverUrl` in
localStorage (`<app>.web.config.v1`). The original TypeScript client
(`@emdzej/ediabasx-client`) and its `ws://localhost:6802` default are
left untouched.

For the dongle, the user (or a per-app first-run config page hosted
within each SPA) sets `serverUrl` to `ws://<dongle-ip>/rpc`. The dongle
firmware does not require any change to the TS code or the published
npm client — the migration happens entirely on the web-app side, in
each app's existing settings UI.

## 7. JSON-RPC server

### 7.1 Method registry

The 20 methods listed in `~/Projects/my/ediabasx/packages/ediabasx-server/src/ediabas-server.ts`
are reimplemented one-for-one. Each handler is annotated with whether
it must run on the VM task or can execute inline:

| Method | Runs on |
|---|---|
| `init`, `end`, `job`, `listSgbd`, `listJobs`, `getJobMetadata`, `disassembleJob` | VM task (serialized) |
| `resultSets`, `resultText`, `resultInt`, `resultReal`, `resultBinary`, `resultFormat` | VM task (read result state) |
| `state`, `errorCode`, `errorText` | VM task |
| `break` | **inline** (interrupts in-flight job by setting a flag the VM checks between opcodes) |
| `log.subscribe`, `log.unsubscribe`, `info` | inline |

### 7.2 Dispatch flow

```c
// Pseudocode for the per-connection JSON-RPC task
void jsonrpc_handle_message(jsonrpc_ctx_t *ctx, const char *msg, size_t len) {
    cJSON *req = cJSON_ParseWithLength(msg, len);
    /* validate envelope, extract id + method + params */

    const jsonrpc_method_t *m = registry_find(method);
    if (!m) {
        respond_error(ctx, id, JSONRPC_METHOD_NOT_FOUND);
        return;
    }

    if (m->serialize_through_vm) {
        vm_job_t job = { .method = m, .params = params, .reply_sem = ctx->reply_sem };
        xQueueSend(vm_queue, &job, portMAX_DELAY);
        xSemaphoreTake(ctx->reply_sem, portMAX_DELAY);
        respond_result(ctx, id, job.result);
    } else {
        cJSON *result = m->handler(ctx, params);
        respond_result(ctx, id, result);
    }
}
```

The `break` inline path sets `vm->break_requested = true`. The VM's
opcode loop polls this flag (mirroring the TS `requestBreak()` semantics
that ediabasx-embedded's README notes is not yet ported — it will be
ported now as part of this work).

### 7.3 Log notifications

`log_bus` broadcasts JSON-RPC notifications to subscribed connections:

```json
{
  "jsonrpc": "2.0",
  "method": "log",
  "params": {
    "level": "info",
    "category": "transport_kline",
    "msg": "5-baud init OK, KWP2000 handshake on pin 7",
    "time": 1717689600123
  }
}
```

Subscription levels filter at the broadcast site, not per-connection,
to keep the hot path cheap.

### 7.4 Binary parameter encoding

The TS server accepts job params as either a semicolon-joined string
(legacy) or an array containing strings and `{binary: "<base64>"}`
objects (≥ 0.7.1). The C implementation supports both for compatibility:

```c
// Legacy: split on ';'
// New: walk JSON array, base64-decode {binary} entries to byte buffers
```

Each parameter is materialized into the VM's parameter scratch buffer
(in PSRAM, capped at 64 KB total per job).

## 8. ediabasx integration

### 8.1 Component manager dependency

`firmware/idf_component.yml` declares:

```yaml
dependencies:
  ediabasx-embedded:
    git: https://github.com/<org>/ediabasx
    path: native
    version: "v0.1.0"        # bump per release
```

ESP-IDF fetches and builds it as a managed component on first build.

### 8.2 Platform bridge (`ediabasx_platform/`)

Implements the three extension points the native library exposes:

1. **`edxn_transport_t` vtable** — composite transport that dispatches
   to `transport_kline`, `transport_can`, `transport_ibus`, or
   `transport_doip` based on the currently-selected interface.
2. **`sgbd_loader_t`** — reads `*.prg` / `*.grp` from `/sdcard/sgbd/`
   via FATFS, scans case-insensitively (BMW disk convention).
3. **`table_loader_t`** — same backing store, looks up external table
   PRGs.

The platform bridge is the only place that imports both `<ediabasx/*>`
headers and ESP-IDF headers. Everywhere else, one side or the other.

### 8.3 Memory configuration

The VM task is created with its stack in PSRAM (`xTaskCreateWithCaps`
with `MALLOC_CAP_SPIRAM`). `edxn_ediabas_t` is allocated on PSRAM.
The ~50 KB VM struct + parsed-PRG cache + result sets dominate
ediabasx's memory usage.

## 9. OBD HAL

The only code that touches the four interlock-critical GPIOs
(`KLINE7_EN`, `KLINE8_EN`, `DOIP_ACT`, `LLINE_DRV`).

```c
typedef enum {
    OBD_MODE_IDLE,           // all transceivers off, safe default
    OBD_MODE_KLINE7,         // K-line on pin 7 only
    OBD_MODE_KLINE8,         // K-line on pin 8 only
    OBD_MODE_KLINE_BOTH,     // K-line on pins 7 and 8 (E-series default)
    OBD_MODE_DOIP_ACTIVE,    // DoIP activation on pin 8 (F/G chassis)
    OBD_MODE_CAN_ONLY,       // CAN active, K-line and DoIP both off
} obd_mode_t;

esp_err_t obd_set_mode(obd_mode_t mode);
obd_mode_t obd_get_mode(void);

esp_err_t obd_lline_pulse(uint32_t bit_pattern, int baud_rate);
```

`obd_set_mode()` asserts that `KLINE8_EN` and `DOIP_ACT` are never
simultaneously high — the hardware interlock catches this too, but the
software assertion catches the bug earlier and panics in development
builds.

L-line driving is wrapped in `obd_lline_pulse()`, which bit-bangs the
5-baud KWP wake sequence over the AO3416 N-FET. Only used during the
init phase of legacy K-line dialogue.

## 10. Transport components

### 10.1 K-line (`transport_kline/`)

- ESP-IDF UART driver, half-duplex.
- Selectable baud rate (10400 / 9600 / 57600 typical).
- KWP2000 timing handled in software: P1 (inter-byte), P2 (response delay),
  P3 (inter-message), P4 (inter-byte tester). Tunable per ECU profile.
- 5-baud slow init handled by manually toggling the L9637D TX pin at
  200 ms per bit, then switching to the UART driver for KWP handshake.
- Echo cancellation: the L9637D echoes TX onto RX. The driver discards
  echoed bytes by counting transmitted-but-not-yet-acked octets.

### 10.2 CAN (`transport_can/`)

- ESP-IDF TWAI driver wrapped to expose `edxn_transport_t`.
- Configurable bus speed: 500 kbps (D-CAN, standard for E60+/F/G OBD),
  100 kbps (K-CAN-style buses, future).
- ISO-TP layer for diagnostic message segmentation (multi-frame
  transfers). Implemented in this component because EDIABAS speaks ISO-TP
  natively.
- RX events posted to the transport_rx mailbox.

### 10.3 IBUS (`transport_ibus/`)

- UART driver via TH3122.
- Bus arbitration via collision detection — TH3122 reports collisions
  on its echo line; on collision, back off random 100–500 µs and retry.
- Checksum framing per BMW IBUS spec.
- Logical messages routed by source/destination byte.

### 10.4 DoIP (`transport_doip/`)

- Activation: caller sets `OBD_MODE_DOIP_ACTIVE` via OBD HAL.
- After 200 ms (gateway response time), open TCP socket to `169.254.0.1:13400`
  (BMW gateway's link-local IP).
- Send DoIP routing activation request, wait for routing activation response.
- ISO 13400 framing over the socket: header (8 bytes) + payload.
- Diagnostic messages tunneled inside DoIP payloads, demuxed back to
  the ediabasx VM's transport vtable.

## 11. OTA

### 11.1 P4 firmware (implemented)

- **Endpoint:** `POST /settings/ota/firmware`, `Content-Type: application/octet-stream`,
  body is the raw `.bin` straight from `firmware/build/bimmerz_box.bin`.
- **Flow:** stream into the inactive OTA slot via `esp_ota_begin /
  esp_ota_write / esp_ota_end`. First chunk is validated against
  `ESP_APP_DESC_MAGIC_WORD` so a wrong file fails fast before any flash
  writes. On success, `esp_ota_set_boot_partition()` flips otadata and
  the dongle reboots ~500 ms after the JSON response flushes.
- **Rollback (armed by `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`):** a
  freshly-installed image boots in state `ESP_OTA_IMG_PENDING_VERIFY`.
  `ota_manager_init()` schedules a 60 s `esp_timer` that calls
  `esp_ota_mark_app_valid_cancel_rollback()` once the device has been
  up that long. Any panic / watchdog reset inside the 60 s window
  causes the bootloader to revert to the previous slot on next boot —
  so a brick from a bad upload is recoverable without USB-JTAG.
- **Response shape:** `{ok:true, version, partition, size, restart_ms}`
  on success, `{ok:false, error}` on failure (HTTP 400/413/500).
- **UI:** `/settings/` has a *Firmware update* section with a file
  picker + progress bar. Upload uses XHR (so the bar reflects actual
  bytes-on-wire, not a fetch black box).

### 11.2 Security gaps (known, not yet addressed)

These are deferred to a later iteration. Documented so callers know.

- **No authentication on `/settings/*` or `/api/*`.** Anyone on the
  dongle's AP can flash firmware, wipe NVS, or read/write the SD card.
  Treat AP-Wi-Fi access as physical access until a credential gate is
  added (likely shared-secret challenge + per-session cookie).
- **No image signature verification.** `esp_ota_write` validates the
  ESP image checksum but does not verify cryptographic provenance. A
  LAN attacker (or anyone who reached the AP credentials) can flash an
  arbitrary image. Mitigation path: enable
  `CONFIG_SECURE_BOOT_V2_ENABLED` + `CONFIG_SECURE_SIGNED_APPS_*` so
  the bootloader rejects unsigned images. Pre-condition: settle on a
  signing key escrow story first.

### 11.3 C6 radio firmware (not yet implemented)

Planned, not built:

- C6 firmware blob to live in the `c6_fw` partition (1 MB slot at
  `0xe20000`).
- `c6_host` component would own the ESP-Hosted master and the C6
  bootloader protocol over SDIO.
- Upload path TBD (likely `POST /settings/ota/c6`) — handler would
  stream the image into `c6_fw` then trigger a C6 reflash via the
  ESP-Hosted slave's update protocol.
- Rare path — C6 firmware updates ride along with major P4 releases.

### 11.4 Asset OTA (not yet implemented — covered ad-hoc by SD writes)

Today, app/SGBD bundles are pushed via the `/api/files/upload`
endpoint (per-file) or the *Upload & extract zip* button in
`/settings/` (browser-side unzip via embedded fflate). Neither is
atomic or signature-checked. A proper asset OTA channel is sketched
below for future implementation:

- Signed tarball: manifest + ECDSA-P256 signature + files tree.
- Server-side flow: verify against `/sdcard/config/ota-trust.pem`,
  extract to `/sdcard/staging/`, atomic rename swap, purge old tree
  on next boot.
- USB-MSC drop-in remains the offline fallback for users without
  Wi-Fi access.

## 12. Configuration (NVS)

```
wifi.ap.ssid          = "BimmerzBox-XXXX"   # XXXX from last 2 bytes of chip ID
wifi.ap.password      = "<random 12-char>"  # generated on first boot, written to NVS
wifi.ap.channel       = 6
wifi.ap.hidden        = false
network.ip            = 172.16.7.1
network.netmask       = 255.255.255.0
network.dhcp_start    = 172.16.7.100
diagnostic.default_app = "inpax"            # which app /  redirects to
diagnostic.default_kline = "both"           # initial K-line mode on connect
diagnostic.can_speed_kbps = 500
ota.public_key_pem    = <baked at build, overridable via /sdcard/config/ota-trust.pem>
```

A 10 s long-press on the multifunction button restores all NVS keys to
defaults (preserving the per-device random SSID/password seed). A
status LED pattern (all four flashing in unison) confirms the reset.

## 13. Logging

- ESP-IDF's `esp_log_*` macros, configured to route through `log_bus`.
- Each log record: level, tag, message, monotonic timestamp.
- Three sinks, fed by the `log_bus` task:
  1. UART console (USB-C CDC-ACM).
  2. PSRAM ring buffer (~1 MB, ~10k recent entries).
  3. JSON-RPC log notifications to subscribed WS clients.
- Rotating file log on SD card (capped at 10 MB total, oldest evicted).
- Crash dumps captured via `esp_core_dump_*` to a dedicated NVS or flash
  partition (TBD — sized at ~64 KB for stack + register state).

## 14. Build configuration

### 14.1 Targets

Three board variants, selected via `CONFIG_BIMMERZ_BOARD_*` (layered
overlay file `sdkconfig.defaults.<variant>` on top of the common
`sdkconfig.defaults`):

- `waveshare_p4_module_dev_kit` — phase 1, [Waveshare ESP32-P4 Module
  DEV-KIT](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT). SoM +
  carrier. Onboard C6 (Wi-Fi+BLE) over SDIO, SD slot, Ethernet PHY
  broken out. K-line / CAN / IBUS / DoIP components compile but the
  OBD HAL routes `obd_set_mode()` to no-ops (with logging) because
  the transceivers don't exist on this board.
- `waveshare_p4_wifi6` — phase 2, [Waveshare ESP32-P4-WiFi6
  Devkit](https://docs.waveshare.com/ESP32-P4-WIFI6). Single-board
  variant of the same family. Same C6 + SD pinmap; no Ethernet PHY.
- `dongle` — phase 3, custom hardware. Full transceiver wiring, all
  modes active.

### 14.2 Pin assignments

A single header `firmware/components/board/include/boards/<variant>.h`
defines every GPIO, UART, SPI, I2C, and TWAI assignment for that
board. Components import via the dispatch header
`boards/board.h`, which `#include`s the right per-variant file based
on Kconfig.

### 14.3 CI

- Build matrix: `waveshare_p4_module_dev_kit` × `waveshare_p4_wifi6`
  × `dongle` — see `.github/workflows/firmware-build.yml`. Per-board
  `.bin` artefacts are uploaded on every push; GitHub Release
  `published` attaches them to the release.
- Static analysis: `idf.py clang-tidy` on the project + components.
- Unit tests: ediabasx-embedded already has a test driver (`test/run.c`);
  ESP-IDF host tests can wrap the same harness for parsed-PRG regression.

## 15. Open items

- **Crash dump partition size and storage location.** TBD with first
  panic-on-purpose test.
- **Wi-Fi STA fallback.** Currently AP-only. If we ever want cloud-
  assisted OTA, a temporary STA mode triggered by a button combo would
  be straightforward to add later.
- **TLS termination.** All current routes are HTTP. Adding HTTPS on
  port 443 with a self-signed cert is possible but creates a UX wart
  (browser warnings). Defer until there's a real use case.
- **ECU profile system.** KWP timing, baud rate, ISO-TP parameters per
  vehicle / ECU type — needs a config format. The TS server doesn't
  expose this; it's hardcoded per interface. Decide whether the dongle
  inherits that approach or extends it.
- **Multi-CAN support.** Hardware reserves a second TWAI footprint but
  doesn't populate it. Firmware-side, the transport layer is already
  abstracted enough to add a second channel cleanly.

## 16. Alternative mode — J2534 PassThru

The dongle can operate as a **SAE J2534 v04.04 PassThru** device in
addition to the default EDIABAS-server mode, exposing the same physical
transports to PC diagnostic software (BMW ISTA-D / ISTA+, E-SYS, WinKFP,
or generic J2534 clients) instead of (or alongside) the web apps.

**No hardware changes.** The transceivers already on the BOM cover every
J2534 protocol BMW software uses:

| J2534 protocol ID  | Maps to                                  |
|--------------------|------------------------------------------|
| `ISO9141`          | K-line via L9637D, raw framing           |
| `ISO14230` (KWP)   | K-line via L9637D, KWP timing            |
| `ISO15765` (CAN)   | TWAI + TJA1051T/3, ISO-TP layer in fw    |
| `ISO15765 + DoIP`  | LAN8720A + lwIP, ISO 13400 framing       |
| `J1850 PWM/VPW`    | unsupported — not used by BMW            |

J2534 IOCTL knobs (P1/P2/P3/P4 KWP timings, ISO-TP STmin, message
filters PASS/BLOCK/FLOW_CONTROL) all map onto controls that
`transport_kline` and `transport_can` already need for EDIABAS.

**Firmware additions** (~3 weeks once `transport_kline` + `transport_can`
have real ECU comms working):

| Piece                                    | ~LOC | Notes |
|------------------------------------------|------|-------|
| USB CDC-ACM device mode on USB OTG       | 150  | via `espressif/esp_tinyusb` managed component |
| `transport_j2534` component              | 600  | parses J2534 commands, dispatches to existing transport vtable |
| Precise KWP timing in `transport_kline`  | (shared with EDIABAS) | already needed |
| Filter engine (PASS/BLOCK/FLOW_CONTROL)  | 200  | runs inside `transport_rx` |
| Admin UI mode toggle (NVS-backed)        | 50   | EDIABAS / J2534 / both simultaneously |

**Differentiator:** the J2534 wire protocol rides either USB CDC **or**
TCP over the dongle's Wi-Fi AP. That makes it a **wireless J2534**
adapter — PC reaches the dongle through the AP without a USB cable to
the OBD port. Useful when the OBD socket is awkward to reach (kick
panels, transmission tunnels). Most J2534 hardware is USB-tethered.

**PC-side gap (not dongle work):** a J2534 v04.04 `.dll` (Windows) /
`.so` (Linux) implementing the API and translating to our wire
protocol. ~2–3 weeks of C++, plus a Windows-side registry installer
under `HKLM\Software\PassThruSupport.04.04\<vendor>`. Adopting the
Tactrix Openport wire protocol on the firmware side would let an
existing open-source DLL talk to us and avoid that work.

**Optional hardware add (deferred):** a high-side switch to apply a
programming voltage (5–18 V pulse) on an OBD pin. Old-school flashing
needs it; BMW J2534 software almost never does. ~$0.20 BOM if we add it.

**Suggested rollout order:**
1. Land EDIABAS-mode K-line + CAN comms first (proves transports work).
2. Add `transport_j2534` as a sibling protocol on those same transports.
3. Add USB CDC-ACM device mode + admin UI toggle.
4. Decide between a custom DLL and adopting the Tactrix protocol.
