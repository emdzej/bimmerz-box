# Bimmerz Box firmware

ESP-IDF project for the Bimmerz Box OBD dongle. See `../docs/hardware.md`
and `../docs/firmware.md` for the design.

## Prerequisites

- ESP-IDF v5.3 or later, with the `esp32p4` target installed.
- A Waveshare ESP32-P4 Module DEV-KIT (phase 1) or the custom dongle PCB
  (phase 3).

## Build

The build picks a board variant via a sdkconfig defaults overlay.

```sh
# Phase 1: Waveshare dev board
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.waveshare" \
       set-target esp32p4
idf.py build

# Phase 3: custom dongle
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
