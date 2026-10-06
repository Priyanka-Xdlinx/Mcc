import { useEffect } from "react";
import { useSelector } from "react-redux";
import { selectBootLoading } from "../redux/app/index.js";
import { selectIsRestoringSession } from "../redux/auth/index.js";
import hideBootLoader from "./hideBootLoader.js";

/**
 * Holds the routes until main.jsx's session check settles, so a signed-in user
 * never sees the sign-in page flash. The loader on screen meanwhile is static
 * markup in index.html; this only takes it down.
 */
export default function BootGate({ children }) {
  const bootLoading = useSelector(selectBootLoading);
  const restoring = useSelector(selectIsRestoringSession);
  const waiting = bootLoading || restoring;

  useEffect(() => {
    if (!waiting) hideBootLoader();
  }, [waiting]);

  return waiting ? null : children;
}
