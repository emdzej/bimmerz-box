# Bimmerz Box — Hardware Specification

## 1. Overview

Bimmerz Box is a self-contained BMW diagnostic dongle that plugs into the
vehicle's OBD-II port. It hosts the [ediabasx](../../ediabasx) diagnostic
engine and serves the web rewrites of INPA, NCS-Expert, NFS, and TUNE from
its own Wi-Fi access point. Phones and laptops in the car connect to the
dongle's AP, open a browser to a fixed URL, and run all diagnostic and
coding workflows without external tooling.

### 1.1 Design goals

- Single board, OBD-mounted stick form factor (~95 × 30 × 18 mm).
- BMW chassis coverage from E-series (K-line, IBUS, D-CAN) to G-series (DoIP / ENET).
- Web apps are first-class clients; the dongle is the server. No PC required.
- Field-updatable over the air for both firmware and asset payloads.
- BOM target ≤ $35 in 100-unit volume.

### 1.2 Non-goals

- J2534 passthrough emulation. The web apps are the only intended frontend.
- Cellular / cloud connectivity. The dongle is AP-only inside the car.
- Vehicle-bus simulation or hardware-in-the-loop testing.
- Permanent installation in the vehicle (dongle draws from always-live OBD pin 16; an undervoltage cutoff protects the battery, but the user is expected to unplug between sessions).

## 2. Block diagram

```
   OBD pin 16 ─┬─[fuse]──[TVS]──[ideal-diode]──┬── VBAT (filtered ~12 V)
               │                               │
               │                               ├──► L9637D VS  (K-line bus pull-up)
               │                               ├──► P-FET high-side ──► OBD pin 8 (DoIP activate)
               │                               ├──► TH3122 VS (IBUS)
               │                               ▼
               │                          ┌─────────┐
               │                          │ TPS62933│  12 V → 5 V buck
               │                          └────┬────┘
               │                               ├──► L9637D VSS, transceiver logic rails
               │                               ▼
               │                          ┌─────────┐
               │                          │  3.3V   │  5 V → 3.3 V LDO
               │                          │  LDO    │
               │                          └────┬────┘
               │                               ├──► P4 3.3 V, C6, PHY, SD, switches, LEDs
               │   VBAT divider                ▼
               ├──► ADC sense to P4       ┌─────────┐
               │                          │ 3.3 V → │  P4 core 1.1 V rail
               │   ┌──[TLV3201]──► EN     │ 1.1 V   │
               └──►│  comparator          │ LDO     │
                   │  10.5 / 11.0 V       └─────────┘
                   └──────────► gates the 12 V→5 V buck EN

   ┌─────────────────────────────────────────────────────────────────────┐
   │                            ESP32-P4                                 │
   │                                                                     │
   │  ── SDIO 4-bit ──── ESP32-C6 ──── PCB inverted-F antenna            │
   │  ── SDIO 4-bit ──── microSD (push-push, side cutout)                │
   │  ── RMII (9 pins) ── LAN8720A ── HR911105A magjack ── OBD 3/11/12/13│
   │                                                                     │
   │  ── UART ── L9637D ──┬─[TMUX1208 ch1]── OBD pin 7                   │
   │                      └─[TMUX1208 ch2]── OBD pin 8                   │
   │  ── GPIO ── KLINE7_EN  ─► TMUX1208 ch1                              │
   │  ── GPIO ── KLINE8_EN ─┬─► TMUX1208 ch2                             │
   │                        │  (74LVC1G08 AND with !DOIP_ACT)            │
   │  ── GPIO ── DOIP_ACT ──┴─► P-FET high-side  ─►  OBD pin 8           │
   │                                                                     │
   │  ── GPIO ── LLINE_DRV ─► AO3416 N-FET  ─►  OBD pin 15               │
   │                                                                     │
   │  ── TWAI0 ── TJA1051T/3 ── CM choke ──  OBD pin 6 / 14              │
   │  ── UART ── TH3122 ──  3-pin JST (data / GND / +12 V)               │
   │                                                                     │
   │  ── USB OTG HS ── USB-C (5 V in + CDC-ACM console)                  │
   │  ── GPIO ×4 ── status LEDs (PWR / WIFI / COMM / FAULT)              │
   │  ── GPIO ×2 ── reset + multifunction buttons                        │
   │  ── ADC  ── VBAT divider sense                                      │
   └─────────────────────────────────────────────────────────────────────┘
```

