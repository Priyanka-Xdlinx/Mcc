// Line icons for buttons and tabs, drawn on a 24 px grid with the current text colour,
// in a spacecraft / mission-control style.
const P: Record<string, string> = {
  // tabs
  dashboard: 'M12 3a9 9 0 1 0 9 9M12 12l5-5M16 3.5A9 9 0 0 1 20.5 8M12 12h.01',
  telemetry: 'M3 12h3l2-6 4 12 2-6h7',
  charts: 'M3 20h18M6 16l4-6 4 3 6-8M6 16v.01',
  track: 'M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18ZM3.6 9h16.8M3.6 15h16.8M12 3c2.5 2.7 3.5 5.7 3.5 9s-1 6.3-3.5 9c-2.5-2.7-3.5-5.7-3.5-9s1-6.3 3.5-9Z',
  decode: 'M8 7l-5 5 5 5M16 7l5 5-5 5M13.5 5l-3 14',
  docs: 'M5 4h10l4 4v12H5ZM15 4v4h4M8 12h8M8 16h6',
  xdlinx: 'M13 5.5l5.5 5.5M9.5 9l-4-4-2.5 2.5 4 4M15 14.5l4 4-2.5 2.5-4-4M9 15l6-6M10.5 10.5l3 3-3.5 3.5-3-3ZM19 3a2 2 0 0 1 2 2',
  // feed, theme
  play: 'M7 5l12 7-12 7Z',
  stop: 'M7 7h10v10H7Z',
  sun: 'M12 16a4 4 0 1 0 0-8 4 4 0 0 0 0 8ZM12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4',
  moon: 'M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5Z',
  auto: 'M12 21a9 9 0 1 0 0-18v18ZM12 3a9 9 0 0 1 0 18',
  // actions
  plus: 'M12 5v14M5 12h14',
  x: 'M6 6l12 12M18 6L6 18',
  check: 'M5 12.5l4.5 4.5L19 7',
  reset: 'M4 12a8 8 0 1 0 2.4-5.7M4 4v4h4',
  section: 'M4 5h16v5H4ZM4 14h7v5H4ZM14 14h6v5h-6Z',
  calc: 'M5 3h14v18H5ZM8 7h8M8 12h.01M12 12h.01M16 12h.01M8 16h.01M12 16h.01M16 16h.01',
  func: 'M15 4c-2 0-3 1-3.5 3L9 17c-.5 2-1.5 3-3.5 3M8 10h8M14 13l5 5M19 13l-5 5',
  trash: 'M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3',
  orbit: 'M12 14a2 2 0 1 0 0-4 2 2 0 0 0 0 4ZM3 12c0-2.8 4-5 9-5s9 2.2 9 5-4 5-9 5-9-2.2-9-5Z',
  satellite:
    'M9.5 9.5l5 5M8 11l-4.5-4.5L6.5 3.5 11 8M13 16l4.5 4.5 3-3L16 13M10 8l6 6-2 2-6-6ZM17 3a4 4 0 0 1 4 4M17 6a1 1 0 0 1 1 1',
  clock: 'M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18ZM12 7v5l3 2',
  api: 'M4 7h6M4 12h10M4 17h6M16 5l4 7-4 7',
}

export type IconName = keyof typeof P

export function Icon({ name, size = 16, className }: { name: IconName; size?: number; className?: string }) {
  return (
    <svg
      className={`ico ${className ?? ''}`}
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={1.8}
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      focusable="false"
    >
      <path d={P[name]} />
    </svg>
  )
}
