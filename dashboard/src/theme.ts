/**
 * Theme: light / dark with persisted choice. Defaults to OS preference
 * on first visit. Same key scheme as bimmerz-hub so a user who's set
 * a preference on the docs site lands with the same theme on the
 * dongle's dashboard.
 */
const STORAGE_KEY = 'bimmerz-hub:theme'
export type Theme = 'light' | 'dark'

export function loadTheme(): Theme {
  if (typeof window === 'undefined') return 'dark'
  const stored = window.localStorage.getItem(STORAGE_KEY)
  if (stored === 'light' || stored === 'dark') return stored
  const prefersDark =
    typeof window.matchMedia === 'function' &&
    window.matchMedia('(prefers-color-scheme: dark)').matches
  return prefersDark ? 'dark' : 'light'
}

export function applyTheme(theme: Theme): void {
  if (typeof document === 'undefined') return
  document.documentElement.dataset.theme = theme
}

export function persistTheme(theme: Theme): void {
  if (typeof window === 'undefined') return
  window.localStorage.setItem(STORAGE_KEY, theme)
}