## 3. Power subsystem

| Stage | Part | Notes |
|---|---|---|
| Input protection | SMBJ24CA TVS + 1 A polyfuse + LM74700 ideal-diode | TVS clamps load-dump residuals; ideal diode handles reverse polarity with minimal drop. |
| 12 V → 5 V | TPS62933 synchronous buck | ~1 A capability; EN pin gated by UVLO comparator. |
| 5 V → 3.3 V | AP2127 LDO (or equivalent) | 600 mA, low quiescent current. |
| 3.3 V → 1.1 V | small LDO for P4 core | ~250 mA peak. LDO chosen over buck for BOM simplicity; total dissipation ~0.25 W is acceptable. |
| UVLO | TLV3201 comparator | Trip at 10.5 V (disable), re-enable at 11.0 V (hysteresis). Output drives the buck EN pin. Protects vehicle battery from forgotten dongle. |
| VBAT sense | resistor divider to P4 ADC | Reports vehicle voltage to firmware for diagnostic display and logging. |

**Estimated 3.3 V load (peak):** ~750 mA.
- P4 + PSRAM active: 250 mA
- C6 in Wi-Fi TX: 250 mA
- LAN8720A: 60 mA
- microSD active write: 100 mA
- Transceivers + switches + LEDs: 80 mA

**Estimated input current at 12 V:** ~280 mA peak. Far inside the OBD-II socket's specified current envelope.

## 4. MCU subsystem

### 4.1 ESP32-P4 (application processor)

- Dual-core RISC-V, 400 MHz.
- 32 MB external PSRAM (octal SPI, soldered alongside the SoC or on the module).
- Internal flash sized for firmware partitions (16 MB target, dual-app OTA).
- Hosts: ediabasx VM, JSON-RPC server, HTTP server, FATFS on SD, ESP-Hosted master to C6.

### 4.2 ESP32-C6 (radio co-processor)

- Wi-Fi 6 (2.4 GHz only — Wi-Fi 6E 6 GHz not needed for in-car AP use).
- Bluetooth LE 5.x.
- Acts as a Wi-Fi NIC for the P4 via ESP-Hosted over SDIO 4-bit.
- Owns its own flash for radio firmware; OTA-updatable independently.

### 4.3 Interconnect

- P4 ↔ C6: SDIO 4-bit at 50 MHz (~25–50 Mbps useful throughput — orders of magnitude above what diagnostic traffic needs).
- P4 ↔ microSD: SDIO 4-bit on a separate SDMMC controller.

### 4.4 Antenna

- PCB inverted-F antenna on the C6 end of the board.
- Copper pour cutout under the antenna with a 50 Ω matching network footprint.
- Minimum 10 mm clearance from the RJ45 magjack (cable shield detunes 2.4 GHz).
- No U.FL connector — AP range only needs to cover the cabin (~3 m).

## 5. Storage

| Item | Choice | Rationale |
|---|---|---|
| microSD slot | push-push, side-cutout, hinge-less | End user rarely touches it; updates happen via OTA. |
| Card spec | up to 32 GB SDHC, FAT32 | Sufficient for all .prg / .grp / SG files + asset bundles. |
| Internal flash | 16 MB SPI (or module-internal) | Firmware partitions, NVS, OTA slots. |

## 6. Communication subsystems

### 6.1 K-line front-end

