# Firmware updates

The dongle's firmware ships in two halves: the **P4 application
image** (binary, flashed into one of two 7 MB OTA partitions) and
the **SD-card assets** (web apps + BMW data files, dropped onto the
SD via USB-MSC or the admin UI).

## OTA — application image

Dual-partition layout: at any time one partition holds the **running**
firmware, the other is **standby**. An OTA push writes to the standby
slot, verifies it, then switches the next-boot pointer. If the new
image fails to boot cleanly, the bootloader rolls back to the
previously-running slot automatically.

### Pushing an OTA build

The dongle's `/admin/` page has an **OTA upload** form. Drop a built
`bimmerz_box.bin` file, wait for the upload to finish (a few seconds
over the AP), the dongle reboots into the new image.

For development you can also flash directly over USB-JTAG with the
ESP-IDF toolchain:

```sh
cd firmware
idf.py -p /dev/cu.usbmodem... flash
```

That bypasses OTA entirely and writes to flash directly. Useful when
the running image won't boot.

## SD-card assets — USB-MSC

Plug the dongle into a computer via USB. It exposes its SD card as a
USB Mass Storage device — a removable drive in your file manager.

What you upload here:

- **`/web/<app>/`** — the SPAs themselves. Each web app's
  `dist/` output drops into its own folder. The dashboard probes
  the folder list at boot — apps appear as enabled tiles on the
  hub once their folder exists.
- **`/data/`** — every BMW data file the installed apps need.
  Each app expects a particular layout under this root:
  EDIABASX reads `.prg` / `.grp` SGBDs from `/data/ediabas/ecu/`,
  NCSX reads NCS-Expert profile + manifest files from
  `/data/ncs/`, INPAX runs `.ipo` scripts from `/data/inpa/`, and
  newer tools document their paths in their own repos. The dongle
  doesn't care what's there — it just serves the namespace
  read-only at `http://172.16.7.1/data/`.

Eject the drive cleanly before unplugging. The dongle remounts FAT
on the next reboot.

::: tip USB-MSC vs admin upload
USB-MSC is the fast path for bulk loads — the whole DATEN disk
takes maybe 30 seconds. The admin upload is the right tool when
you're already on Wi-Fi, don't have a USB cable handy, and just
need to swap one file.
:::

## Reverting

The bootloader keeps the previous OTA slot intact. If a new firmware
boots cleanly the rollback marker is cleared; if it crashes on boot
or fails its health check, the next reboot lands on the previous
slot automatically.

To force a rollback without waiting for a crash, hold the dongle's
multi-purpose button for 10 seconds at power-on — the bootloader
treats that as a "boot the other slot" signal.

## Versions

- The current firmware version is shown in the hub's footer and via
  `GET /api/info`.
- Release notes live in
  [github.com/emdzej/bimmerz-box/releases](https://github.com/emdzej/bimmerz-box/releases).
- Built artefacts are produced by the firmware repo's CI. Download
  the `bimmerz_box.bin` from the release page, drop it on the OTA
  form.
