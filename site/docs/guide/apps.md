# Using the apps

The dongle ships with the bimmerz toolkit installed on the SD card.
Each tool is a single-page web app served from
`/sdcard/apps/<slug>/`. The dashboard hub itself lives at
`/sdcard/sys/dashboard/` and is served at `/`. The dashboard shows a
tile for each installed app — greyed out for ones whose files aren't
on the card.

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

## Getting the apps onto the SD card

Ready-to-ship dongles ship with the apps pre-loaded. If you're
building a DIY box (or replacing / updating an app on an existing
one), each app is built from its own repository — see that repo's
README for the exact build steps.

| Slug           | Repo                                                                            | Deploys to                        |
|----------------|---------------------------------------------------------------------------------|-----------------------------------|
| `dashboard`    | [`emdzej/bimmerz-box`](https://github.com/emdzej/bimmerz-box/tree/main/dashboard) *(this repo)* | `/sdcard/sys/dashboard/`          |
| `ediabasx`     | [`emdzej/ediabasx`](https://github.com/emdzej/ediabasx)                         | `/sdcard/apps/ediabasx/`          |
| `inpax`        | [`emdzej/inpax`](https://github.com/emdzej/inpax)                               | `/sdcard/apps/inpax/`             |
| `ncsx`         | [`emdzej/ncsx`](https://github.com/emdzej/ncsx)                                 | `/sdcard/apps/ncsx/`              |
| `nfsx`         | [`emdzej/nfsx`](https://github.com/emdzej/nfsx)                                 | `/sdcard/apps/nfsx/`              |
| `tunex`        | [`emdzej/tunex`](https://github.com/emdzej/tunex)                               | `/sdcard/apps/tunex/`             |

**General shape** (verify against each repo's README before running):

1. Clone the repo.
2. `pnpm install && pnpm build` (or the equivalent — the individual
   README will state exactly).
3. Upload the `dist/` (or `build/`) contents to the corresponding
   `/sdcard/...` path above, using either the settings file browser
   or USB-MSC (see [Loading data files](#loading-data-files) below —
   same mechanisms).

The dashboard `HEAD`-probes each app slug at load time; missing apps
render as dimmed "not installed" tiles instead of 404-ing when
clicked.

## App manifest

Each app folder under `/sdcard/apps/<slug>/` **can** ship an optional
`manifest.json` next to its `index.html`. The dashboard reads it at
load time and uses it to render the app's tile. Missing or unparseable
manifest → the tile still renders, using sensible defaults derived
from the folder slug — so a manifest is a nice-to-have, never a
hard requirement.

The schema (all fields optional):

| Field          | Type       | Default                              | Purpose                                                                                              |
|----------------|------------|--------------------------------------|------------------------------------------------------------------------------------------------------|
| `name`         | string     | slug uppercased (e.g. `EDIABASX`)    | Display name shown on the tile.                                                                      |
| `description`  | string     | empty string                         | Short tagline under the name.                                                                        |
| `version`      | string     | (unshown in current dashboard build) | App version. Reserved for future dashboard revisions.                                                |
| `icon`         | string     | (none)                               | Path (relative to the app folder) to an SVG / PNG icon.                                              |
| `accent`       | string     | trailing `"X"` if `name` ends in `X` | Trailing chars of `name` styled with the BMW M-tricolour accent. Set `""` to opt out.                |
| `category`     | string     | (unshown for now)                    | Future: dashboard groups tiles by this label.                                                        |
| `requires`     | `string[]` | `[]`                                 | Advisory capability list (e.g. `["can", "kline"]`). Displayed as hints; no runtime enforcement yet.  |

### Example — `ediabasx/manifest.json`

```json
{
  "name": "EDIABASX",
  "description": "Diagnostic jobs — DS2 / KWP2000",
  "version": "0.4.2",
  "icon": "icon.svg",
  "requires": ["kline", "can"]
}
```

The trailing `X` is picked up automatically by the accent heuristic —
no need to spell it out.

### Opting out of the accent

For apps whose name doesn't fit the `*X` convention (say `Settings`
or `Dashboard`), set `accent` to an empty string:

```json
{ "name": "Settings", "accent": "" }
```

### A different accent

To style a different suffix (e.g. treat `PRO` as the accent):

```json
{ "name": "TUNEPRO", "accent": "PRO" }
```

### Discovery flow (reference)

1. Dashboard calls `/api/files?path=/sdcard/apps` at load.
2. For each subdirectory returned, fetches `/<slug>/manifest.json`.
3. Renders a tile per slug, populating `name` / `description` / `icon`
   / accent from the manifest, or from the defaults above.
4. Tiles sort alphabetically by resolved `name`.

Canonical schema definition (kept in sync with the dashboard):
[`dashboard/src/tiles.ts`](https://github.com/emdzej/bimmerz-box/blob/main/dashboard/src/tiles.ts)
(look for the `AppManifest` type). No firmware or dashboard rebuild
is required to add a new app — drop the folder + manifest under
`/sdcard/apps/<slug>/` and refresh the dashboard.

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
