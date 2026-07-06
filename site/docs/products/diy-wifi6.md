# DIY — Waveshare ESP32-P4-WiFi6 devkit

An alternative to the [Module DEV-KIT modules path](./diy-modules) for
people who don't want the Ethernet-carrier stack and prefer a single
smaller board.

The [Waveshare ESP32-P4-WiFi6 Devkit](https://docs.waveshare.com/ESP32-P4-WIFI6)
puts the ESP32-P4 and an ESP32-C6 Wi-Fi 6 / BT 5.4 co-processor on
one board — no SoM + carrier split, no on-board Ethernet PHY, no
mikroBUS header. Wi-Fi, SD card, and USB-C flashing are all built in.
You wire external K-line and CAN transceivers to the pin headers, feed
the board 5 V from an automotive step-down, and you're done.

**Bench-verified.** The wiring in this guide matches what's running
against a real BMW K-line ECU on the bench today; K-line loop-through
is confirmed via the built-in `klineWireTest` RPC.

## What you'll need

### The brain

**Waveshare ESP32-P4-WiFi6 Devkit** — single-board ESP32-P4 + C6 combo,
16 MB flash, 32 MB PSRAM, microSD slot, USB-C.

- [Waveshare product page](https://www.waveshare.com/esp32-p4-wifi6.htm)
  · [official docs](https://docs.waveshare.com/ESP32-P4-WIFI6)
- The firmware ships a matching board overlay:
  `sdkconfig.defaults.waveshare_p4_wifi6`. Prebuilt binaries for this
  board variant are attached to every
  [GitHub release](https://github.com/emdzej/bimmerz-box/releases).

### K-line transceiver

**MikroE ISO 9141 Click** (L9637D on a mikroBUS carrier) is what the
bench rig uses. This board has no mikroBUS header, so you'll wire the
Click to the WiFi6 devkit's header with jumpers rather than plugging
it in. Any generic L9637D breakout works the same way.

- [MikroE product page](https://www.mikroe.com/iso-9141-click) — set
  the on-board jumper to **3.3 V logic**.

### CAN transceiver

Any **TJA1051T/3** breakout with a 3.3 V logic side. One transceiver
covers the D-CAN pair (OBD pins 6 / 14) that classic BMW chassis
use for OBD-II.

- Cheap generic modules from AliExpress / Adafruit / MikroE all work.
- The `S` (silent-mode select) pin needs to be driven — the firmware
  handles this from `/rpc/can/0` open. Don't leave it floating.

### Storage

**microSD card**, 16 GB SDHC, FAT32. Populate it with the SGBD / DATEN
payloads before first boot — see the
[web-apps guide](../guide/apps).

### OBD-II cable

**OBD-II to bare wires** or to a screw-terminal block. You'll wire
pin 6 / 14 (CAN), pin 7 (K-line), pin 16 (+12 V), and pin 4 or 5 (GND)
to the corresponding board / transceiver pins.

### Power

The board runs off 5 V. From the OBD-II port's always-live 12 V on
pin 16 you have two options:

- **Automotive 12 V → 5 V buck converter** — LM2596, MP1584, or any
  cheap generic module rated ≥1 A. Feed 5 V into the board's **VBUS**
  header pin (top-right of the header field, next to VSYS / GND / EN /
  3V3). Do **not** feed the board raw 12 V.
- **12 V-compatible USB-C car charger** — trim the USB cable, solder
  to VBUS / GND. Simpler mechanically at the cost of one dead cable.

Fuse the OBD +12 V wire with an inline 1 A fast-blow. The rig draws
~280 mA peak at 12 V input — a short in the wiring would otherwise
burn vehicle wiring, not the fuse.

**Bench testing** — skip the OBD-II side and plug the board's USB-C
into a laptop. VBUS is bridged internally, so the board powers up on
laptop 5 V and you can flash / monitor at the same time.

### Optional

- **SPDT toggle switch** in the OBD harness between pin 7 and pin 8,
  routed to the L9637D K-line input — see [Chassis-specific K-line
  routing](#chassis-specific-k-line-routing) below.
- **Small enclosure** — the WiFi6 devkit is small enough that a
  matchbox-sized 3D print holds it plus the two transceiver breakouts.

## Wiring

Full pinout with the setup diagram lives in the repo:
[`docs/wifi6-prototype.md`](https://github.com/emdzej/bimmerz-box/blob/main/docs/wifi6-prototype.md).
The relevant tables:

### GPIO ↔ transceiver

| Board pin      | Function               | Wire to                              |
|----------------|------------------------|--------------------------------------|
| GPIO **20**    | K-line UART1 **TX**    | L9637D TxD-in                        |
| GPIO **21**    | K-line UART1 **RX**    | L9637D RxD-out                       |
| GPIO **33**    | CAN0 TWAI **TX**       | TJA1051 TXD                          |
| GPIO **32**    | CAN0 TWAI **RX**       | TJA1051 RXD                          |
| GPIO **27**    | CAN0 STBY (**S**)      | TJA1051 S (silent, active-high)      |
| **VBUS**       | 5 V input              | Buck converter 5 V output            |
| **GND**        | 0 V                    | Buck GND, both transceivers' GND, OBD pins 4/5 |
| **3V3**        | 3.3 V logic rail       | Both transceivers' Vcc (logic side)  |

> **Naming gotcha.** On this board the K-line pin labels follow the
> L9637D **datasheet** (TxD = the transceiver's *input*, comes from
> the MCU's TX pin). CAN follows the **MCU perspective** (RX = the
> P4's TWAI RX line). Wire from the tables above and don't cross-
> reference them by feel — the two peripherals use opposite
> conventions.

### OBD-II ↔ transceiver

| OBD pin | Signal        | Wire to                       |
|---------|---------------|-------------------------------|
|   4     | Chassis GND   | Common GND rail               |
|   5     | Signal GND    | Common GND rail               |
|   6     | CAN-H         | TJA1051 CANH                  |
|   7     | K-line        | L9637D K-line pin (bus side)  |
|  14     | CAN-L         | TJA1051 CANL                  |
|  16     | +12 V (KL30)  | Buck converter input (fused)  |

L9637D transceiver **Vs** (12 V supply on the bus side) goes to the
same fused +12 V feed as the buck — the L9637D needs vehicle 12 V to
drive K-line low. Without it, K-line stays idle-high and the ECU never
hears the box.

### Chassis-specific K-line routing

Some BMW chassis carry the diagnostic K-line on OBD pin **8** instead
of pin 7 (or on both). Two ways to handle it on the prototype rig:

- **Solder a fixed jumper** between OBD pin 7 and pin 8 on the socket
  side of your harness. Cheapest and most reliable when the box will
  only ever plug into one chassis type.
- **Wire a physical SPDT toggle switch** into the harness so K-line
  goes to pin 7 *or* pin 8 (never both simultaneously). Use when the
  box moves between chassis families.

**Never short pin 7 to pin 8 directly.** Some chassis put a switched
+12 V feed on pin 8; a hard short would back-feed 12 V into the
L9637D's bus pin and destroy the transceiver instantly. The final PCB
handles this with a TMUX1208 analog switch driven by firmware
(`KLINE7_EN` / `KLINE8_EN`) — the DIY rig has no equivalent, so the
jumper / toggle stands in for it.

## Flash + boot

Two paths:

### From a GitHub release (fastest)

No ESP-IDF required. Grab the four `.bin` artefacts for the
`waveshare_p4_wifi6` variant from a
[GitHub release](https://github.com/emdzej/bimmerz-box/releases) and
push them with `esptool`:

```sh
pip install esptool
V=0.1.0
BOARD=waveshare_p4_wifi6
gh release download "$V" --repo emdzej/bimmerz-box \
  --pattern "bootloader-$BOARD-$V.bin" \
  --pattern "partition-table-$BOARD-$V.bin" \
  --pattern "ota_data_initial-$BOARD-$V.bin" \
  --pattern "bimmerz_box-$BOARD-$V.bin"

python -m esptool --chip esp32p4 -p /dev/cu.usbmodem<...> -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x2000  "bootloader-$BOARD-$V.bin" \
  0x8000  "partition-table-$BOARD-$V.bin" \
  0x10000 "ota_data_initial-$BOARD-$V.bin" \
  0x20000 "bimmerz_box-$BOARD-$V.bin"
```

The board's USB-C connector is the ESP32-P4's native USB-Serial-JTAG —
flash and console share that port. Windows / macOS / Linux all see it
as a plain CDC device (no drivers).

Full instructions with alternatives are in the
[repo README](https://github.com/emdzej/bimmerz-box#flash-prebuilt-binaries-from-github-releases).

### From source

If you'd rather build:

```sh
git clone https://github.com/emdzej/bimmerz-box
cd bimmerz-box/firmware
. $IDF_PATH/export.sh
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.waveshare_p4_wifi6" \
       set-target esp32p4
idf.py build
idf.py -p /dev/cu.usbmodem<...> flash monitor
```

The build pulls
[`ediabasx-embedded`](https://github.com/emdzej/ediabasx-embedded) as
a sibling repo via a path dependency — clone it next to
`bimmerz-box`.

## First boot check

Once flashed, the box brings up the `BimmerzBox` open Wi-Fi AP at
`172.16.7.1`. Join it, open `http://172.16.7.1/settings/`, and confirm:

- The **Firmware** card shows the running version + git hash.
- **Files** lists the SD-card contents (SGBDs / DATEN payloads).

To confirm the K-line hardware loop end-to-end, open the browser
DevTools console on any page under `http://172.16.7.1/` and run:

```js
const ws = new WebSocket('ws://172.16.7.1/rpc/ediabasx');
ws.onopen  = () => ws.send(JSON.stringify({jsonrpc:"2.0", id:1, method:"klineWireTest"}));
ws.onmessage = e => { console.log(e.data); ws.close(); };
```

Expected: `{"loopOk":true, "rxWhenTxHigh":1, "rxWhenTxLow":0, ...}`.
If `loopOk` is false, walk the K-line wiring — see the
[repo's prototype doc](https://github.com/emdzej/bimmerz-box/blob/main/docs/wifi6-prototype.md#bring-up-checklist)
for the debug steps.

## When to pick this path (vs the other DIY options)

- **This board**: you like the single-board form factor, don't need
  Ethernet, and want the smallest bench rig that still uses off-the-
  shelf transceivers.
- **[Module DEV-KIT modules path](./diy-modules)**: you want the
  mikroBUS-style header (click boards plug straight in), on-board
  Ethernet PHY for DoIP / ENET experiments, and a physically larger
  board to solder to.
- **[Custom PCB](./diy-pcb)**: you're going past prototype — one
  small board, on-board transceivers, OBD-II-dongle form factor.

## Cost

**TBC.** The board is cheaper than the Module DEV-KIT + carrier
combo; add the two transceiver breakouts and a buck module and you're
under what the modules path costs, though at the cost of losing the
Ethernet PHY and mikroBUS convenience.
