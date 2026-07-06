# WiFi6 devkit — OBD-II prototype rig

The [Waveshare ESP32-P4-WiFi6 Devkit](https://docs.waveshare.com/ESP32-P4-WIFI6)
is a single-board ESP32-P4 development board with an onboard C6
Wi-Fi 6 + BT 5.4 co-processor. Same SDIO + SD-card pin map as the
Module DEV-KIT, but no Ethernet PHY and no SoM/carrier split — the two
boards are parallel supported firmware variants, not a lineage.
This page documents the wiring used to bench-test the firmware against
a real BMW OBD-II port: K-line + CAN transceivers, an automotive 12 V
→ 5 V step-down module, and the OBD-II socket itself.

For the final PCB design (custom dongle, all transceivers on-board),
see [`hardware.md`](hardware.md).

## Setup diagram

```
                                                        ┌──────────────────────────┐
                                                        │   BimmerzBox AP (Wi-Fi)  │
                                                        │   172.16.7.1  (browser)  │
                                                        └────────────┬─────────────┘
                                                                     │ Wi-Fi 6
                                                                     │ (onboard C6)
                                                                     ▼
   OBD-II socket (looking into the car's DLC)                 ┌──────────────────────────────┐
                                                              │   Waveshare ESP32-P4-WiFi6   │
                                                              │              Devkit          │
   pin 16 (+12 V, always live)  ────► [inline fuse ~1 A]      │                              │
        │                              │                      │  VSYS ◄── 5 V ───┐           │
        │                              ▼                      │                  │           │
        │                       ┌──────────────┐  5 V, ~2 A   │  GND  ◄──────────┼───┐       │
        │                       │  12→5 V buck │──────────────►                  │   │       │
        │                       │  (LM2596 /   │              │  GPIO 20 ────────┼───┼──┐    │
        │                       │   MP1584 /   │              │  GPIO 21 ────────┼───┼──┼──┐ │
        │                       │   automotive │              │                  │   │  │  │ │
        │                       │   USB brick) │              │  GPIO 33 ────────┼───┼──┼──┼─┼──┐
        │                       └──────┬───────┘              │  GPIO 32 ────────┼───┼──┼──┼─┼──┼──┐
        │                              │ GND                  │  GPIO 27 ────────┼───┼──┼──┼─┼──┼──┼──┐
        ▼                              ▼                      └──────────────────┼───┼──┼──┼─┼──┼──┼──┘
   pin 4/5 (chassis / signal GND) ───► common GND rail ───────────────────────►  │   │  │  │ │  │  │
                                                                                 │   │  │  │ │  │  │
   pin 7 (K-line) ◄─────────────────►  ┌────────────────┐  L9637D RxD-out ───────────┘   │  │ │  │  │
                                       │    L9637D      │  L9637D TxD-in  ◄──────────────┘  │ │  │  │
                                       │ (K-line xcvr)  │                                   │ │  │  │
                                       │ Vs=+12V (fused │                                   │ │  │  │
                                       │  OBD pin 16)   │                                   │ │  │  │
                                       │ Vcc=+3.3V (3V3)│                                   │ │  │  │
                                       │ GND=common     │                                   │ │  │  │
                                       └────────────────┘                                   │ │  │  │
                                                                                            │ │  │  │
   pin 6  (CAN-H, D-CAN) ◄──────────►  ┌────────────────┐  TJA1051 TXD  ◄───────────────────┘ │  │  │
   pin 14 (CAN-L, D-CAN) ◄──────────►  │   TJA1051T/3   │  TJA1051 RXD  ─────────────────────►│  │  │
                                       │  (CAN xcvr)    │  TJA1051 S    ◄─────────────────────┘  │  │
                                       │ Vcc=+5V (VSYS  │                                        │  │
                                       │  from buck)    │                                        │  │
                                       │ VIO=+3.3V (3V3)│                                        │  │
                                       │ GND=common     │                                        │  │
                                       └────────────────┘                                        │  │
                                                                                                │  │
                                              (unused wires from board header ─────────────────►│  │
                                                to spare GPIOs / SD slot / debug UART)          │  │
                                                                                                │  │
   USB-C on the board is only needed for:                                                       │  │
     - initial flash from a host PC (see README §Flash prebuilt binaries)                       │  │
     - console monitoring via idf.py monitor                                                    │  │
   Once the box is in the car, VSYS is fed from the buck and USB-C stays disconnected.          │  │
   (VBUS is the USB-side 5 V — sourced from the host when USB-C is plugged. VSYS is             │  │
    the board's 5 V system rail into the on-board 3.3 V regulator — feed the buck here.)        │  │
```

## OBD-II pin usage

Standard 16-pin J1962 socket, driver's side looking at the connector:

| OBD pin | Signal        | Wired to                       | Notes                                                            |
|---------|---------------|--------------------------------|------------------------------------------------------------------|
|   4     | Chassis GND   | common GND rail                | Tie to pin 5. Ground return for the buck and both transceivers.  |
|   5     | Signal GND    | common GND rail                |                                                                  |
|   6     | CAN-H (D-CAN) | TJA1051T/3 CANH                | For E9x / F/E-series D-CAN. Older E-series (E39/E46) have no CAN on OBD — leave unwired. |
|   7     | K-line        | L9637D K-line pin              | DS2 (E36/E38/E39/E46) and KWP2000 buses share this pin.          |
|   8     | K-line (alt.) | *see note below*               | Some BMW chassis route the diagnostic K-line to pin 8 instead of (or in addition to) pin 7. |
|  14     | CAN-L (D-CAN) | TJA1051T/3 CANL                | Pair with pin 6.                                                 |
|  16     | +12 V (KL30)  | buck converter input (via fuse)| Always-live battery. Add an inline 1 A fast-blow fuse in the wire. |

> **OBD pin 8 (K-line routing per chassis)** — a few BMW chassis expose
> the K-line diagnostic bus on pin 8 rather than pin 7 (or on both).
> If your specific car needs pin 8, either:
>
> - **Solder a fixed jumper** between OBD pin 7 and pin 8 on the
>   socket side of the harness — cheapest and most reliable when the
>   box will only ever plug into one chassis type. On the final PCB
>   ([`hardware.md`](hardware.md) §4) this is handled by the TMUX1208
>   analog switch driven by `KLINE7_EN` / `KLINE8_EN`; the prototype
>   rig has no equivalent so the solder-jumper takes its place.
> - **Wire a physical SPDT toggle switch** into the OBD harness so pin
>   7 or pin 8 (never both simultaneously — a hard short between them
>   at the vehicle end can back-feed 12 V from OBD pin 8 into the
>   L9637D bus pin on chassis where pin 8 is not K-line but a switched
>   +12 V line, which destroys the transceiver instantly). Use this
>   when the box is shared across chassis families.
>
> Never wire OBD pin 8 directly to the L9637D without knowing what the
> target chassis puts on that pin. See the DoIP-activate discussion in
> [`hardware.md`](hardware.md) §7 for the same class of hazard on
> newer cars.

Pins **1, 2, 3, 9, 10, 11, 12, 13, 15** are not used by this
prototype. On the final PCB they carry manufacturer-specific routing
(DoIP activate on pin 8 for newer chassis, L-line on pin 15, Ethernet
on 3/11/12/13) — see [`hardware.md`](hardware.md).

## Board GPIO mapping (WiFi6 devkit)

From `firmware/components/board/include/boards/waveshare_p4_wifi6.h`:

| Signal            | GPIO | Wire to                              |
|-------------------|------|--------------------------------------|
| K-line UART1 TX   |  20  | L9637D TxD-in                        |
| K-line UART1 RX   |  21  | L9637D RxD-out                       |
| CAN0 TWAI TX      |  33  | TJA1051T/3 TXD                       |
| CAN0 TWAI RX      |  32  | TJA1051T/3 RXD                       |
| CAN0 STBY (S)     |  27  | TJA1051T/3 S (silent, active-high)   |

> **Naming gotcha** — on this board the K-line pin labels follow the
> **L9637D datasheet** (TxD = the transceiver's input, comes from the
> MCU's TX pin). CAN follows the **MCU perspective** (RX = the P4's
> TWAI RX line). Don't assume the two conventions match when hand-
> wiring; see the comment block in the header file.

## Transceiver power / logic rails

| Board pin | Supplies                                          | Notes                                                                       |
|-----------|---------------------------------------------------|-----------------------------------------------------------------------------|
| **VSYS**  | Buck output (5 V) → **TJA1051T/3 Vcc**            | Main transceiver supply. Also the board's own 5 V rail.                     |
| **3V3**   | Board 3.3 V → **TJA1051T/3 VIO** + L9637D logic Vcc | The TJA1051T/3 needs VIO tied to the MCU's I/O rail (3.3 V) so TXD/RXD levels match the P4 — this is separate from its 5 V Vcc, and skipping it leaves the RXD line at 5 V logic (out of spec for the P4). |
| **+12 V** (fused OBD 16) | **L9637D Vs** (bus-side)               | L9637D pulls K-line low against its internal 510 Ω pull-up to Vs — without 12 V here the bus stays idle and the ECU never responds. |
| **GND**   | Common ground rail                                | Both transceivers' GND + buck GND + OBD pins 4/5. Missing this is the #1 first-power-up bug.  |

## Power notes

- **Buck converter choice** — any automotive-rated 12 V → 5 V module
  rated ≥ 1 A works. The box draws ~280 mA peak at 12 V input; head-
  room accommodates SD-card write bursts and Wi-Fi TX. Common cheap
  options: LM2596 breakout, MP1584 module, or a step-down USB-A car
  charger with the USB cable trimmed and soldered to **VSYS + GND**
  on the board (not VBUS — that pin is the USB-side rail).
- **Fuse the OBD +12 V feed.** Pin 16 is always live; a wiring short
  will otherwise burn the vehicle wiring, not your board's protection.
  1 A fast-blow inline covers this rig's normal draw with margin.
- **Ground reference** — the buck's GND, both transceivers' GND, and
  the WiFi6 board's GND pin all tie to OBD pins 4/5. Missing this
  ground reference is a common first-power-up mistake: the board
  boots (USB-C-powered) but the transceivers see no signal ground and
  neither bus works.
- **Undervoltage protection is absent** on this prototype. The final
  PCB has a TLV3201 comparator that gates the buck EN at 10.5 / 11.0 V
  ([`hardware.md`](hardware.md) §3) — for bench use, unplug the OBD
  cable when you're done to avoid slowly draining the vehicle battery.

## Transceiver board choices

Any breakout with the right transceiver IC works. What we've bench-
tested:

- **K-line**: MikroE [*ISO 9141 Click*](https://www.mikroe.com/iso-9141-click)
  — this is the **L9637D013TR** breakout board (ST L9637D in SO-8 on a
  mikroBUS carrier). Pin **TX** on the carrier goes to L9637D TxD-in —
  that's the pin we drive from the P4's UART1 TX (GPIO 20). Pin **RX**
  goes to L9637D RxD-out → P4 UART1 RX (GPIO 21).
- **CAN**: any TJA1051T/3 breakout (SparkFun, generic Waveshare). The
  `S` pin is silent-mode select — the firmware drives it low to bring
  the transceiver out of standby on `/rpc/can/0` open, and high after
  close. Do not leave it floating; the internal pull-up parks it in
  standby, which stops CAN traffic dead.

## Bring-up checklist

1. Flash the firmware (see [README §Flash prebuilt binaries](../README.md#flash-prebuilt-binaries-from-github-releases)).
2. Confirm the SD card mounts by joining the `BimmerzBox` AP and
   loading `http://172.16.7.1/settings/` — it should list the SD-card
   contents under *Files*.
3. Confirm the K-line hardware loop with the diagnostic RPC. Open the
   browser DevTools console on any page under `http://172.16.7.1/`
   and run:

   ```js
   const ws = new WebSocket('ws://172.16.7.1/rpc/ediabasx');
   ws.onopen  = () => ws.send(JSON.stringify({jsonrpc:"2.0", id:1, method:"klineWireTest"}));
   ws.onmessage = e => { console.log(e.data); ws.close(); };
   ```

   Expected `{"loopOk":true,"rxWhenTxHigh":1,"rxWhenTxLow":0,"ok":"ok"}`.
   If `loopOk` is false, walk the wiring — see the mapping table above.
4. Confirm CAN 0 by opening `/rpc/can/0` from any client that speaks
   the CAN JSON-RPC. The endpoint answers `can_ready` with the pin
   report `TX=33 RX=32 STBY=27` in the boot log.

## Cross-reference

- [Module DEV-KIT pinout](dev-board-pinout.md) — the phase-1 board
  wiring, for comparison.
- [`boards/waveshare_p4_wifi6.h`](../firmware/components/board/include/boards/waveshare_p4_wifi6.h)
  — authoritative GPIO assignments.
- [Final PCB hardware spec](hardware.md) — the phase-3 target with all
  transceivers on-board.
