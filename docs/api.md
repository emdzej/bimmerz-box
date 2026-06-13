# Bimmerz Box — API reference

All endpoints live on the dongle's single HTTP server (port 80) on its
Wi-Fi AP at `172.16.7.1`. Two transport flavours:

- **HTTP** — static files, admin file/system API, captive-portal helpers
- **WebSocket** — JSON-RPC 2.0 surfaces for diagnostics (ediabasx VM,
  UART, CAN)

The dongle is AP-only: clients join the `BimmerzBox` SSID and reach
`http://172.16.7.1/`. There is no auth — anyone on the AP can call
anything. Don't expose the dongle to a hostile network.

---

## Table of contents

- [HTTP](#http)
  - [Static apps](#static-apps)
  - [SD data namespace `/data/*`](#sd-data-namespace-data)
  - [Captive-portal helpers](#captive-portal-helpers)
- [Admin API `/api/*`](#admin-api-api)
  - [`GET /api/info`](#get-apiinfo)
  - [`GET /api/config` · `POST /api/config`](#get-apiconfig--post-apiconfig)
  - [`POST /api/restart`](#post-apirestart)
  - [`POST /api/factory-reset`](#post-apifactory-reset)
  - [`GET /api/files`](#get-apifiles)
  - [`GET /api/files/raw`](#get-apifilesraw)
  - [`POST /api/files/upload`](#post-apifilesupload)
  - [`POST /api/files/mkdir`](#post-apifilesmkdir)
  - [`DELETE /api/files`](#delete-apifiles)
- [JSON-RPC 2.0 envelope](#json-rpc-20-envelope)
- [`/rpc/ediabasx` — ediabasx VM](#rpcediabasx)
  - [Session: `init` · `end`](#session-init--end)
  - [Introspection: `info` · `state` · `errorCode` · `errorText`](#introspection)
  - [SGBD discovery: `listSgbd` · `listJobs` · `getJobMetadata` · `disassembleJob`](#sgbd-discovery)
  - [Execution: `job` · `break`](#execution)
  - [Result accessors (legacy)](#result-accessors-legacy)
  - [Log streaming: `log.subscribe` · `log.unsubscribe`](#log-streaming)
  - [K-line bench debug](#k-line-bench-debug)
- [`/rpc/uart/<n>` — UART RPC](#rpcuartn)
- [`/rpc/can/<n>` — CAN RPC](#rpccann)
- [Error codes](#error-codes)

---

## HTTP

### Static apps

| Path                  | Source                                          |
|-----------------------|-------------------------------------------------|
| `GET /`               | `/sdcard/web/dashboard/index.html` (the hub)    |
| `GET /<app>/...`      | `/sdcard/web/<app>/...` if `<app>/` exists      |
| `GET /<unknown>/`     | falls back to dashboard index (SPA routing)     |

Routing rules:

1. URIs are sanitised — anything containing `/../`, `/./`, `//` or
   trailing `/..` is rejected with `400 Bad Request`.
2. If the `Host:` header doesn't match `172.16.7.1`, the request is
   rerouted via the captive flow (see below).
3. The first path segment is matched against a real subdirectory of
   `/sdcard/web/`. If it exists (and isn't `dashboard/`), the request
   is served from that app's directory; otherwise the dashboard's
   `index.html` is returned (SPA hash-routing).

`Content-Type` is inferred from the file extension; common web types are
handled (`html / css / js / mjs / json / svg / png / jpg / ico / webp /
woff / woff2 / csv / wasm / map`), others fall back to
`application/octet-stream`.

`HEAD` is supported alongside `GET` so the dashboard can probe tile
presence without downloading bodies.

### SD data namespace `/data/*`

| Path                                  | Maps to                                |
|---------------------------------------|----------------------------------------|
| `GET /data/<path>`                    | `/sdcard/data/<path>` (read-only)      |

Used by SPAs (e.g. ediabasx) that need to fetch BMW DATEN-disk files,
calibration tables, etc. that live on the SD card outside the
`/sdcard/web/` tree. Directory listings aren't exposed — clients fetch
files by exact path.

### Captive-portal helpers

| Path                            | Method | Purpose                                  |
|---------------------------------|--------|------------------------------------------|
| `GET /welcome`                  | GET    | Embedded welcome page (in flash)         |
| `POST /captive/accept`          | POST   | Mark client IP as accepted               |

Behaviour:

- Any request with a `Host:` header ≠ `172.16.7.1` (which is what the
  OS captive probe URLs hit after DNS hijack) returns a **302** to
  `http://172.16.7.1/welcome` — *unless* the client IP has previously
  hit `POST /captive/accept`, in which case the server returns a
  platform-appropriate "online" body so the OS dismisses the captive
  sheet:
  - URLs matching `hotspot-detect` / `library/test/success` →
    `200 OK` + literal Apple Success HTML body.
  - URLs matching `generate_204` / `gen_204` → `204 No Content`.
  - URLs matching `connecttest` → `200 OK` + `Microsoft Connect Test`.
  - URLs matching `ncsi.txt` → `200 OK` + `Microsoft NCSI`.
  - Anything else → `204 No Content`.
- `POST /captive/accept` returns `{"ok":true}` and records the source
  IPv4. Up to 16 IPs are tracked (FIFO eviction).

---

## Admin API `/api/*`

JSON-only. All endpoints return `application/json` unless noted. Errors
use standard HTTP status codes (400 / 404 / 405 / 500) with a short
text body explaining the cause.

### `GET /api/info`

Server / chip metadata.

**Response**
```json
{
  "firmware": "1.0.0",
  "chip_id": "AA:BB:CC:DD:EE:FF",
  "idf_version": "v5.4.4",
  "uptime_s": 12345,
  "free_heap_kb": 8192,
  "largest_block_kb": 4096
}
```

### `GET /api/config` · `POST /api/config`

Persisted configuration (NVS namespace).

**`GET` response**
```json
{
  "ap_ssid": "BimmerzBox",
  "ap_password": "bimmerzbox",
  "ap_channel": 6,
  "ap_ip": "172.16.7.1/24",
  "eth_mode": "off",     // "off" | "dhcp" | "static"
  "eth_ip": "",          // CIDR when eth_mode == "static"
  "eth_gw": "",
  "default_app": "inpax"
}
```

**`POST` body** — any subset of the above keys. Unknown keys are
ignored. `ap_channel` must be 1–13. Strings are written as-is. Changes
take effect after `POST /api/restart`.

```json
{"ap_ssid":"MyBox","ap_password":"secret123","ap_channel":11}
```

**Response**: `{"ok":true}` or 500.

### `POST /api/restart`

Returns `{"ok":true}` immediately, then reboots the dongle after a
short delay (a few hundred ms — enough for the response to flush).

### `POST /api/factory-reset`

Erases the NVS partition, returns `{"ok":true}`, then reboots. All
persisted configuration is lost.

### `GET /api/files`

List a directory on the SD card.

**Query**
- `path` (required) — must start with `/sdcard` and contain no `..` or
  `//` segments.

**Response**
```json
{
  "path": "/sdcard/data/ediabas/ecu",
  "entries": [
    { "name": "MS420DS0.prg", "type": "file", "size": 824810, "mtime": 1717930000 },
    { "name": "subdir",       "type": "dir",  "size": 0,      "mtime": 1717930100 }
  ]
}
```

Errors: `400` (bad path), `404` (not a directory).

### `GET /api/files/raw`

Stream a single file. `Content-Type` is inferred from the extension
(falls back to `application/octet-stream`); a `Content-Disposition:
attachment; filename=...` header is set so browsers download rather
than render.

**Query**: `path` (same rules as `/api/files`).

Errors: `400` (bad path), `404` (not a file).

### `POST /api/files/upload`

Stream the raw request body into a target file (no multipart parsing).
File is truncated/created at the target path. On any failure mid-stream
the partial file is `unlink`'d.

**Query**: `path` — full target file path including filename.

**Body**: raw bytes (any `Content-Type`).

**Response**: `{"ok":true}` or 500.

### `POST /api/files/mkdir`

Create a directory.

**Query**: `path`.

**Response**: `{"ok":true}` or 500 (already exists / parent missing).

### `DELETE /api/files`

Delete a file or empty directory.

**Query**: `path`.

**Response**: `{"ok":true}`, 404 (not found), or 500 (non-empty dir,
permissions, etc).

---

## JSON-RPC 2.0 envelope

All `/rpc/...` endpoints speak JSON-RPC 2.0 over WebSocket text frames.
One JSON object per frame. The envelope follows the spec exactly:

**Request → server**
```json
{ "jsonrpc": "2.0", "id": 42, "method": "info", "params": {} }
```

**Result ← server**
```json
{ "jsonrpc": "2.0", "id": 42, "result": { ... } }
```

**Error ← server**
```json
{ "jsonrpc": "2.0", "id": 42, "error": { "code": -32601, "message": "Method not found" } }
```

**Notification ← server** (no `id`, no response expected)
```json
{ "jsonrpc": "2.0", "method": "uart.rx", "params": { ... } }
```

`id` may be a number, string, or null; the server echoes it back
verbatim. Notifications without an `id` produce no response from
either side. Standard JSON-RPC error codes apply: `-32700` parse
error, `-32600` invalid request, `-32601` method not found,
`-32602` invalid params, `-32603` internal error. Component-specific
errors are returned as `{"error": "<machine_readable_code>"}` inside
the `result` field (see each endpoint below).

Binary payloads (UART bytes, CAN data) are base64-encoded strings.

---

## `/rpc/ediabasx`

The ediabasx server — runs BEST2 SGBD bytecode on the embedded VM,
talks to the ECU via `transport_kline`. Shape matches the TypeScript
reference server in `~/Projects/my/ediabasx/packages/ediabasx-server`.

### Session: `init` · `end`

Lifecycle acks. Both currently return `{"ok": true}`. The C server
doesn't keep per-connection state beyond what the VM already holds —
these exist so the client lib's bootstrap sequence completes.

### Introspection

#### `info`
Server / dongle metadata.
```json
{
  "connected": true,
  "clients": 0,
  "host": "172.16.7.1",
  "port": 80,
  "transport": "websocket",
  "sgbdPath": "/sdcard/data/ediabas/ecu"
}
```

#### `state`
VM state: `"ready"`. (`"busy"` / `"break"` / `"error"` will land when
async job execution does.)

#### `errorCode`
Last EDIABAS error code: `{ "code": 0 }`.

#### `errorText`
Last EDIABAS error text: `{ "text": "" }`.

### SGBD discovery

#### `listSgbd`
Returns all `.prg` and `.grp` files under `/sdcard/data/ediabas/ecu/`,
sorted by name (case as on disk).

```json
{
  "sgbds": [
    { "name": "MS420DS0.prg", "ext": "prg" },
    { "name": "d_0080.grp",   "ext": "grp" }
  ]
}
```

#### `listJobs`
**Params**: `{ "ecu": "<sgbd-name>" }` — bare basename (`"MS420DS0"`)
or filename (`"MS420DS0.prg"` / `"d_0080.grp"`).

```json
{
  "jobs": [
    { "name": "IDENT", "argCount": 0, "resultCount": 0 },
    { "name": "STATUS_MESSWERTBLOCK_LESEN", "argCount": 1, "resultCount": 12 }
  ],
  "tableCount": 5
}
```

`argCount` / `resultCount` may be 0 today — the parser doesn't yet
walk the SGBD's argument/result metadata section. `getJobMetadata`
will be the canonical source once it's implemented.

#### `getJobMetadata`
**Params**: `{ "ecu", "job" }`.

```json
{ "name": "IDENT", "args": [], "results": [] }
```

Args/results are empty until the metadata-section parser lands.

#### `disassembleJob`
**Params**: `{ "ecu", "job" }`. Returns a placeholder line — the
embedded disassembler isn't wired yet:

```json
{ "lines": ["(disassembly not yet supported on the embedded server)"] }
```

### Execution

#### `job`
The real EDIABAS execution path. Loads the SGBD if not already loaded,
auto-runs `INITIALISIERUNG` on first execution (+ `IDENTIFIKATION`
variant-swap for `.grp` files), then runs the requested job.

**Params**
```json
{
  "ecu": "MS420DS0",
  "job": "IDENT",
  "params": "arg0;arg1"   // optional, semicolon-separated string OR
                          // ["arg0", "arg1", {"binary":"<base64>"}]
}
```

**Response**
```json
{
  "sets": [
    {
      "VARIANTE":  { "name": "VARIANTE",  "type": "text",    "value": "MS420DS0" },
      "OBJECT":    { "name": "OBJECT",    "type": "text",    "value": "MS420DS0" },
      "JOBNAME":   { "name": "JOBNAME",   "type": "text",    "value": "IDENT" },
      "SAETZE":    { "name": "SAETZE",    "type": "integer", "value": 1 },
      "ECU":       { "name": "ECU",       "type": "text",    "value": "MS 42.0 fuer M52 mit EWS 3" },
      "JOB_STATUS":{ "name": "JOB_STATUS","type": "text",    "value": "OKAY" }
    },
    {
      "ID_BMW_NR": { "name": "ID_BMW_NR", "type": "text",    "value": "7500255" },
      "ID_HW_NR":  { "name": "ID_HW_NR",  "type": "integer", "value": 15 }
    }
  ]
}
```

Set `0` is the **system set** (VARIANTE / OBJECT / JOBNAME / SAETZE +
persisted INFO metadata: ECU / ORIGIN / REVISION / AUTHOR / COMMENT /
PACKAGE / SPRACHE / JOB_STATUS). Sets `1..N` are the job's data sets
in emission order. Entry shape is always `{ name, type, value }`.

`type` values: `"text"` (string), `"integer"`, `"long"`, `"real"`,
`"binary"` (array of byte numbers).

**Error response (transport / VM failure)**
```json
{ "sets": [], "error": "exec failed", "errorCode": 8 }
```

`errorCode` is the `edxn_error_t` enum value — see [Error
codes](#error-codes).

#### `break`
Aborts the in-flight job. Currently returns `{"ok": true}` — async
break wiring lands when the dispatcher is multi-threaded.

### Result accessors (legacy)

`resultSets / resultText / resultInt / resultReal / resultBinary /
resultFormat` — kept for compatibility with clients that drilldown
incrementally after `job`. Today these return shape-correct stubs:
- `resultSets` → `{ "count": 0 }`
- `resultText` → `{ "value": "" }`
- `resultInt` / `resultReal` → `{ "value": 0 }`
- `resultBinary` → `{ "value": [] }`
- `resultFormat` → `{ }`

The embedded ediabasx-client caches the `job` response and serves
these locally — no server-side state needed.

### Log streaming

#### `log.subscribe`
**Params**: `{ "level": "debug"|"info"|"warn"|"error" }` (default
`"info"`). Returns `{ "ok": true, "level": "<echoed>" }`.

#### `log.unsubscribe`
Returns `{ "ok": true }`. The broadcast plumbing from `log_bus` →
`log` notifications is wired in the firmware but not yet hot — no
notifications are sent at runtime.

### K-line bench debug

Direct UART access bypassing the VM. Intended for hardware bring-up;
prefer `/rpc/uart/0` for app-level use.

#### `klineWireTest`
Detaches the UART, drives TX high then low as plain GPIO, samples RX
between each step.

```json
{ "rxWhenTxHigh": 1, "rxWhenTxLow": 0, "loopOk": true, "ok": "ok" }
```

#### `klineHoldTx`
**Params**: `{ "level": 0|1, "holdMs": 5000 }`. Drives TX to `level`
for `holdMs` (capped at 30000), samples RX mid-hold, restores the UART.

```json
{ "level": 0, "holdMs": 5000, "rxDuringHold": 0, "ok": "ok" }
```

#### `klineProbe`
**Params**: `{ "tx": [0x12,0x04,0x00], "baud": 9600, "timeoutMs": 200, "loopback": false }`.
Default `tx` is `AA 55 0F F0`.

```json
{ "tx": [...], "rx": [...], "rxHex": "12 04 00 16", "ok": "ok" }
```

#### `klineSetBaud` · `klineSetParity`
**Params**: `{ "baud": 9600 }` / `{ "parity": "even"|"odd"|"none" }`.
Return `{ "baud": ..., "ok": "ok" }` / `{ "parity": ..., "ok": "ok" }`.

#### `klineSlowInit`
5-baud bit-bang on TX. **Params**:
```json
{
  "value": 51,            // default 0x33
  "bitTimeMs": 200,
  "baudAfter": 10400,
  "parityAfter": "none",
  "readKeyBytesMs": 1000  // read window for the ECU's key bytes
}
```
Returns `{ "value", "bitTimeMs", "rx": [...], "rxHex", "ok" }`.

#### `klineFastInit`
**Params**: `{ "breakMs": 25, "idleMs": 25 }`. Returns `{ "breakMs",
"idleMs", "ok" }`.

#### `klineKwpRequest`
Full BMW-FAST (KWP2000) transaction.
```json
{
  "ecu":     0x12,
  "tester":  0xF1,
  "payload": [0x21, 0x01],   // or "21 01" hex string
  "baud":    10400,
  "timeoutMs": 1200,
  "telEndMs":   50,
  "regenMs":    20
}
```
Returns:
```json
{
  "tx": [...], "txHex": "82 12 F1 21 01 A7",
  "rx": [...], "rxHex": "84 F1 12 ...",
  "payload": [...], "payloadHex": "...",
  "checksumOk": true,
  "ok": "ok"
}
```

#### `klineDs2Request`
Full DS1 / DS2 / Concept-1 transaction.
```json
{
  "concept":  6,            // 1 = Concept-1, 5 = DS1, 6 = DS2
  "payload":  [0x12, 0x04, 0x00],
  "baud":     9600,
  "timeoutMs": 1200,
  "telEndMs":   50,
  "regenMs":     0,
  "interByteMs": 0,
  "checksumByUser": false   // true = payload already includes XOR
}
```
Returns `{ "tx", "txHex", "rx", "rxHex", "checksumOk", "ok" }`.

---

## `/rpc/uart/<n>`

Generic UART pipe. Index map (today):

- `/0` → K-line (`BOARD_KLINE_UART_NUM` on `UART_NUM_1`)
- `/1..n` → reserved (returns `uart_not_present`)

**Arbitration**: one holder per index. While `/rpc/uart/0` is held,
the ediabasx `job` RPC returns `EDXN_ERR_TRANSPORT` (8) so the SGBD's
error trap fires cleanly instead of stomping the bus. Holder is
tracked by WebSocket fd — disconnect releases automatically.

### `uart.open`
```json
{
  "exclusive":   false,     // true = reject if held; false = revoke prior holder
  "baud":        9600,
  "parity":      "even",    // "none" | "even" | "odd"
  "dataBits":    8,
  "stopBits":    1,
  "consumeEcho": true       // K-line is half-duplex; auto-discard TX echo on read
}
```
**Result**
```json
{ "ok": true, "baud": 9600, "parity": "even", "exclusive": false, "consumeEcho": true }
```
Errors: `"bus_busy"`, `"uart_not_present"`, `"rx_task_failed"`.

### `uart.configure`
Update settings while open. Same params as `open`. Caller must already
be the holder; otherwise `"not_holder"`.

### `uart.write`
```json
{ "data": "EgQA" }          // base64 — [0x12, 0x04, 0x00]
```
**Result**: `{ "ok": true, "wrote": 3 }` or `{ "error": "<reason>" }`.

If `consumeEcho` is true (default), the matching TX echo bytes are
read off the UART before this returns. If false, the echo arrives as
part of `uart.rx`.

### `uart.transact`
Request/response in one shot.
```json
{ "data": "EgQA", "readMs": 1000, "readBytes": 64 }
```
**Result**: `{ "data": "<base64>", "len": N }`.

### `uart.slowInit`
5-baud bit-bang.
```json
{ "value": 51, "bitTimeMs": 200, "baudAfter": 10400, "parityAfter": "none" }
```

### `uart.fastInit`
```json
{ "breakMs": 25, "idleMs": 25 }
```

### `uart.close`
`{}` — release the lock, stop the RX pump task.

### Notifications

#### `uart.rx`
Streamed RX bytes — sent only to the current holder while open.
```json
{ "data": "EgQAEgQAFg==" }   // base64
```
Emitted every ~50 ms whenever new bytes are available.

#### `uart.revoked`
Sent to the previous holder when a cooperative `uart.open` kicks them
off.
```json
{ "by": "rpc-client" }
```

---

## `/rpc/can/<n>`

Generic CAN frame pipe (classical CAN; no CAN-FD). Index map:

- `/0` → TWAI0 / TJA1051T #1 (`BOARD_CAN0_*` pins)
- `/1` → TWAI1 / TJA1051T #2 (`BOARD_CAN1_*` pins)

On boards where the transceiver isn't wired (either Waveshare dev
board → all `-1`), `can.open` returns `"can_not_present"`. Each index
is its own
lock domain — `/rpc/can/0` and `/rpc/can/1` can be held by different
clients simultaneously.

### `can.open`
```json
{
  "exclusive": false,
  "bitrate":   500000,      // 25000 50000 100000 125000 250000 500000 800000 1000000
  "mode":      "normal"     // "normal" | "listen-only" | "no-ack"
}
```
**Result**
```json
{ "ok": true, "bitrate": 500000, "mode": "normal", "exclusive": false }
```
Errors: `"bus_busy"`, `"can_not_present"`, `"unsupported_bitrate"`.

Drives the TJA1051T's `S` pin **low** (normal mode). On close it goes
high again (standby) so the bus is unloaded when idle.

### `can.configure`
Reconfigure while open. Same params. The driver is uninstalled and
re-installed (TWAI can't hot-swap timing), so a brief bus disconnect
happens.

### `can.send`
```json
{
  "id":   0x6F1,
  "ext":  false,             // 11-bit (false) vs 29-bit (true)
  "rtr":  false,             // Remote Transmission Request
  "data": "AQID"             // base64 — up to 8 bytes for classical
}
```
**Result**: `{ "ok": true }` or `{ "error": "<esp_err_name>" }` /
`"frame_not_object"` / `"data_too_long_for_classical_can"`.

### `can.sendBatch`
```json
{
  "frames": [
    { "id": 0x6F1, "data": "AQID" },
    { "id": 0x6F1, "data": "AwQF" }
  ]
}
```
**Result**: `{ "sent": 2, "requested": 2 }`. Stops on first failure.
Max 32 frames per call.

### `can.recover`
`{}` — initiate explicit bus-off recovery. Returns `{ "ok": true }`
or an esp error name.

### `can.close`
`{}` — release, uninstall driver, drive `S` high.

### Notifications

#### `can.rx`
```json
{ "id": 0x130, "ext": false, "rtr": false, "data": "QQA=", "ts": 1234567890 }
```
`ts` is microseconds since boot (off `esp_timer_get_time()`). Clients
filter their own IDs — hardware filtering accepts everything by
default.

#### `can.revoked`
```json
{ "by": "rpc-client" }
```

---

## Error codes

### HTTP / admin

- **400** — malformed query, traversal in path, body too large, bad JSON
- **404** — file/directory not found
- **405** — method not allowed
- **500** — file I/O failure, NVS write failure

### JSON-RPC standard

| Code     | Meaning                                          |
|----------|--------------------------------------------------|
| -32700   | Parse error (invalid JSON)                       |
| -32600   | Invalid request envelope                         |
| -32601   | Method not found                                 |
| -32602   | Invalid params                                   |
| -32603   | Internal error                                   |

### ediabasx `errorCode`

These come from the embedded VM (`edxn_error_t` in
`ediabasx-embedded/include/ediabasx/types.h`):

| Code | Name                       | Meaning                                  |
|------|----------------------------|------------------------------------------|
| 0    | `EDXN_OK`                  |                                          |
| 1    | `EDXN_ERR_NOMEM`           |                                          |
| 2    | `EDXN_ERR_INVALID_PRG`     | SGBD parse error                         |
| 3    | `EDXN_ERR_JOB_NOT_FOUND`   |                                          |
| 4    | `EDXN_ERR_ILLEGAL_OPCODE`  |                                          |
| 5    | `EDXN_ERR_STACK_OVERFLOW`  |                                          |
| 6    | `EDXN_ERR_STACK_UNDERFLOW` |                                          |
| 7    | `EDXN_ERR_DIV_ZERO`        |                                          |
| 8    | `EDXN_ERR_TRANSPORT`       | Wire-level failure; comm trap fired      |
| 9    | `EDXN_ERR_TABLE`           |                                          |
| 10   | `EDXN_ERR_FILE_IO`         |                                          |
| 11   | `EDXN_ERR_OPERAND`         |                                          |
| 12   | `EDXN_ERR_USER_BREAK`      | `break` opcode (BIP_0008)                |
| 13   | `EDXN_ERR_TRAP`            | `eerr` opcode raised a trap              |
| 14   | `EDXN_ERR_GENERR`          | User-requested error (`generr`)          |
| 15   | `EDXN_ERR_BAD_FLOAT`       | Inf/NaN (BIP_0011)                       |

### UART / CAN string errors

Per-component machine-readable strings returned inside `result.error`:

| Component | Code                              | Meaning                                |
|-----------|-----------------------------------|----------------------------------------|
| `uart.*`  | `uart_not_present`                | Index has no UART wired on this board  |
| `uart.*`  | `bus_busy`                        | Held by another client (exclusive)     |
| `uart.*`  | `not_holder`                      | Call requires the open holder          |
| `uart.*`  | `bad_data` / `bad_base64_or_empty`| Malformed `data` field                 |
| `uart.*`  | `rx_task_failed`                  | Couldn't spawn the RX pump             |
| `can.*`   | `can_not_present`                 | Index has no transceiver on this board |
| `can.*`   | `bus_busy`                        | Held by another client (exclusive)     |
| `can.*`   | `not_holder_or_closed`            | Call requires an open holder           |
| `can.*`   | `unsupported_bitrate`             | Bitrate not one of the standard values |
| `can.*`   | `frame_not_object`                | `send` payload not a `{id,…}` object   |
| `can.*`   | `data_too_long_for_classical_can` | More than 8 bytes (FD not yet supported)|

ESP-IDF errors are passed through verbatim as `esp_err_to_name()`
strings (`ESP_ERR_TIMEOUT`, `ESP_ERR_INVALID_STATE`, …).
