import React from 'react'
import ReactDOM from 'react-dom/client'
import { AuthGate } from './auth/AuthGate'
import { installAuthFetch } from './auth/auth'
// IBM Plex, bundled so the dashboard looks the same offline (same faces as the Orblinx platform).
import '@fontsource/ibm-plex-sans/400.css'
import '@fontsource/ibm-plex-sans/500.css'
import '@fontsource/ibm-plex-sans/600.css'
import '@fontsource/ibm-plex-sans-condensed/500.css'
import '@fontsource/ibm-plex-sans-condensed/600.css'
import '@fontsource/ibm-plex-mono/400.css'
import '@fontsource/ibm-plex-mono/500.css'
import './index.css'

// Before anything renders, so every fetch carries the CSRF header and renews an expired session.
installAuthFetch()

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <AuthGate />
  </React.StrictMode>,
)