- One **L9637D** transceiver (BMW-grade, ISO 9141-2 / KWP2000 timings, integrated slew control, ±60 V bus protection).
- Output routed through a **TMUX1208** quad SPST analog switch (only 2 of 4 channels used; remaining 2 footprint-reserved).
- Independent GPIOs gate channels 1 (pin 7) and 2 (pin 8). Three operating modes:
  - **Pin 7 only** — F/G chassis or known-pin-7 ECUs.
  - **Pin 8 only** — rare; certain legacy modules.
  - **Both ON** — default for E-series unknown-target diagnostics. Pins 7 and 8 tie to the same K-line node at the switch outputs; safe for open-drain operation.
- L9637D VCC is tied to the 3.3 V rail (same as the P4 UART) — no level shifter needed. Verified on the bench with the MikroE ISO 9141 Click set to its 3.3 V jumper.
- L9637D VS pulled from VBAT (battery-direct, after protection).

### 6.2 L-line driver

- Output-only.
- **AO3416** logic-level N-FET drain-tied to OBD pin 15, source to GND.
- 24 V Zener gate-to-drain for transient protection.
- GPIO drives the gate; firmware bit-bangs the 5-baud KWP wake-up sequence.
- No receive path. L-line is used solely to wake legacy ECUs; subsequent traffic is on K-line.

### 6.3 CAN front-end

- **TJA1051T/3** transceiver (3.3 V logic side, 5 V supply, ±8 kV ESD on bus pins, slope-control).
- **744232 common-mode choke** + **PESD2CAN** dual TVS on the bus side for EMC margin.
- No on-board 120 Ω termination. The vehicle gateway terminates internally; the dongle sits on an OBD-port stub.
- Connected to P4's TWAI0 controller. TWAI1 reserved (footprint-optional second transceiver) for future multi-bus experimentation.
- Bus speed selectable in firmware (typically 500 kbps for D-CAN, 100 kbps for K-CAN-style buses).

### 6.4 IBUS

- **TH3122** single-wire-bus transceiver (BMW IBUS standard part).
- VS pulled from VBAT (after protection); logic side is 5 V and needs a level shifter to the P4's 3.3 V UART (a small voltage translator IC, e.g. a 74LVC1T45 channel — unlike the K-line side, IBUS isn't 3.3-V-tolerant).
- Connected to a P4 UART (separate from K-line UART).
- Exposed to the user via a **3-pin JST-PH side header**:
  - Pin 1: IBUS data
  - Pin 2: GND
  - Pin 3: switched +12 V (for optionally powering the dongle from the IBUS harness, off-OBD)
- IBUS is **not** routed to the OBD connector — the OBD-II spec does not carry it, and BMW IBUS lives on the radio harness / IKE rear connector on E38/E39/E46/E53.

### 6.5 Ethernet / ENET (DoIP)

- **LAN8720A** RMII PHY, 25 MHz crystal.
- **HR911105A** RJ45 magjack with integrated magnetics, LEDs, and shield tabs.
- 100BASE-TX only (sufficient for DoIP).
- Standard MDI wiring to OBD via the magjack:
  - PHY TX+ → magjack pin 1 → OBD pin 3
  - PHY TX- → magjack pin 2 → OBD pin 11
  - PHY RX+ → magjack pin 3 → OBD pin 12
  - PHY RX- → magjack pin 6 → OBD pin 13
- **DoIP activation:** a P-FET high-side switch connects VBAT (via a 510 Ω current-limit resistor) to OBD pin 8 when `DOIP_ACT` is asserted. The BMW gateway uses this voltage as the cue to enable its DoIP stack.

## 7. OBD-II pinout

