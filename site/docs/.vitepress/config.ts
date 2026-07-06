import { defineConfig } from "vitepress";

/**
 * box.bimmerz.app — product site for bimmerz box, the OBD diagnostic
 * dongle in the bimmerz family. Sibling site to bimmerz.app (the
 * suite landing) and the per-tool docs.
 *
 * `base` defaults to "/" because the site lives at the apex of its
 * subdomain. Override with `VITEPRESS_BASE=/some/path/` for preview
 * deploys under a sub-path.
 */
const base = process.env.VITEPRESS_BASE ?? "/";

export default defineConfig({
  base,
  title: "bimmerz box",
  description:
    "OBD diagnostic dongle for classic BMWs — plug into the car, connect to its Wi-Fi, run EDIABASX / INPAX / NCSX / NFSX in a browser tab.",
  cleanUrls: true,
  lastUpdated: false,
  head: [
    ["link", { rel: "icon", href: `${base}icon.svg`, type: "image/svg+xml" }],
    ["meta", { name: "theme-color", content: "#1c69d4" }],
    ["meta", { property: "og:type", content: "website" }],
    ["meta", { property: "og:title", content: "bimmerz box — OBD diagnostics from your phone" }],
    [
      "meta",
      {
        property: "og:description",
        content:
          "Plug a small Wi-Fi dongle into your BMW's OBD-II port and run the full bimmerz toolkit from any device's browser. Open hardware, open firmware.",
      },
    ],
  ],

  themeConfig: {
    nav: [
      { text: "Quick start", link: "/quickstart" },
      { text: "User guide", link: "/guide/" },
      {
        text: "Get one",
        items: [
          { text: "Overview", link: "/products/" },
          { text: "DIY — Module DEV-KIT", link: "/products/diy-modules" },
          { text: "DIY — WiFi6 devkit", link: "/products/diy-wifi6" },
          { text: "DIY — custom PCB", link: "/products/diy-pcb" },
          { text: "Ready-to-ship", link: "/products/ready" },
        ],
      },
      { text: "bimmerz.app", link: "https://bimmerz.app" },
    ],

    sidebar: {
      "/guide/": [
        { text: "Overview", link: "/guide/" },
        { text: "Connect a device", link: "/guide/connect" },
        { text: "Using the apps", link: "/guide/apps" },
        { text: "Firmware updates", link: "/guide/firmware" },
        { text: "Troubleshooting", link: "/guide/troubleshooting" },
      ],
      "/products/": [
        { text: "Compare the paths", link: "/products/" },
        {
          text: "DIY",
          items: [
            { text: "Pick your DIY path", link: "/products/diy" },
            { text: "Module DEV-KIT", link: "/products/diy-modules" },
            { text: "WiFi6 devkit", link: "/products/diy-wifi6" },
            { text: "Custom PCB", link: "/products/diy-pcb" },
          ],
        },
        { text: "Ready-to-ship", link: "/products/ready" },
      ],
    },

    socialLinks: [
      { icon: "github", link: "https://github.com/emdzej/bimmerz-box" },
    ],

    footer: {
      message:
        'Part of the <a href="https://bimmerz.app">bimmerz</a> family. Open hardware & firmware (MIT).',
      copyright: "© emdzej",
    },

    search: {
      provider: "local",
    },
  },
});
