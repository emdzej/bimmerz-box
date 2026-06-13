/**
 * Apps hosted on this dongle are auto-discovered at runtime — no
 * hard-coded list. The dashboard:
 *   1. Lists `/sdcard/apps/` via the admin file API.
 *   2. Fetches `/<app>/manifest.json` for each subdirectory found.
 *   3. Builds a tile per manifest.
 *
 * Adding a new app means dropping its build output under
 * `/sdcard/apps/<slug>/` with a `manifest.json` next to `index.html`.
 * No firmware or dashboard rebuild required.
 *
 * Tile hrefs are root-relative (`/<slug>/`) so they resolve against
 * whatever IP the user used to reach the dongle (AP, captive portal,
 * future Ethernet).
 */

export type AppManifest = {
  /** Display name. Defaults to the directory slug uppercased if omitted. */
  name?: string
  description?: string
  version?: string
  /** Path (relative to the app's folder) to a tile icon SVG/PNG. */
  icon?: string
  /**
   * Highlight the trailing N chars of `name` with the M-accent style.
   * Default heuristic when omitted: split on a trailing `'X'`. Set
   * `accent: ""` to opt out entirely.
   */
  accent?: string
  /** Future: dashboard groups by this label when set. */
  category?: string
  /**
   * Capabilities this app needs. Dashboard can flag tiles whose
   * requirements aren't met (e.g. `"can"` when the dongle has no
   * TJA1051T wired). Strings are advisory — no schema enforcement yet.
   */
  requires?: string[]
}

export type AppTile = {
  /** URL path slug (the directory name under `/sdcard/apps/`). */
  slug: string
  /** Resolved display name. */
  name: string
  /** Resolved description (empty string when unset). */
  description: string
  /** Root-relative URL for the SPA. */
  href: string
  /** Raw manifest for the tile renderer to consult for optional fields. */
  manifest: AppManifest
}

type FilesEntry = { name: string; type: 'file' | 'dir' }

/**
 * Auto-discover apps. Resolves to an empty list when the dongle's
 * admin API is unreachable or the `apps/` directory is empty — the
 * dashboard renders an empty grid in that case rather than 404'ing.
 */
export async function discoverApps(): Promise<AppTile[]> {
  let entries: FilesEntry[]
  try {
    const r = await fetch('/api/files?path=/sdcard/apps')
    if (!r.ok) return []
    const data = (await r.json()) as { entries?: FilesEntry[] }
    entries = data.entries ?? []
  } catch {
    return []
  }

  const dirs = entries.filter((e) => e.type === 'dir')
  const tiles = await Promise.all(
    dirs.map(async (d): Promise<AppTile | null> => {
      let manifest: AppManifest = {}
      try {
        const r = await fetch(`/${d.name}/manifest.json`, {
          headers: { Accept: 'application/json' },
        })
        if (r.ok) manifest = (await r.json()) as AppManifest
      } catch {
        // App folder exists but no manifest — still render with a
        // sensible default name so the user sees there's something
        // installed but uncatalogued.
      }
      return {
        slug: d.name,
        name: manifest.name ?? d.name.toUpperCase(),
        description: manifest.description ?? '',
        href: `/${d.name}/`,
        manifest,
      }
    }),
  )

  return tiles
    .filter((t): t is AppTile => t !== null)
    .sort((a, b) => a.name.localeCompare(b.name))
}

/**
 * Split a name into stem + accent for the M-tricolour styling.
 *   - If manifest specifies `accent` and the name ends with it → use that.
 *   - If manifest specifies `accent: ""` → no accent.
 *   - Otherwise default heuristic: trailing `'X'` is the accent.
 */
export function splitAppName(
  name: string,
  accent: string | undefined,
): { stem: string; accent: string } {
  if (accent === '') return { stem: name, accent: '' }
  if (accent && name.endsWith(accent)) {
    return { stem: name.slice(0, -accent.length), accent }
  }
  if (name.endsWith('X')) return { stem: name.slice(0, -1), accent: 'X' }
  return { stem: name, accent: '' }
}
