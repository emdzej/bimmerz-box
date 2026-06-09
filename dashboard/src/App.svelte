<script lang="ts">
  import { onMount } from 'svelte'
  import { applyTheme, loadTheme, persistTheme, type Theme } from './theme.js'
  import { apps, type AppTile } from './tiles.js'

  let theme = $state<Theme>(loadTheme())

  /**
   * Per-tile presence: have we confirmed `/sdcard/web/<app>/` exists on
   * the dongle? Three states:
   *   - `undefined` — still probing or API unreachable (renders normally)
   *   - `false`     — confirmed missing (renders dimmed, click disabled)
   *   - `true`      — confirmed present
   *
   * We can't disambiguate via HEAD on `/<app>/index.html` because the
   * dongle's SPA fallback for unknown paths serves the dashboard's own
   * index.html (HTTP 200, wrong body). One authoritative call to the
   * admin file API is cleaner.
   */
  let present = $state<Record<string, boolean | undefined>>({})

  onMount(() => {
    probeInstalledApps()
  })

  async function probeInstalledApps(): Promise<void> {
    try {
      const r = await fetch('/api/files?path=/sdcard/web')
      if (!r.ok) return  // leave `present` undefined → tiles render normally
      const data = (await r.json()) as {
        entries?: Array<{ name: string; type: 'file' | 'dir' }>
      }
      const installed = new Set(
        (data.entries ?? [])
          .filter((e) => e.type === 'dir')
          .map((e) => e.name),
      )
      for (const tile of apps) {
        // href is "/ediabasx/" → segment "ediabasx"
        const seg = tile.href.replace(/^\/|\/$/g, '')
        present[tile.name] = installed.has(seg)
      }
    } catch {
      // API not reachable (dev mode, network blip) — leave undefined
    }
  }

  function toggleTheme(): void {
    theme = theme === 'light' ? 'dark' : 'light'
    applyTheme(theme)
    persistTheme(theme)
  }

  function splitAppName(name: string): { stem: string; x: string } {
    if (name.endsWith('X')) return { stem: name.slice(0, -1), x: 'X' }
    return { stem: name, x: '' }
  }
</script>

<div class="m-stripe" aria-hidden="true">
  <div class="m-stripe__band m-stripe__band--light"></div>
  <div class="m-stripe__band m-stripe__band--dark"></div>
  <div class="m-stripe__band m-stripe__band--red"></div>
</div>

