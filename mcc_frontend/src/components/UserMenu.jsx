import { useState } from "react";
import { useDispatch, useSelector } from "react-redux";
import { can } from "../auth/permissions.js";
import { selectIsSigningOut, selectUser, signOut } from "../redux/auth/index.js";
import ChangePasswordForm from "./ChangePasswordForm.jsx";
import UsersAdmin from "./UsersAdmin.jsx";
import { Box, Dialog, DropdownMenu, Text } from "./common/index.js";

/** The signed-in user in the header: change password, manage users (if permitted), sign out. */
export default function UserMenu() {
  const dispatch = useDispatch();
  const user = useSelector(selectUser);
  const signingOut = useSelector(selectIsSigningOut);
  const [dialog, setDialog] = useState(null);
  const name = user.displayName || user.username;

  const items = [
    { label: "Change password", onClick: () => setDialog("password") },
    ...(can(user, "USERS_VIEW") ? [{ label: "Manage users", onClick: () => setDialog("users") }] : []),
    { label: signingOut ? "Signing out…" : "Sign out", onClick: () => dispatch(signOut()), disabled: signingOut },
  ];

  return (
    <>
      <DropdownMenu
        triggerLabel={`Signed in as ${user.username}`}
        trigger={
          <>
            <Box
              aria-hidden
              display="inline-flex"
              alignItems="center"
              justifyContent="center"
              width={22}
              height={22}
              sx={{ borderRadius: "50%", bgcolor: "primary.main", color: "primary.contrastText", fontWeight: 600, fontSize: 12 }}
            >
              {name.slice(0, 1).toUpperCase()}
            </Box>
            <Box component="span" sx={{ display: { xs: "none", md: "inline" } }}>
              {name}
            </Box>
          </>
        }
        header={
          <>
            <Text variant="title">{user.username}</Text>
            <Text variant="caption">{user.role === "ADMIN" ? "Administrator" : "Operator"}</Text>
          </>
        }
        items={items}
      />
      {dialog === "password" && (
        <Dialog title="Change password" onClose={() => setDialog(null)}>
          <ChangePasswordForm user={user} onDone={() => setDialog(null)} onCancel={() => setDialog(null)} />
        </Dialog>
      )}
      {dialog === "users" && (
        <Dialog title="Users" maxWidth="lg" onClose={() => setDialog(null)}>
          <UsersAdmin me={user} canManage={can(user, "USERS_MANAGE")} />
        </Dialog>
      )}
    </>
  );
}
