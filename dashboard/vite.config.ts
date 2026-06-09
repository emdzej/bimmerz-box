import { svelte } from '@sveltejs/vite-plugin-svelte'
import { defineConfig } from 'vite'

// The dashboard serves at the dongle's root (/), so all hashed asset
// paths in the build are root-relative ("/assets/…"). No `base` override
// needed; vite's default '/' is correct.
export default defineConfig({
  plugins: [svelte()],
  server: {
    port: 5180,
    strictPort: false,
  },
})