| OBD pin | Function on dongle | Driver |
|---|---|---|
| 1 | unused | — |
| 2 | unused | — |
| 3 | ENET TX+ | LAN8720A via HR911105A |
| 4 | Chassis GND | direct |
| 5 | Signal GND | direct |
| 6 | CAN-H | TJA1051T/3 |
| 7 | K-line (always K-line role) | L9637D via TMUX1208 ch1 |
| 8 | K-line **or** DoIP activate +12 V | L9637D via TMUX1208 ch2 **xor** P-FET to VBAT (hard-interlocked) |
| 9 | unused | — |
| 10 | unused | — |
| 11 | ENET TX- | LAN8720A via HR911105A |
| 12 | ENET RX+ | LAN8720A via HR911105A |
| 13 | ENET RX- | LAN8720A via HR911105A |
| 14 | CAN-L | TJA1051T/3 |
| 15 | L-line | AO3416 N-FET (output only) |
| 16 | Battery +12 V | power input |

### 7.1 Chassis-specific wiring notes

Not every BMW chassis exposes every protocol on the OBD-II socket. The
dongle assumes the table above is wired through; if a particular
chassis routes a bus elsewhere, the owner has to tap it across.

**E46 (and similar M-OBD-only E-chassis):** D-CAN is **not** wired to
the OBD-II socket (`X19527`). For diagnostic tools that need CAN —
including this dongle when running CAN-side jobs — the most
convenient tap point is the instrument cluster (`IKE`) connector
`X11175` (black). Run a twisted pair from cluster to socket:

| Cluster `X11175` | OBD-II `X19527` | Signal               | Wire colour    |
|------------------|-----------------|----------------------|----------------|
| pin 9            | pin 6           | D-CAN-H              | yellow / red   |
| pin 10           | pin 14          | D-CAN-L              | yellow / brown |

**CAN topology caveat:** CAN was designed as a daisy-chained bus with
termination at the two ends, not a star. An OBD-II tap from the
cluster turns the dongle into a stub branch off the main bus. It
still works at 500 kbit/s with short stubs, but enable the
transceiver's onboard termination — or add a 120 Ω across CAN-H /
CAN-L at the dongle — so the dongle end is properly terminated. The
TJA1051T/3 in the BOM doesn't include internal termination; add an
external resistor or use a variant that does.

**Recommended parts:**

- Wire taps: **BMW p/n 61138364566** (TE / AMP `0-1393431-1`).
  Sealed quick-splice connector designed for the gauge of wire BMW
  uses in this loom. Don't substitute with hardware-store
  scotchlocks — they cold-flow and lose contact over time.

#### OBD-II socket — housing and crimp contacts

The OBD-II socket in BMW vehicles is **TE Connectivity `968915-1`**
(BMW OE `61138380698` / `8380698`). It mates **only with female
contacts from the Micro Timer II family** — tab size **1.6 × 0.6 mm**,
double locking lance. **MQS** contacts (0.63 mm tab — e.g. TE
`963715-1`) and **Micro Timer I** do **not** fit and will not latch.

Two contact sizes cover everything you'd terminate into the back of
the socket:

**Standard wires — 0.50 to 1.00 mm² (12 V supply on pin 16, grounds
on pins 4 and 5, main signal lines):**

| Plating | Reel / strip *(cut to length)* | Loose pieces *(hand-crimp friendly)* | Notes |
|---|---|---|---|
| **Gold (Au)** | `964263-3` *(sealed-style equivalent: `964274-3`)* | `964275-3` | Direct equivalent of BMW OE `61138364519`. Recommended — gold plating prevents tarnish and intermittent contact. |
| **Tin (Sn)** | `964263-2` *(sealed: `964274-2`)* | `964275-2` | Economy tin-plated alternative. |

**Thin wires — 0.20 to 0.60 mm² (individual K-line / CAN / Ethernet
diagnostic conductors, e.g. the IKE→OBD CAN tap above):**

| Plating | Reel / strip | Loose pieces | Notes |
|---|---|---|---|
| **Gold (Au)** | `969028-3` | `969019-3` | Direct equivalent of BMW OE `61138366598`. Designed for clean crimps on thin signal wire. |
| **Tin (Sn)** | `969028-2` | `969019-2` *(also `969005-2`)* | Tin-plated thin-wire variant. |