<header>
  <h1 class="brand">bimmerz<span class="brand__box">box</span></h1>

  <div class="actions">
    <button
      class="icon-btn"
      type="button"
      onclick={toggleTheme}
      aria-label={theme === 'light' ? 'Switch to dark theme' : 'Switch to light theme'}
      title={theme === 'light' ? 'Switch to dark theme' : 'Switch to light theme'}
    >
      {#if theme === 'light'}
        <!-- moon icon -->
        <svg viewBox="0 0 24 24" width="20" height="20" aria-hidden="true">
          <path
            d="M21 12.79A9 9 0 1 1 11.21 3a7 7 0 0 0 9.79 9.79Z"
            fill="none" stroke="currentColor" stroke-width="2"
            stroke-linecap="round" stroke-linejoin="round"
          />
        </svg>
      {:else}
        <!-- sun icon -->
        <svg viewBox="0 0 24 24" width="20" height="20" aria-hidden="true">
          <circle cx="12" cy="12" r="4" fill="none" stroke="currentColor" stroke-width="2" />
          <g stroke="currentColor" stroke-width="2" stroke-linecap="round">
            <line x1="12" y1="2" x2="12" y2="5" />
            <line x1="12" y1="19" x2="12" y2="22" />
            <line x1="2" y1="12" x2="5" y2="12" />
            <line x1="19" y1="12" x2="22" y2="12" />
            <line x1="4.93" y1="4.93" x2="7.05" y2="7.05" />
            <line x1="16.95" y1="16.95" x2="19.07" y2="19.07" />
            <line x1="4.93" y1="19.07" x2="7.05" y2="16.95" />
            <line x1="16.95" y1="7.05" x2="19.07" y2="4.93" />
          </g>
        </svg>
      {/if}
    </button>

    <a
      class="icon-btn"
      href="/admin/"
      aria-label="Open admin settings"
      title="Settings (admin)"
    >
      <!-- gear icon -->
      <svg viewBox="0 0 24 24" width="20" height="20" aria-hidden="true">
        <path
          d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"
          fill="none" stroke="currentColor" stroke-width="2"
          stroke-linecap="round" stroke-linejoin="round"
        />
        <circle cx="12" cy="12" r="3" fill="none" stroke="currentColor" stroke-width="2" />
      </svg>
    </a>
  </div>
</header>

<main>
  <ul class="tiles">
    {#each apps as tile (tile.href)}
      {@const parts = splitAppName(tile.name)}
      {@const isPresent = present[tile.name]}
      {@const disabled = isPresent === false}
      <li class="tile" class:tile--disabled={disabled}>
        {#if disabled}
          <div class="tile__inner" aria-disabled="true">
            <span class="tile__name">
              <span>{parts.stem}</span><span class="tile__name-accent">{parts.x}</span>
            </span>
            <span class="tile__blurb">{tile.blurb}</span>
            <span class="tile__badge">not installed</span>
          </div>
        {:else}
          <a class="tile__inner" href={tile.href}>
            <span class="tile__name">
              <span>{parts.stem}</span><span class="tile__name-accent">{parts.x}</span>
            </span>
            <span class="tile__blurb">{tile.blurb}</span>
          </a>
        {/if}
      </li>
    {/each}
  </ul>
</main>

<style>
  header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 28px 32px 4px;
    max-width: 1100px;
    margin: 0 auto;
  }

  .brand {
    margin: 0;
    font-size: 28px;
    font-weight: 800;
    letter-spacing: -0.5px;
    background: linear-gradient(
      135deg,
      var(--m-light) 0%,
      var(--m-dark) 55%,
      var(--m-red) 100%
    );
    -webkit-background-clip: text;
    background-clip: text;
    color: transparent;
    display: inline-flex;
    align-items: baseline;
    gap: 6px;
  }
  .brand__box {
    font-size: 16px;
    font-weight: 600;
    letter-spacing: 0;
    background: none;
    color: var(--fg-muted);
    -webkit-text-fill-color: var(--fg-muted);
  }

  .actions {
    display: inline-flex;
    gap: 8px;
  }

  .icon-btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 36px;
    height: 36px;
    background: transparent;
    border: 1px solid var(--border);
    border-radius: 8px;
    color: var(--fg-muted);
    cursor: pointer;
    text-decoration: none;
    transition:
      border-color 0.15s ease,
      color 0.15s ease,
      background 0.15s ease;
  }
  .icon-btn:hover {
    border-color: var(--m-light);
    color: var(--fg);
    background: var(--tile-bg-hover);
  }
  .icon-btn:focus-visible {
    outline: 2px solid var(--m-light);
    outline-offset: 2px;
  }

  main {
    max-width: 1100px;
    margin: 0 auto;
    padding: 24px 32px 80px;
  }

  .tiles {
    list-style: none;
    padding: 0;
    margin: 0;
    display: grid;
    gap: 16px;
    grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  }

  .tile {
    /* The tile is just a sizing wrapper; the .tile__inner element
     * controls all visuals so the same styling applies to <a> and
     * <div> equivalents (present vs disabled tile). */
  }

  .tile__inner {
    display: flex;
    flex-direction: column;
    justify-content: center;
    align-items: center;
    gap: 8px;
    height: 150px;
    padding: 20px;
    background: var(--tile-bg);
    border: 1px solid var(--border);
    border-radius: 12px;
    box-shadow: var(--tile-shadow);
    text-align: center;
    color: var(--fg);
    text-decoration: none;
    transition:
      transform 0.18s ease,
      border-color 0.18s ease,
      box-shadow 0.18s ease,
      background 0.18s ease;
  }
  a.tile__inner:hover {
    transform: translateY(-2px);
    border-color: var(--m-light);
    background: var(--tile-bg-hover);
    box-shadow: var(--tile-shadow-hover);
  }
  a.tile__inner:focus-visible {
    outline: 2px solid var(--m-light);
    outline-offset: 2px;
  }

  .tile--disabled .tile__inner {
    opacity: 0.5;
    cursor: not-allowed;
    background: var(--bg);
  }

  .tile__name {
    font-size: 24px;
    font-weight: 700;
    letter-spacing: -0.3px;
  }
  .tile__name-accent {
    color: var(--m-red);
  }
  .tile__blurb {
    color: var(--fg-muted);
    font-size: 13px;
  }
  .tile__badge {
    color: var(--fg-faint);
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 2px 8px;
    margin-top: 4px;
  }

</style>
