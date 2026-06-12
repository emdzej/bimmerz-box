# bimmerz box — product site

VitePress site published at [box.bimmerz.app](https://box.bimmerz.app)
via GitHub Pages. Deployed automatically by
`.github/workflows/site-deploy.yml` on every push to `main` that
touches `site/**`.

## Local dev

```sh
pnpm install
pnpm dev        # http://localhost:5173
pnpm build      # → docs/.vitepress/dist
pnpm preview
```

## Layout

```
docs/
├── .vitepress/
│   ├── config.ts         — nav, sidebar, OG metadata, base URL
│   └── theme/            — M-tricolour stripe + palette overrides
├── public/
│   ├── CNAME             — box.bimmerz.app
│   └── icon.svg          — favicon
├── index.md              — hero + features
├── quickstart.md         — 5-step "plug it in and go"
├── guide/                — full user guide
│   ├── index.md
│   ├── connect.md
│   ├── apps.md
│   ├── firmware.md
│   └── troubleshooting.md
└── products/
    ├── index.md          — DIY vs ready compare table
    ├── diy.md            — build-your-own (BOM + KiCad + firmware)
    └── ready.md          — off-the-shelf option
```

## Companion repos referenced from the site

- [bimmerz-box](https://github.com/emdzej/bimmerz-box) — this repo (firmware, dashboard, KiCad PCB)
- [bimmerz](https://github.com/emdzej/bimmerz) — shared workspace (client libs, theme, UI components)
- [bimmerz.app](https://github.com/emdzej/bimmerz.app) — parent marketing site at `bimmerz.app`
