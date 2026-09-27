import { useCallback, useEffect, useState } from 'react'

export type ThemeChoice = 'system' | 'light' | 'dark'

const KEY = 'mcc.theme'

function read(): ThemeChoice {
  try {
    const v = localStorage.getItem(KEY)
    return v === 'light' || v === 'dark' ? v : 'system'
  } catch {
    return 'system'
  }
}

function apply(choice: ThemeChoice) {
  const root = document.documentElement
  if (choice === 'system') delete root.dataset.theme
  else root.dataset.theme = choice
}

/** Light, dark, or follow the OS. The choice is remembered per browser. */
export function useTheme() {
  const [choice, setChoice] = useState<ThemeChoice>(read)

  useEffect(() => {
    apply(choice)
    try {
      if (choice === 'system') localStorage.removeItem(KEY)
      else localStorage.setItem(KEY, choice)
    } catch {
      /* private window: the choice lasts for this page only */
    }
  }, [choice])

  const set = useCallback((c: ThemeChoice) => setChoice(c), [])
  return { choice, set }
}

/**
 * The palette actually on screen ('light' | 'dark'), for canvas drawing (uPlot)
 * that reads colours once from CSS variables and must redraw when they change.
 */
export function useResolvedTheme(): 'light' | 'dark' {
  const read = (): 'light' | 'dark' => {
    const t = document.documentElement.dataset.theme
    if (t === 'light' || t === 'dark') return t
    try {
      return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'
    } catch {
      return 'light'
    }
  }
  const [resolved, setResolved] = useState(read)
  useEffect(() => {
    const update = () => setResolved(read())
    const mo = new MutationObserver(update)
    mo.observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] })
    let mq: MediaQueryList | null = null
    try {
      mq = window.matchMedia('(prefers-color-scheme: dark)')
      mq.addEventListener('change', update)
    } catch {
      /* no media queries: keep the attribute observer */
    }
    return () => {
      mo.disconnect()
      mq?.removeEventListener('change', update)
    }
  }, [])
  return resolved
}
