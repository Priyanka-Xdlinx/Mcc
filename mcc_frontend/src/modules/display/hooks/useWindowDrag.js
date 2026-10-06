import { useEffect, useRef, useState } from "react";
import { clamp, MIN_H, MIN_W } from "../utils/layout.js";

/** Pixels from the desk's top or bottom edge where dragging starts auto-scrolling. */
const EDGE = 40;

/**
 * Move and resize for one window. Geometry lives here while dragging, so other
 * windows never re-render; `onCommit(geo)` runs once, on pointerup, and only
 * when something changed.
 *
 * Returns the live geometry and `handle(mode)`, the pointer props for a drag
 * handle: "move" (title bar), "r" (right edge), "b" (bottom edge), "rb" (corner).
 */
export default function useWindowDrag(win, { onStart, onCommit }) {
  const [geo, setGeo] = useState({ x: win.x, y: win.y, w: win.w, h: win.h });
  const drag = useRef(null);

  // The desk changed this window (arrange, restore): follow it unless mid-drag.
  useEffect(() => {
    if (!drag.current) setGeo({ x: win.x, y: win.y, w: win.w, h: win.h });
  }, [win.x, win.y, win.w, win.h]);
  useEffect(
    () => () => {
      if (drag.current) cancelAnimationFrame(drag.current.raf);
    },
    [],
  );

  const apply = (d) => {
    const W = d.desk?.clientWidth ?? 1200;
    const dx = d.cx - d.px;
    // Pointer travel plus how far the desk has scrolled since the drag began.
    const dy = d.cy - d.py + (d.desk ? d.desk.scrollTop - d.s0 : 0);
    const g = d.g;
    const next =
      d.mode === "move"
        ? { ...g, x: clamp(g.x + dx, 0, W - g.w), y: Math.max(0, g.y + dy) }
        : {
            ...g,
            w: d.mode === "b" ? g.w : clamp(g.w + dx, MIN_W, W - g.x),
            h: d.mode === "r" ? g.h : Math.max(MIN_H, g.h + dy),
          };
    d.last = next;
    setGeo(next);
  };

  const start = (mode) => (e) => {
    if (e.button !== 0) return;
    if (mode === "move" && e.target.closest("button, input, select")) return;
    e.preventDefault();
    e.currentTarget.setPointerCapture(e.pointerId);
    const desk = e.currentTarget.closest(".dp-desk");
    const d = { mode, px: e.clientX, py: e.clientY, cx: e.clientX, cy: e.clientY, g: geo, last: geo, desk, s0: desk?.scrollTop ?? 0, raf: 0 };
    drag.current = d;
    // Scroll the desk while the pointer rests near its top or bottom edge.
    const tick = () => {
      if (drag.current !== d) return;
      if (d.desk) {
        const r = d.desk.getBoundingClientRect();
        const v = d.cy > r.bottom - EDGE ? d.cy - (r.bottom - EDGE) : d.cy < r.top + EDGE ? d.cy - (r.top + EDGE) : 0;
        if (v) {
          const before = d.desk.scrollTop;
          d.desk.scrollTop += clamp(Math.round(v / 2), -24, 24);
          if (d.desk.scrollTop !== before) apply(d);
        }
      }
      d.raf = requestAnimationFrame(tick);
    };
    d.raf = requestAnimationFrame(tick);
    onStart();
  };

  const move = (e) => {
    const d = drag.current;
    if (!d) return;
    d.cx = e.clientX;
    d.cy = e.clientY;
    apply(d);
  };

  const end = (e) => {
    const d = drag.current;
    if (!d) return;
    drag.current = null;
    cancelAnimationFrame(d.raf);
    if (e.currentTarget.hasPointerCapture(e.pointerId)) e.currentTarget.releasePointerCapture(e.pointerId);
    const g = d.last;
    if (g.x !== d.g.x || g.y !== d.g.y || g.w !== d.g.w || g.h !== d.g.h) onCommit(g);
  };

  const handle = (mode) => ({ onPointerDown: start(mode), onPointerMove: move, onPointerUp: end, onPointerCancel: end });

  return { geo, handle };
}
