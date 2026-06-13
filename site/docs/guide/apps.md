# Using the apps

The dongle ships with the bimmerz toolkit installed on the SD card.
Each tool is a single-page web app served from
`/sdcard/web/<app>/`. The dashboard shows a tile for each installed
app — greyed out for ones whose files aren't on the card.

## EDIABASX — diagnostic jobs

Opens at `http://172.16.7.1/ediabasx/`.

What it does: loads a BMW SGBD (`.prg` or `.grp` file), runs jobs
against your ECU, shows the results. This is the same engine the
dealer tool runs internally.

Workflow:

1. Pick an ECU from the SGBD list. The dongle scans
   `/sdcard/data/ediabas/ecu/` and lists everything it finds. For
   group files (e.g. `d_motor.grp`), the first job call
   auto-resolves to the right variant via IDENTIFIKATION.
2. Pick a job — `IDENT`, `STATUS_*`, `LESEN_*`, `FS_LESEN` (fault
   codes), `STEUERN_*` (actuator tests), etc.
3. Hit **Run**. The dongle drives the K-line / CAN, runs the SGBD
   bytecode, and the result sets appear.

You need the **BMW DATEN-disk** SGBD files for your chassis under
`/sdcard/data/ediabas/ecu/`. Upload them via `/settings/` or write them
directly via USB-MSC (see [Firmware updates](./firmware)).

## INPAX — live values

Opens at `http://172.16.7.1/inpax/`.

What it does: runs INPA `.ipo` scripts. Live data, ECU
configuration, the built-in diagnostic procedures BMW shipped with
the original scripts.

INPAX needs the dongle's K-line / CAN transport (via EDIABASX
underneath) plus the IPO script files on the SD card. Both come from
your INPA install — point INPAX at the right paths and the script
catalogue loads.

## NCSX — coding

Opens at `http://172.16.7.1/ncsx/`.

What it does: reads an ECU's current coding, presents it as
plain-English options (e.g. "DRL on with ignition: yes/no"), lets
you flip them, writes back.

Needs NCS-Expert profile + manifest files on the SD card (under
`/sdcard/data/ncs/` typically).

::: warning Coding is write-back
Bad coding will brick a module's function (most often: features stop
working, dash lights up). It's rarely physically destructive but
**always** save the current coding before changing anything. NCSX
auto-backs-up to the SD card before any write — keep those backups.
:::

## NFSX — flashing

Opens at `http://172.16.7.1/nfsx/`.

What it does: writes new firmware to ECUs. Three paths:

- **Direct DS2** — for E36/E38/E39/E46 ECUs that speak DS2 on K-line.
  Standard dealer session-mode programming.
- **C167 BSL bootmode** — for bench-pulled MS42 / MS43 / similar
  ECUs with the BOOT pin grounded. Uploads MiniMon to RAM and
  drives the AM29F400B flash directly.
- **IPO-driven** (legacy) — runs the original BMW `.ipo` flash
  scripts via the INPAX VM.

::: danger Flashing can brick
Wrong firmware to wrong ECU = paperweight. Backup the original
before writing. NFSX verifies sizes against per-ECU region tables
to catch the most common mistake (wrong file selected).
:::

## TUNEX — firmware image editor

Opens at `http://172.16.7.1/tunex/`.

What it does: opens a flash dump in a hex view, applies XDF
definitions (TunerPro format) for structured editing of tables and
scalars, saves a modified image you can re-flash with NFSX.

Pure editor — the dongle just serves it. The interesting work
happens in your browser.

## Loading data files

Two ways to get BMW data onto the dongle's SD card:

### Via the admin web UI

`/settings/` has a file browser. Click into `/sdcard/data/ediabas/ecu/`,
hit **Upload**, drop your `.prg` / `.grp` files. Good for a handful
of files.

### Via USB-MSC

Plug the dongle into a computer via USB while it's on. The SD card
appears as a removable drive. Copy files in bulk, eject, the
dongle remounts on the next reboot.

This is the right path for the full DATEN disk (2000+ files,
several hundred MB).
