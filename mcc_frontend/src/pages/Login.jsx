import { useEffect, useRef, useState } from "react";
import { useDispatch, useSelector } from "react-redux";
import { Navigate, useLocation } from "react-router";
import { Button, ErrorBanner, Stack, Text, TextField } from "../components/common/index.js";
import { HOME } from "../config/navigation.js";
import AuthLayout from "../layouts/AuthLayout.jsx";
import {
  restoreSession,
  selectAuthError,
  selectBootUnreachable,
  selectIsRestoringSession,
  selectIsSigningIn,
  selectUser,
  signIn,
} from "../redux/auth/index.js";

/** The sign-in page: username and password. */
export default function Login() {
  const dispatch = useDispatch();
  const location = useLocation();
  const user = useSelector(selectUser);
  const error = useSelector(selectAuthError);
  const unreachable = useSelector(selectBootUnreachable);
  const checking = useSelector(selectIsRestoringSession);
  const busy = useSelector(selectIsSigningIn);
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const passwordRef = useRef(null);

  useEffect(() => {
    document.title = "Sign in · OrbLinx MCC";
  }, []);

  if (user) {
    const from = location.state?.from;
    return <Navigate to={from ? `${from.pathname}${from.search ?? ""}` : HOME} replace />;
  }

  const submit = async (e) => {
    e.preventDefault();
    if (busy) return;
    const result = await dispatch(signIn({ username: username.trim(), password }));
    if (signIn.rejected.match(result)) {
      setPassword("");
      passwordRef.current?.focus();
    }
  };

  if (unreachable) {
    return (
      <AuthLayout>
        <ErrorBanner>{error}</ErrorBanner>
        <Button variant="contained" size="large" loading={checking} onClick={() => dispatch(restoreSession())}>
          Try again
        </Button>
      </AuthLayout>
    );
  }

  return (
    <AuthLayout>
      <Stack component="form" gap={2} onSubmit={submit} noValidate>
        <ErrorBanner>{error}</ErrorBanner>
        <TextField
          label="Username"
          autoFocus
          required
          value={username}
          onChange={(e) => setUsername(e.target.value)}
          inputProps={{ autoComplete: "username", autoCapitalize: "none", spellCheck: false, maxLength: 64 }}
        />
        <TextField
          label="Password"
          type="password"
          required
          value={password}
          inputRef={passwordRef}
          onChange={(e) => setPassword(e.target.value)}
          inputProps={{ autoComplete: "current-password" }}
        />
        <Button type="submit" variant="contained" size="large" loading={busy} disabled={!username.trim() || !password}>
          Sign in
        </Button>
        <Text variant="caption">Accounts are created by an administrator.</Text>
      </Stack>
    </AuthLayout>
  );
}
