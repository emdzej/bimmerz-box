import { mount } from 'svelte'
import App from './App.svelte'
import './app.css'
import { applyTheme, loadTheme } from './theme.js'

// Apply theme BEFORE Svelte mounts so the first paint already matches
// the saved choice (no flash of opposite theme).
applyTheme(loadTheme())

const target = document.getElementById('app')
if (target === null) throw new Error('Missing #app target in index.html')

mount(App, { target })
