import { useSelector } from "react-redux";
import { Navigate, Outlet } from "react-router";
import { can } from "../auth/permissions.js";
import { Stack, Text } from "../components/common/index.js";
import { homeFor } from "../config/navigation.js";
import { selectUser } from "../redux/auth/index.js";

/** Lets the page through only if the user holds `perm`; anyone else goes to a page they may open. */
export default function PermissionRoute({ perm }) {
  const user = useSelector(selectUser);
  if (can(user, perm)) return <Outlet />;
  return <HomeRedirect />;
}

/** "/" and unknown paths: the user's first page, or a plain notice when they may open none. */
export function HomeRedirect() {
  const user = useSelector(selectUser);
  const home = homeFor(user);
  if (!home) {
    return (
      <Stack align="center" justify="center" gap={1} sx={{ minHeight: "60vh", p: 4, textAlign: "center" }}>
        <Text variant="title">No pages to open</Text>
        <Text variant="caption">Your account has no access to any page yet. Ask an administrator.</Text>
      </Stack>
    );
  }
  return <Navigate to={home} replace />;
}
