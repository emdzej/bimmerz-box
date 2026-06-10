#pragma once

// Waveshare ESP32-P4 Module DEV-KIT pin assignments (phase 1 bring-up).
//
// Pinout confirmed against:
//   - Waveshare ESP32-P4-Platform examples (github.com/waveshareteam/ESP32-P4-Platform)
//   - Espressif ESP-Hosted reference docs (P4 Function EV Board topology)
//   - ESP-IDF ESP32-P4 EMAC defaults
//
// Most diagnostic transceivers do NOT exist on this board. The OBD HAL
// treats undefined pins as no-ops so the firmware still boots and serves
// the JSON-RPC + HTTP stack for end-to-end browser testing.

// ---- C6 link (ESP-Hosted over SDIO Slot 1, 4-bit, 40 MHz) -----------------
#define BOARD_C6_SDIO_CLK_GPIO        18
#define BOARD_C6_SDIO_CMD_GPIO        19
#define BOARD_C6_SDIO_D0_GPIO         14
#define BOARD_C6_SDIO_D1_GPIO         15
#define BOARD_C6_SDIO_D2_GPIO         16
#define BOARD_C6_SDIO_D3_GPIO         17
#define BOARD_C6_RESET_GPIO           54

// ---- SD card (SDMMC slot 0, 4-bit) ----------------------------------------
// The SD card's VDD on this board is powered from the P4's internal LDO IO
// channel 4 — the SDMMC driver MUST attach a `sd_pwr_ctrl_new_on_chip_ldo`
// power-control handle, otherwise the card never powers on and mount
// returns ESP_ERR_TIMEOUT.
#define BOARD_SD_CLK_GPIO             43
#define BOARD_SD_CMD_GPIO             44
#define BOARD_SD_D0_GPIO              39
#define BOARD_SD_D1_GPIO              40
#define BOARD_SD_D2_GPIO              41
#define BOARD_SD_D3_GPIO              42
#define BOARD_SD_LDO_IO_CHANNEL       4

// ---- Ethernet PHY (RMII; LAN8720A by default on Waveshare DEV-KIT) --------
// REF_CLK is sourced from the PHY (50 MHz on GPIO 50 input).
#define BOARD_ETH_PHY_ADDR            1
#define BOARD_ETH_PHY_RST_GPIO        51
#define BOARD_ETH_MDC_GPIO            31
#define BOARD_ETH_MDIO_GPIO           52
#define BOARD_ETH_RMII_TX_EN_GPIO     49
#define BOARD_ETH_RMII_TXD0_GPIO      34
#define BOARD_ETH_RMII_TXD1_GPIO      35
#define BOARD_ETH_RMII_RXD0_GPIO      29
#define BOARD_ETH_RMII_RXD1_GPIO      30
#define BOARD_ETH_RMII_CRS_DV_GPIO    28
#define BOARD_ETH_RMII_REF_CLK_GPIO   50

// ---- K-line via L9637D / ISO 9141 Click (off-board bench wiring) ----------
// Moved off GPIO 37/38 (which were also the UART0 default console pins) to
// GPIO 5 (TX) / GPIO 4 (RX) on the P6 header. UART1 still owns the
// peripheral; only the pin map changes.
#define BOARD_KLINE_UART_NUM          UART_NUM_1
#define BOARD_KLINE_TX_GPIO           5    // P4 → click mikroBUS TX (L9637D pin 3 via click trace)
#define BOARD_KLINE_RX_GPIO           4    // click mikroBUS RX (L9637D pin 4) → P4
#define BOARD_KLINE7_EN_GPIO          (-1) // analog switches not present in this rig
#define BOARD_KLINE8_EN_GPIO          (-1)
#define BOARD_LLINE_DRV_GPIO          (-1)
#define BOARD_DOIP_ACT_GPIO           (-1)

// CAN (no transceivers on the dev board — rpc_can returns can_not_present)
#define BOARD_CAN0_TX_GPIO            (-1)
#define BOARD_CAN0_RX_GPIO            (-1)
#define BOARD_CAN0_STBY_GPIO          (-1)
#define BOARD_CAN1_TX_GPIO            (-1)
#define BOARD_CAN1_RX_GPIO            (-1)
#define BOARD_CAN1_STBY_GPIO          (-1)

#define BOARD_IBUS_UART_NUM           UART_NUM_2
#define BOARD_IBUS_TX_GPIO            (-1)
#define BOARD_IBUS_RX_GPIO            (-1)

// ---- Status LEDs / buttons ------------------------------------------------
// The DEV-KIT has BOOT and RST tactile buttons; only RST is wired to the
// chip's reset line, BOOT is GPIO 35 (shared with EMAC TXD1 — leave alone
// unless you're sure Ethernet is unused).
#define BOARD_LED_PWR_GPIO            (-1)
#define BOARD_LED_WIFI_GPIO           (-1)
#define BOARD_LED_COMM_GPIO           (-1)
#define BOARD_LED_FAULT_GPIO          (-1)
#define BOARD_BTN_MULTI_GPIO          (-1)

// ---- VBAT sense -----------------------------------------------------------
#define BOARD_VBAT_ADC_CHANNEL        (-1)

// ---- Marker ---------------------------------------------------------------
#define BOARD_HAS_DIAGNOSTIC_HARDWARE 0
