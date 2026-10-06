import { useDispatch, useSelector } from "react-redux";
import { Navigate } from "react-router";
import ChangePasswordForm from "../components/ChangePasswordForm.jsx";
import { Text } from "../components/common/index.js";
import { HOME } from "../config/navigation.js";
import AuthLayout from "../layouts/AuthLayout.jsx";
import { selectUser, signOut } from "../redux/auth/index.js";

/** The forced step after signing in with a temporary password. */
export default function ChangePassword() {
  const dispatch = useDispatch();
  const user = useSelector(selectUser);

  if (!user) return <Navigate to="/login" replace />;
  if (!user.mustChangePassword) return <Navigate to={HOME} replace />;

  return (
    <AuthLayout>
      <Text>
        Welcome, <b>{user.displayName || user.username}</b>. You signed in with a temporary password; choose your
        own to continue.
      </Text>
      <ChangePasswordForm user={user} cancelLabel="Sign out" onCancel={() => dispatch(signOut())} />
    </AuthLayout>
  );
}
