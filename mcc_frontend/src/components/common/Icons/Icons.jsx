// Line icons for buttons and tabs, drawn on a 24 px grid in the current text
// colour, in a spacecraft / mission-control style.
const PATHS = {
  // pages
  telemetry: "M3 12h3l2-6 4 12 2-6h7",
  charts: "M3 20h18M6 16l4-6 4 3 6-8M6 16v.01",
  decode: "M8 7l-5 5 5 5M16 7l5 5-5 5M13.5 5l-3 14",
  display: "M3 4h13v10H3ZM3 7h13M16 9h5v11H8v-6M16 12h5",
  docs: "M5 4h10l4 4v12H5ZM15 4v4h4M8 12h8M8 16h6",
  orbit: "M12 14a2 2 0 1 0 0-4 2 2 0 0 0 0 4ZM3 12c0-2.8 4-5 9-5s9 2.2 9 5-4 5-9 5-9-2.2-9-5Z",
  // feed, theme
  stop: "M7 7h10v10H7Z",
  sun: "M12 16a4 4 0 1 0 0-8 4 4 0 0 0 0 8ZM12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4",
  moon: "M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5Z",
  auto: "M12 21a9 9 0 1 0 0-18v18ZM12 3a9 9 0 0 1 0 18",
  satellite:
    "M9.5 9.5l5 5M8 11l-4.5-4.5L6.5 3.5 11 8M13 16l4.5 4.5 3-3L16 13M10 8l6 6-2 2-6-6ZM17 3a4 4 0 0 1 4 4M17 6a1 1 0 0 1 1 1",
  // actions
  plus: "M12 5v14M5 12h14",
  minus: "M5 12h14",
  x: "M6 6l12 12M18 6L6 18",
  reset: "M4 12a8 8 0 1 0 2.4-5.7M4 4v4h4",
  section: "M4 5h16v5H4ZM4 14h7v5H4ZM14 14h6v5h-6Z",
  trash: "M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3",
  clock: "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18ZM12 7v5l3 2",
  edit: "M4 20h4L19 9l-4-4L4 16ZM13.5 6.5l4 4",
  save: "M5 4h11l3 3v13H5ZM8 4v5h7V4M8 20v-6h8v6",
  copy: "M9 9h11v11H9ZM15 9V4H4v11h5",
  folder: "M3 6h6l2 2h10v11H3Z",
  grid: "M4 4h7v7H4ZM13 4h7v7h-7ZM4 13h7v7H4ZM13 13h7v7h-7Z",
  menu: "M4 7h16M4 12h16M4 17h16",
  up: "M6 15l6-6 6 6",
  down: "M6 9l6 6 6-6",
  user: "M12 12a4 4 0 1 0 0-8 4 4 0 0 0 0 8ZM4 21a8 8 0 0 1 16 0",
};

export default function Icon({ name, size = 16, className }) {
  return (
    <svg
      className={`ico ${className ?? ""}`}
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
      <path d={PATHS[name]} />
    </svg>
  );
}
