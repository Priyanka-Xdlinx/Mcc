import { useSelector } from "react-redux";
import { Navigate, Outlet, useLocation } from "react-router";
import { selectSignedOutByUser, selectUser } from "../redux/auth/index.js";

/**
 * Signed-in pages only. A user on a temporary password must choose their own
 * before anything else (the backend refuses every other call until then).
 */
export default function PrivateRoute() {
  const user = useSelector(selectUser);
  const signedOutByUser = useSelector(selectSignedOutByUser);
  const location = useLocation();

  if (!user) {
    // Remember the page only when the session was missing, not after signing out.
    return <Navigate to="/login" replace state={signedOutByUser ? undefined : { from: location }} />;
  }
  if (user.mustChangePassword) return <Navigate to="/change-password" replace />;
  return <Outlet />;
}
