/** Replay and live-follow cadences. */
export const SPEEDS = [
  { label: "0.5×", ms: 2000 },
  { label: "1×", ms: 1000 },
  { label: "2×", ms: 500 },
  { label: "4×", ms: 250 },
];

/**
 * Which layer a trace step belongs to, from its title:
 * 1 = which message do these bytes belong to (framing, tiers, LUT),
 * 2 = what does this byte mean (the mission database), 3 = the result.
 */
export function layerOf(title) {
  if (title.startsWith("Layer 1")) return 1;
  if (title.startsWith("Layer 2") || title === "The arithmetic") return 2;
  return 3;
}

/** Which part of the 1280-byte payload a byte offset falls in. */
export const tierOfByte = (i) => (i < 400 ? "VH" : i < 800 ? "H" : i < 1100 ? "M" : i < 1275 ? "L" : "rsv");
