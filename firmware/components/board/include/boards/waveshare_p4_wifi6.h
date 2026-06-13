#pragma once

// Waveshare ESP32-P4-WiFi6 Devkit pin assignments (phase 2 bring-up).
//
// Board reference: https://docs.waveshare.com/ESP32-P4-WIFI6
//
// Single-board design (no separate SoM + carrier). The C6 radio is
// onboard; SDIO + SD-card pin maps are inherited from the same family
// as the P4 Module DEV-KIT — cross-checked against the LCD-4.3
// variant's IDF examples (github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3,
// examples/esp-idf/05_sdmmc + 04_wifistation).
//
// Differences from `waveshare_p4_module_dev_kit.h` (the Module DEV-KIT):
//   - Ethernet PHY not confirmed present on the base WiFi6 board
//     (the LCD touchscreen variants don't break it out). Pinned to
//     `(-1)` here — flip BOARD_HAS_ETHERNET back to 1 and restore
//     the PHY pins once verified against the schematic.
//   - K-line, CAN, IBUS pin choices are bench-wiring decisions, not
//     board-specific. Kept identical to `waveshare_p4_module_dev_kit.h` so the same
//     mikroBUS / breakout transceiver rig is reusable.
//
// Anything marked `TBD` needs to be confirmed against the board
// schematic when hardware is in hand.

// ---- C6 link (ESP-Hosted over SDIO Slot 1, 4-bit, 40 MHz) -----------------
// Same as the Module DEV-KIT — onboard C6 wired to the standard P4
// SDIO Slot 1 pins.
#define BOARD_C6_SDIO_CLK_GPIO        18
#define BOARD_C6_SDIO_CMD_GPIO        19
#define BOARD_C6_SDIO_D0_GPIO         14
#define BOARD_C6_SDIO_D1_GPIO         15
#define BOARD_C6_SDIO_D2_GPIO         16
#define BOARD_C6_SDIO_D3_GPIO         17
#define BOARD_C6_RESET_GPIO           54   // TBD — confirm against schematic

// ---- SD card (SDMMC slot 0, 4-bit) ----------------------------------------
// Identical pin map to the Module DEV-KIT — same LDO IO ch 4 power
// topology (confirmed: LCD-4.3 example also uses the on-chip LDO
// power-control handle).
#define BOARD_SD_CLK_GPIO             43
#define BOARD_SD_CMD_GPIO             44
#define BOARD_SD_D0_GPIO              39
#define BOARD_SD_D1_GPIO              40
#define BOARD_SD_D2_GPIO              41
#define BOARD_SD_D3_GPIO              42
#define BOARD_SD_LDO_IO_CHANNEL       4

// ---- Ethernet PHY — not confirmed on the base WiFi6 devkit ----------------
// LCD variants drop the Ethernet PHY. Leave disabled until verified
// against the WiFi6 devkit schematic; the firmware tolerates -1 as a
// "no PHY" signal and skips bring-up.
#define BOARD_ETH_PHY_ADDR            (-1)
#define BOARD_ETH_PHY_RST_GPIO        (-1)
#define BOARD_ETH_MDC_GPIO            (-1)
#define BOARD_ETH_MDIO_GPIO           (-1)
#define BOARD_ETH_RMII_TX_EN_GPIO     (-1)
#define BOARD_ETH_RMII_TXD0_GPIO      (-1)
#define BOARD_ETH_RMII_TXD1_GPIO      (-1)
#define BOARD_ETH_RMII_RXD0_GPIO      (-1)
#define BOARD_ETH_RMII_RXD1_GPIO      (-1)
#define BOARD_ETH_RMII_CRS_DV_GPIO    (-1)
#define BOARD_ETH_RMII_REF_CLK_GPIO   (-1)

// ---- K-line via L9637D / ISO 9141 Click (off-board bench wiring) ----------
// Same as the Module DEV-KIT rig — identical mikroBUS / breakout
// wiring expected.
#define BOARD_KLINE_UART_NUM          UART_NUM_1
#define BOARD_KLINE_TX_GPIO           5
#define BOARD_KLINE_RX_GPIO           4
#define BOARD_KLINE7_EN_GPIO          (-1)
#define BOARD_KLINE8_EN_GPIO          (-1)
#define BOARD_LLINE_DRV_GPIO          (-1)
#define BOARD_DOIP_ACT_GPIO           (-1)

// ---- CAN (TWAI0 + TWAI1) --------------------------------------------------
// Same bench-rig assignments as the Module DEV-KIT — TJA1051T on
// TWAI0; second transceiver not yet wired.
#define BOARD_CAN0_TX_GPIO            33
#define BOARD_CAN0_RX_GPIO            26
#define BOARD_CAN0_STBY_GPIO          0
#define BOARD_CAN1_TX_GPIO            (-1)
#define BOARD_CAN1_RX_GPIO            (-1)
#define BOARD_CAN1_STBY_GPIO          (-1)

#define BOARD_IBUS_UART_NUM           UART_NUM_2
#define BOARD_IBUS_TX_GPIO            (-1)
#define BOARD_IBUS_RX_GPIO            (-1)

// ---- Status LEDs / buttons ------------------------------------------------
// Onboard buttons + user LEDs TBD against the schematic. Most
// Waveshare WiFi6 variants expose at least one user LED on a
// header-accessible GPIO; pin to be filled in.
#define BOARD_LED_PWR_GPIO            (-1)
#define BOARD_LED_WIFI_GPIO           (-1)
#define BOARD_LED_COMM_GPIO           (-1)
#define BOARD_LED_FAULT_GPIO          (-1)
#define BOARD_BTN_MULTI_GPIO          (-1)

// ---- VBAT sense -----------------------------------------------------------
#define BOARD_VBAT_ADC_CHANNEL        (-1)

// ---- Marker ---------------------------------------------------------------
#define BOARD_HAS_DIAGNOSTIC_HARDWARE 0
