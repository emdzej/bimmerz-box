#pragma once

// Custom Bimmerz Box dongle PCB pin assignments (phase 3).
//
// All values are placeholders until schematic capture pins them down.
// Replace each (-1) with the actual GPIO when the board is laid out.

// ---- C6 link (ESP-Hosted over SDIO 4-bit) ---------------------------------
#define BOARD_C6_SDIO_CLK_GPIO        (-1)
#define BOARD_C6_SDIO_CMD_GPIO        (-1)
#define BOARD_C6_SDIO_D0_GPIO         (-1)
#define BOARD_C6_SDIO_D1_GPIO         (-1)
#define BOARD_C6_SDIO_D2_GPIO         (-1)
#define BOARD_C6_SDIO_D3_GPIO         (-1)
#define BOARD_C6_RESET_GPIO           (-1)

// ---- SD card (SDIO 4-bit) -------------------------------------------------
#define BOARD_SD_CLK_GPIO             (-1)
#define BOARD_SD_CMD_GPIO             (-1)
#define BOARD_SD_D0_GPIO              (-1)
#define BOARD_SD_D1_GPIO              (-1)
#define BOARD_SD_D2_GPIO              (-1)
#define BOARD_SD_D3_GPIO              (-1)

// ---- Ethernet PHY (LAN8720A, RMII) ----------------------------------------
#define BOARD_ETH_PHY_ADDR            1
#define BOARD_ETH_PHY_RST_GPIO        (-1)

// ---- K-line (L9637D + TMUX1208) -------------------------------------------
// L9637D runs from 3.3 V on this board — no level shifter needed (VCC pin
// driven from the same 3V3 rail as the P4 UART). Confirmed on the bench
// against the MikroE ISO 9141 Click set to its 3.3 V jumper option.
#define BOARD_KLINE_UART_NUM          UART_NUM_1
#define BOARD_KLINE_TX_GPIO           (-1)   // → L9637D TxD (pin 3)
#define BOARD_KLINE_RX_GPIO           (-1)   // ← L9637D RxD (pin 4)
#define BOARD_KLINE7_EN_GPIO          (-1)   // TMUX1208 ch1 enable
#define BOARD_KLINE8_EN_GPIO          (-1)   // TMUX1208 ch2 enable (HW-interlocked with DOIP_ACT)

// ---- L-line (AO3416 N-FET, output-only) -----------------------------------
#define BOARD_LLINE_DRV_GPIO          (-1)

// ---- DoIP activation (P-FET high-side to OBD pin 8) -----------------------
#define BOARD_DOIP_ACT_GPIO           (-1)

// ---- CAN — two independent buses (each TJA1051T/3) ------------------------
// ESP32-P4 exposes three TWAI controllers via the GPIO matrix; we wire
// the first two transceivers here. Bus 0 is the HS-CAN candidate
// (PT-CAN, 500 kbit/s); bus 1 is the MS-CAN candidate (K-CAN,
// 100/125 kbit/s). Final allocation depends on the OBD harness.
//
// STBY (S pin on TJA1051T): drive low for normal mode, high for
// standby. At reset the GPIO floats and the transceiver's internal
// pull-up holds S high → safe "no CAN load" default until firmware
// brings the bus up.
#define BOARD_CAN0_TX_GPIO            33
#define BOARD_CAN0_RX_GPIO            26
#define BOARD_CAN0_STBY_GPIO          0

#define BOARD_CAN1_TX_GPIO            53
#define BOARD_CAN1_RX_GPIO            47
#define BOARD_CAN1_STBY_GPIO          1

// ---- IBUS (TH3122) --------------------------------------------------------
#define BOARD_IBUS_UART_NUM           UART_NUM_2
#define BOARD_IBUS_TX_GPIO            (-1)
#define BOARD_IBUS_RX_GPIO            (-1)

// ---- Status LEDs / buttons ------------------------------------------------
#define BOARD_LED_PWR_GPIO            (-1)
#define BOARD_LED_WIFI_GPIO           (-1)
#define BOARD_LED_COMM_GPIO           (-1)
#define BOARD_LED_FAULT_GPIO          (-1)
#define BOARD_BTN_MULTI_GPIO          (-1)

// ---- VBAT sense -----------------------------------------------------------
#define BOARD_VBAT_ADC_CHANNEL        (-1)

// ---- Marker ---------------------------------------------------------------
#define BOARD_HAS_DIAGNOSTIC_HARDWARE 1