**Why "sealed / SWS" variants appear in the table.** The OBD-II
socket inside the cabin is not sealed against moisture, but contacts
designed for sealed connectors (`964274-x`, `964275-x`) share the
exact same contact-can geometry and locking-lance pitch as the
unsealed versions, so they latch and connect identically. They're
included here because the loose-piece SKUs are far easier to source
through general electronics retail than strictly-unsealed variants.
Just crimp directly onto the wire and skip the rubber seal.

## 8. Pin 8 hardware interlock (critical)

OBD pin 8 has two mutually-exclusive roles depending on chassis:

- **E-series:** secondary K-line carrying EWS / IKE / instrument-cluster diagnostics.
- **F/G-series:** DoIP activation line, driven to +12 V via a 510 Ω resistor by the diagnostic tester to enable the gateway's Ethernet stack.

Feeding 12 V into an L9637D bus pin destroys the transceiver. Therefore, the firmware **must never** simultaneously enable `KLINE8_EN` and `DOIP_ACT`.

Because software bugs happen, the design enforces this in hardware:

```
                         ┌─────────────────────────┐
   GPIO_KLINE8_EN ───────►│ AND                     │
                          │  ┌──────────────┐       │
                          │  │ 74LVC1G08    │──► to TMUX1208 ch2 EN
                          │  │              │       │
                          │  │              │       │
                          ▼  ▼              │       │
   GPIO_DOIP_ACT ──[NOT]────────────────────┘       │
                  74LVC1G04                         │
                                                    │
   GPIO_DOIP_ACT ────────────────────────────────► to P-FET gate
                  (separately, but firmware must
                   never assert with KLINE8_EN)
```

Equivalently: route `KLINE8_EN` through an AND gate whose other input is the inverted `DOIP_ACT`. The P-FET gate is driven directly by `DOIP_ACT`, but the K-line switch can only conduct on pin 8 when DoIP activation is off. The asymmetry is intentional — losing DoIP for a frame is recoverable; destroying the K-line transceiver is not.

A single 74LVC1G08 (AND) plus 74LVC1G04 (inverter) in SOT-353 packages costs $0.15 combined. Cheap insurance.

## 9. User I/O

### 9.1 LEDs

Four indicators on the top face of the enclosure:

| LED | Color | Meaning |
|---|---|---|
| PWR | green | 3.3 V rail up |
| WIFI | blue | AP started, at least one client capable |
| COMM | amber | blinks on ECU bus traffic (any interface) |
| FAULT | red | firmware-asserted error condition |

### 9.2 Buttons

| Button | Type | Function |
|---|---|---|
| RESET | recessed pinhole | hardware reset of the P4 |
| MULTI | side-mounted tactile | short press = trigger Wi-Fi info OLED page (future) / factory reset on long-press 10 s |

### 9.3 USB-C

Side-mounted USB-C jack with:
- 5 V power input (for bench development without a car).
- CDC-ACM serial console on P4's USB OTG HS, for logs and recovery.
- Optionally, USB MSC mass-storage exposure of the SD card for offline asset updates.

USB-C does **not** carry diagnostic data — all client interaction is over Wi-Fi.

## 10. Mechanical / form factor

```
 ┌───────────────────────────────────────────────────────────┐
 │  ┌────────┐                                               │
 │  │ OBD-II │   PCB (4-layer, controlled impedance)   ┌──┐ ◄── status LEDs ×4
 │  │  male  │   ┌───┐ ┌───┐ ┌─────┐ ┌─────┐ ┌──────┐  │  │     (top face)
 │  │ 16-pin │   │PWR│ │P4 │ │ C6  │ │ SD  │ │ PHY  │  │RJ│
 │  │        │   │   │ │   │ │+ant │ │card │ │      │  │45│
 │  └────────┘   └───┘ └───┘ └─────┘ └─────┘ └──────┘  └──┘
 │                                                           │
 └─────┬─────────────┬────────────────────┬──────────────────┘
       │             │                    │
   USB-C jack    IBUS JST           reset + multifunction
   (side)        (side)             buttons (side, recessed)
```

