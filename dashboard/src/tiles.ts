/**
 * Apps hosted on this dongle. Tile hrefs are root-relative — they
 * resolve against whatever IP the user used to reach the dongle (AP,
 * Ethernet, mDNS, captive portal), so there's no hard-coded host.
 *
 * `present` is filled in at runtime by probing each `/<href>/` with a
 * HEAD request; tiles whose app folder isn't on the SD card render
 * dimmed so the dashboard makes the partial-install state obvious
 * rather than producing 404s on click.
 */
export type AppTile = {
  name: string
  href: string
  blurb: string
}

export const apps: AppTile[] = [
  { name: 'EDIABASX', href: '/ediabasx/', blurb: 'Diagnostic engine + SGBD runner' },
  { name: 'INPAX',    href: '/inpax/',    blurb: 'INPA scripts in the browser' },
  { name: 'NCSX',     href: '/ncsx/',     blurb: 'NCS-Expert coding' },
  { name: 'NFSX',     href: '/nfsx/',     blurb: 'ECU flashing' },
  { name: 'TUNEX',    href: '/tunex/',    blurb: 'Firmware editor + XDF' },
]
