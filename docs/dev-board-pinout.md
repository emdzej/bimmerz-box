# Waveshare ESP32-P4 Module DEV-KIT — Pinout

GPIO assignments used by the firmware on the Waveshare ESP32-P4 Module
DEV-KIT (the dev board the firmware is currently developed against —
[Waveshare docs](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT)).
The successor target is the
[ESP32-P4-WiFi6 Devkit](https://docs.waveshare.com/ESP32-P4-WIFI6) —
same SDIO + SD-card pin map, no Ethernet PHY (see
[`boards/waveshare_p4_wifi6.h`](../firmware/components/board/include/boards/waveshare_p4_wifi6.h)).

Definitions live in
[`firmware/components/board/include/boards/waveshare_p4_module_dev_kit.h`](../firmware/components/board/include/boards/waveshare_p4_module_dev_kit.h).

The ESP32-P4 SoC has 55 GPIOs (0–54), all routable through the GPIO
matrix. Five of them are **strapping pins** (`GPIO 34, 35, 36, 37, 38`,
sampled at reset) and two are **USB-JTAG by default** (`GPIO 24, 25` —
the dongle's `cu.usbmodem...` console + flash path uses these). The
rest are general-purpose.

---

## C6 link — ESP-Hosted over SDIO slot 1

The Wi-Fi/BLE co-processor (ESP32-C6) talks to the P4 over a 4-bit SDIO
bus at 40 MHz on SDMMC controller slot 1.

| Signal           | GPIO | Notes                                    |
|------------------|------|------------------------------------------|
| `C6_SDIO_CLK`    | 18   | clock                                    |
| `C6_SDIO_CMD`    | 19   | command line                             |
| `C6_SDIO_D0`     | 14   | data 0                                   |
| `C6_SDIO_D1`     | 15   | data 1                                   |
| `C6_SDIO_D2`     | 16   | data 2                                   |
| `C6_SDIO_D3`     | 17   | data 3                                   |
| `C6_RESET`       | 54   | active-low reset to the C6               |

Component: `firmware/components/c6_host/`.

> **Gotcha**: `SDMMC_HOST_DEFAULT()` picks slot 1 by default — the same
> slot the C6 link uses. The SD card driver explicitly sets
> `host.slot = SDMMC_HOST_SLOT_0` so the two don't collide.

---

## SD card — SDMMC slot 0

4-bit SDMMC on slot 0. Power comes from the P4's internal LDO (IO
channel 4), wired via `sd_pwr_ctrl_new_on_chip_ldo` in the SDMMC driver
init.

| Signal      | GPIO | Notes                                         |
|-------------|------|-----------------------------------------------|
| `SD_CLK`    | 43   | clock                                         |
| `SD_CMD`    | 44   | command line                                  |
| `SD_D0`     | 39   | data 0                                        |
| `SD_D1`     | 40   | data 1                                        |
| `SD_D2`     | 41   | data 2                                        |
| `SD_D3`     | 42   | data 3                                        |
| `SD_LDO`    |  —   | internal LDO IO channel **4** (not a GPIO)    |

Component: `firmware/components/storage/`.

---

## Ethernet — RMII to LAN8720A PHY

100 Mbit RMII; the PHY sources the 50 MHz REF_CLK on its REFCLK_OUT
pin, which the P4 reads as an input.

| Signal             | GPIO | Notes                                  |
|--------------------|------|----------------------------------------|
| `ETH_PHY_RST`      | 51   | active-low reset to the PHY            |
| `ETH_MDC`          | 31   | management clock                       |
| `ETH_MDIO`         | 52   | management data                        |
| `ETH_RMII_TX_EN`   | 49   |                                        |
| `ETH_RMII_TXD0`    | 34   | ⚠ strap pin (sampled at reset)         |
| `ETH_RMII_TXD1`    | 35   | ⚠ strap pin (also the BOOT button)     |
| `ETH_RMII_RXD0`    | 29   |                                        |
| `ETH_RMII_RXD1`    | 30   |                                        |
| `ETH_RMII_CRS_DV`  | 28   | carrier sense / data valid             |
| `ETH_RMII_REF_CLK` | 50   | 50 MHz input from PHY                  |

PHY address: **1**. Component: `firmware/components/c6_host/` /
`obd_hal` use Ethernet indirectly via the ESP-IDF EMAC driver.

> **Note**: GPIO 34 / 35 are strapping pins. Their states at reset
> matter for boot mode; the LAN8720A's outputs are tri-stated until the
> PHY comes out of its own reset (driven by `ETH_PHY_RST`), so by the
> time the strap value is sampled the line is in its safe default
> state. GPIO 35 is also the BOOT button on the DEV-KIT — don't press
> BOOT while Ethernet is initialising.

---

## K-line — UART1 to L9637D / MikroE ISO 9141 Click

A separate L9637D transceiver wires the dongle's K-line UART to a real
BMW ECU. On the DEV-KIT this is bench-wired through a MikroE ISO 9141
Click on the P6 mikroBUS header.

| Signal          | GPIO | Notes                                       |
|-----------------|------|---------------------------------------------|
| `KLINE_TX`      | 5    | P4 → click mikroBUS TX → L9637D pin 3       |
| `KLINE_RX`      | 4    | L9637D pin 4 → click mikroBUS RX → P4       |
| `KLINE7_EN`     | —    | (-1) — analog switch not present here       |
| `KLINE8_EN`     | —    | (-1)                                        |
| `LLINE_DRV`     | —    | (-1) — no L-line driver on the dev rig      |
| `DOIP_ACT`      | —    | (-1)                                        |

UART peripheral: **UART_NUM_1**. Component:
`firmware/components/transport_kline/`.

> **History**: K-line originally landed on GPIO 37/38, the UART0 default
> console pins. UART0 fought the UART1 reassignment and the bus stayed
> stuck. Moving to GPIO 5/4 (off the console pins) fixed it. Don't move
> K-line back to 37/38 unless you also redirect the console.

---

## CAN — not wired on the dev board

The DEV-KIT has no TJA1051T transceivers, so all CAN macros are `(-1)`
and `/rpc/can/<n>` returns `"can_not_present"` until the dongle PCB is
in hand. See [`docs/api.md`](api.md#rpccann) and the `dongle.h`
counterpart for the production pin map.

| Signal              | GPIO | Notes                                  |
|---------------------|------|----------------------------------------|
| `CAN0_TX/RX/STBY`   | —    | all (-1) on this board                 |
| `CAN1_TX/RX/STBY`   | —    | all (-1) on this board                 |

---

## IBus — UART2 (not wired)

| Signal       | GPIO | Notes                                       |
|--------------|------|---------------------------------------------|
| `IBUS_TX`    | —    | (-1) — TH3122 transceiver not on dev board  |
| `IBUS_RX`    | —    | (-1)                                        |

UART peripheral: **UART_NUM_2**. Reserved for the IBus transport when
the dongle PCB lands.

---

## Status LEDs / buttons / sense — none wired

| Signal              | GPIO | Notes                                  |
|---------------------|------|----------------------------------------|
| `LED_PWR`           | —    | (-1)                                   |
| `LED_WIFI`          | —    | (-1)                                   |
| `LED_COMM`          | —    | (-1)                                   |
| `LED_FAULT`         | —    | (-1)                                   |
| `BTN_MULTI`         | —    | (-1)                                   |
| `VBAT_ADC_CHANNEL`  | —    | (-1)                                   |

The DEV-KIT has BOOT (GPIO 35, also EMAC TXD1) and RST tactile
buttons; only RST is wired to the chip reset line. The firmware
doesn't claim BOOT — leave it alone unless Ethernet is unused.

`BOARD_HAS_DIAGNOSTIC_HARDWARE` is **0** on this board — the OBD HAL
treats undefined pins as no-ops and the firmware boots cleanly even
without transceivers.

---

## Summary — all assigned GPIOs

Sorted by GPIO number, with the subsystem that owns it:

| GPIO | Subsystem   | Signal                  |
|------|-------------|-------------------------|
|   4  | K-line      | `KLINE_RX` (UART1 RX)   |
|   5  | K-line      | `KLINE_TX` (UART1 TX)   |
|  14  | C6 link     | `C6_SDIO_D0`            |
|  15  | C6 link     | `C6_SDIO_D1`            |
|  16  | C6 link     | `C6_SDIO_D2`            |
|  17  | C6 link     | `C6_SDIO_D3`            |
|  18  | C6 link     | `C6_SDIO_CLK`           |
|  19  | C6 link     | `C6_SDIO_CMD`           |
|  28  | Ethernet    | `RMII_CRS_DV`           |
|  29  | Ethernet    | `RMII_RXD0`             |
|  30  | Ethernet    | `RMII_RXD1`             |
|  31  | Ethernet    | `MDC`                   |
|  34  | Ethernet    | `RMII_TXD0` ⚠ strap     |
|  35  | Ethernet    | `RMII_TXD1` ⚠ strap, BOOT button |
|  39  | SD card     | `SD_D0`                 |
|  40  | SD card     | `SD_D1`                 |
|  41  | SD card     | `SD_D2`                 |
|  42  | SD card     | `SD_D3`                 |
|  43  | SD card     | `SD_CLK`                |
|  44  | SD card     | `SD_CMD`                |
|  49  | Ethernet    | `RMII_TX_EN`            |
|  50  | Ethernet    | `RMII_REF_CLK` (input)  |
|  51  | Ethernet    | `PHY_RST`               |
|  52  | Ethernet    | `MDIO`                  |
|  54  | C6 link     | `C6_RESET`              |

**Total in use: 25 GPIOs** (out of 55).

---

## Free GPIOs on this board

Anything not in the table above is unclaimed by the firmware, *but*
some have hidden costs at the chip level:

| GPIO | Status                                                          |
|------|-----------------------------------------------------------------|
| 0    | ✅ free (not a strap, not USB-JTAG)                             |
| 1    | ✅ free                                                         |
| 2    | ✅ free                                                         |
| 3    | ✅ free                                                         |
| 6–13 | ✅ free                                                         |
| 20–23| ✅ free                                                         |
| 24   | ❌ USB-JTAG D+ — taking this kills `cu.usbmodem` console + flash |
| 25   | ❌ USB-JTAG D− — same                                           |
| 26   | ✅ free                                                         |
| 27   | ✅ free                                                         |
| 32   | ✅ free                                                         |
| 33   | ✅ free                                                         |
| 36   | ⚠️ **strap pin** — safe as output, risky as input               |
| 37   | ⚠️ strap pin                                                    |
| 38   | ⚠️ strap pin                                                    |
| 45   | ✅ free                                                         |
| 46   | ✅ free                                                         |
| 47   | ✅ free                                                         |
| 48   | ✅ free                                                         |
| 53   | ✅ free                                                         |

⚠️ For pins flagged "strap", their value is latched at reset to control
boot configuration. After boot they become regular GPIOs. Using them as
**outputs** is safe (you drive them only after boot). Using them as
**inputs** risks an external driver pulling them to the wrong state
during reset and breaking the boot config — avoid unless you know the
external driver's level at reset matches the strap default.

---

## Cross-reference

- **Dongle PCB pin map** — `firmware/components/board/include/boards/dongle.h`
- **Switching boards at build time** — set `BOARD_VARIANT` (waveshare_p4_module_dev_kit /
  dongle) via the appropriate `sdkconfig.defaults.<board>` overlay.
- **API endpoints that own each peripheral** — see [`docs/api.md`](api.md).
