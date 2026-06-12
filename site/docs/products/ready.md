# bimmerz box — Ready-to-ship

For everyone who'd rather plug it in than build it. Production
units come assembled, tested, and pre-flashed.

## Status

**Pre-launch.** The custom PCB is in schematic review; production
runs start once the design is locked. We're holding a waitlist so
the first batch goes to people who actually want one.

[Join the waitlist →](#waitlist) (form at the bottom of this page,
or email `box@bimmerz.app`).

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

To be announced when the first batch is ready. Aiming for "less
than a half-decent OBD-II reader, more than a Chinese ELM327".

## Waitlist

We'll email you when:

1. The first batch is open for pre-order (price + ETA confirmed).
2. Subsequent batches ship.

We won't email you for anything else. To join, drop a line to
`box@bimmerz.app` with:

- Your name
- Which chassis you'd use it on (E36 / E39 / E46 / E60 / E83 / …)
- A guess at how many you'd want (1 is fine — gauges family
  pre-orders vs garage pre-orders)

Or [open a GitHub issue tagged `waitlist`](https://github.com/emdzej/bimmerz-box/issues/new?labels=waitlist&title=Waitlist%3A+%5Bchassis%5D)
if you'd rather keep it public.

## Support

Once you have a unit:

- **Hardware issues** — email with your serial number.
- **Firmware bugs** — file a GitHub issue at
  [github.com/emdzej/bimmerz-box](https://github.com/emdzej/bimmerz-box/issues).
- **Wiring / interpretation questions** — discussions on the same
  repo. The community + we will chip in.