- **Dimensions:** ~95 × 30 × 18 mm.
- **PCB:** 4-layer, controlled-impedance stackup. RMII traces length-matched to ±25 mils. USB HS differential pair length-matched.
- **OBD connector:** board-mount 16-pin male, soldered through-hole for mechanical strength.
- **Enclosure:** 3D-printed or injection-molded shell, two halves snap-fit, with light pipes for the four indicator LEDs.
- **Antenna keep-out:** at least 10 mm copper-free zone around the PCB inverted-F, antenna on the end opposite the RJ45 jack.

## 11. Bill of materials

| Block | Part | Qty | $/100 |
|---|---|---|---|
| Application MCU | ESP32-P4 module w/ 32 MB PSRAM | 1 | 5.00–7.00 |
| Wi-Fi co-processor | ESP32-C6-MINI-1 | 1 | 2.00 |
| Ethernet PHY | LAN8720A | 1 | 1.50 |
| RJ45 magjack | HR911105A (or equivalent) | 1 | 0.50 |
| K-line transceiver | L9637D (SO-8) | 1 | 0.70 |
| K-line switch | TMUX1208 (quad SPST) | 1 | 0.60 |
| Logic level shifter | 74LVC2T45 | 1 | 0.20 |
| Interlock gate | 74LVC1G08 (AND) | 1 | 0.07 |
| Interlock inverter | 74LVC1G04 (NOT) | 1 | 0.07 |
| L-line N-FET | AO3416 | 1 | 0.05 |
| DoIP activation FET | AO3401 P-FET (high-side) | 1 | 0.10 |
| CAN transceiver | TJA1051T/3 | 1 | 0.50 |
| CAN choke + TVS | 744232 + PESD2CAN | 1 ea | 0.30 |
| IBUS transceiver | TH3122 | 1 | 2.50 |
| 12 V → 5 V buck | TPS62933 | 1 | 0.80 |
| 5 V → 3.3 V LDO | AP2127 | 1 | 0.15 |
| 3.3 V → 1.1 V LDO | MIC5219 (or similar small LDO) | 1 | 0.15 |
| UVLO comparator | TLV3201 | 1 | 0.30 |
| Input TVS | SMBJ24CA | 1 | 0.20 |
| Ideal-diode controller | LM74700-Q1 | 1 | 0.60 |
| Polyfuse | 1 A 0805 PTC | 1 | 0.10 |
| MicroSD slot | push-push, side cutout | 1 | 0.40 |
| OBD-II connector | board-mount male, 16-pin | 1 | 2.00 |
| USB-C jack | 6-pin power + data | 1 | 0.30 |
| IBUS JST | 3-pin JST-PH header | 1 | 0.10 |
| Crystal (PHY) | 25 MHz | 1 | 0.15 |
| LEDs ×4 | SMD 0805 | 4 | 0.20 |
| Tactile buttons ×2 | side-mount + recessed | 2 | 0.20 |
| Passives | resistors, capacitors, inductors | — | ~2.00 |
| PCB | 4-layer, ~95 × 30 mm | 1 | 3.00 |
| Enclosure | snap-fit 2-piece | 1 | 4.00 |
| **Total BOM target** | | | **~$28–32** |

## 12. Deferred / open items

- **ESP32-P4 module PN.** Espressif's P4-with-PSRAM module lineup is still consolidating; the final PN should be locked against the ESP-IDF release used at schematic capture time. Falling back to discrete P4 chip + external PSRAM is an option if no suitable module exists.
- **Secure boot keys and production provisioning flow.** Out of scope for prototype; required before any general distribution.
- **Regulatory certification.** Module-level FCC/CE certifications via the chosen P4 and C6 modules cover most of the radio side. Final assembly may still need EMC scan.
- **Schematic capture and PCB layout.** This spec is the input; the EDA work is the next deliverable after firmware architecture is settled.
- **Enclosure CAD.** Form-factor sketch above is indicative only.
