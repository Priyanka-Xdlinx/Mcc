import { useEffect, useState } from "react";
import { useSelector } from "react-redux";
import { selectTheme } from "../redux/app/index.js";

const darkQuery = () => {
  try {
    return window.matchMedia("(prefers-color-scheme: dark)");
  } catch {
    return null;
  }
};

/** The palette actually on screen, "light" | "dark", following the OS when the choice is "system". */
export default function useResolvedTheme() {
  const choice = useSelector(selectTheme);
  const [systemDark, setSystemDark] = useState(() => Boolean(darkQuery()?.matches));

  useEffect(() => {
    const mq = darkQuery();
    if (!mq) return undefined;
    const on = () => setSystemDark(mq.matches);
    mq.addEventListener("change", on);
    return () => mq.removeEventListener("change", on);
  }, []);

  return choice === "system" ? (systemDark ? "dark" : "light") : choice;
}
