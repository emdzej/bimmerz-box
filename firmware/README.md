# Bimmerz Box firmware

ESP-IDF project for the Bimmerz Box OBD dongle. See `../docs/hardware.md`
and `../docs/firmware.md` for the design.

## Prerequisites

- ESP-IDF v5.4.4 with the `esp32p4` target installed.
- One of the supported dev boards (or the custom PCB):
  - [Waveshare ESP32-P4 Module DEV-KIT](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT) — phase 1 (SoM + carrier)
  - [Waveshare ESP32-P4-WiFi6 Devkit](https://docs.waveshare.com/ESP32-P4-WIFI6) — phase 2 (single-board)
  - Custom Bimmerz Box dongle PCB — phase 3

## Build

The build picks a board variant via a sdkconfig defaults overlay.

```sh
# Phase 1: Waveshare Module DEV-KIT
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.waveshare_p4_module_dev_kit" \
       set-target esp32p4
idf.py build

# Phase 2: Waveshare ESP32-P4-WiFi6 Devkit
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.waveshare_p4_wifi6" \
       set-target esp32p4
idf.py build

# Phase 3: custom dongle PCB
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.dongle" \
       set-target esp32p4
idf.py build
```

## Flash

```sh
idf.py -p /dev/cu.usbmodem<...> flash monitor
```

## Layout

```
firmware/
├── main/                  # app_main, board pin maps
├── components/            # one directory per logical subsystem
└── partitions.csv         # 16 MB flash layout, dual-OTA
```
