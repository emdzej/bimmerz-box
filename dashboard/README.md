# dashboard

Root-namespace launcher for the bimmerz-box dongle. Served by the
firmware at `http://172.16.7.1/` from `/sdcard/web/dashboard/` (see
`HUB_APP` in `firmware/components/http_static/src/http_static.c`).

The dashboard:

- Tiles for `ediabasx`, `inpax`, `ncsx`, `nfsx`, `tunex` — links are
  root-relative (`/ediabasx/`, etc.) so they resolve against whichever
  IP / mDNS / interface the user reached the dongle on.
- Gear icon in the header → `/admin/` (the in-flash admin UI baked into
  the firmware).
- HEAD-probes each app on load; tiles whose app folder isn't on the SD
  card render dimmed with a "not installed" badge instead of producing
  404s on click.
- BMW M-tricolour stripe + theme to match `bimmerz.app` and `bimmerz-hub`.

## Develop

```bash
pnpm install
pnpm dev        # http://localhost:5180
pnpm build      # output: dist/
```

`pnpm dev` runs against a Vite dev server with no dongle backend. The
gear icon and tile links will 404 (no `/admin/` or app prefixes locally);
useful for layout work but functionally hollow.

## Deploy to the dongle

After `pnpm build`, upload everything under `dist/` into
`/sdcard/web/dashboard/` on the dongle. Two paths:

1. **Admin file browser** — `http://172.16.7.1/admin/`, navigate into
   `/sdcard/web/`, create `dashboard/`, drag-drop the `dist/` contents
   in.
2. **`curl` against the admin file API** — quick scripted upload:
   ```bash
   cd dist
   find . -type f | while read f; do
     curl -X POST --data-binary "@$f" \
       "http://172.16.7.1/api/files/upload?path=/sdcard/web/dashboard/${f#./}"
   done
   ```

Refresh `http://172.16.7.1/` and the dashboard renders. The HEAD-probe
will then report which sibling apps are installed.
