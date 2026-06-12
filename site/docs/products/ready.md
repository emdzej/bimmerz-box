# bimmerz box — Ready-to-ship

For everyone who'd rather plug it in than build it. Production
units come assembled, tested, and pre-flashed.

## Status

**Pre-launch.** The custom PCB is in schematic review; production
runs start once the design is locked. We'll be accepting
pre-orders when we're ready — **timelines unknown** for now.

Details below describe the unit as it'll ship when the time comes.

## What you get

- **Assembled dongle** in a small enclosure (~55 × 46 × 15 mm).
- **OBD-II cable** — straight or right-angle, your choice at order
  time. Labelled for the chassis you've told us about.
- **microSD card** (16 GB) pre-loaded with the bimmerz hub +
  EDIABASX + INPAX + NCSX. NFSX / TUNEX are opt-in (and you can
  always add them later via USB-MSC).
- **USB-C cable** for power-only use (bench setups, OTA updates).
- **Quick-start card** — the [quick start](/quickstart) on paper.
- **Two-year warranty** on the hardware. Manufacturing defects,
  not "I drove over it".

## What's pre-flashed

- Latest stable firmware (auto-updates via OTA when you connect).
- Default Wi-Fi credentials. Change them on first connect — the
  card has a tear-off section with the unit's serial number for
  support purposes.
- Empty `/data/` namespace. You provide your own BMW DATEN-disk
  files (legal note: dealer toolchain files belong to BMW; we
  don't ship them).

## What we don't do

- **No telemetry.** The dongle never phones home. It doesn't even
  know what an internet looks like — it's AP-only.
- **No lock-in.** Same firmware as the DIY units, same OTA path,
  same web apps. You can wipe it, fork the firmware, and re-flash
  with your own build any time.
- **No proprietary cloud.** Everything runs locally on the dongle
  and your browser.

## Pricing

To be announced when the first batch is ready.

## Pre-orders

We'll open pre-orders when the hardware is finalised and we have a
realistic ship-date. **No timeline yet** — schematic review is
ongoing, the PCB hasn't been fabbed, and we'd rather not commit to
dates we can't keep.

If you want a heads-up when that changes, [watch the repo on
GitHub](https://github.com/emdzej/bimmerz-box) (Releases tab) —
we'll cut a release with the pre-order announcement.

## Support

Once you have a unit:

- **Hardware issues** — email with your serial number.
- **Firmware bugs** — file a GitHub issue at
  [github.com/emdzej/bimmerz-box](https://github.com/emdzej/bimmerz-box/issues).
- **Wiring / interpretation questions** — discussions on the same
  repo. The community + we will chip in.
