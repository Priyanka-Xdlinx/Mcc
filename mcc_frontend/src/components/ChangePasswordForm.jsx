import { useState } from "react";
import { useDispatch } from "react-redux";
import authApi from "../api/authApi.js";
import { errorMessage } from "../api/apiError.js";
import { MIN_PASSWORD, passwordProblem } from "../auth/passwordPolicy.js";
import { signedIn } from "../redux/auth/index.js";
import { Button, ErrorBanner, Stack, Text, TextField } from "./common/index.js";

/**
 * Current + new + confirm. Used as the forced step after signing in with a
 * temporary password, and from the account menu. On success the store gets
 * the updated user, then `onDone` runs.
 */
export default function ChangePasswordForm({ user, onDone, onCancel, cancelLabel = "Cancel" }) {
  const dispatch = useDispatch();
  const [current, setCurrent] = useState("");
  const [next, setNext] = useState("");
  const [confirm, setConfirm] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  const problem = next ? passwordProblem(next, user.username) : null;
  const mismatch = confirm !== "" && confirm !== next;
  const ready = current && next && !problem && confirm === next;

  const submit = async (e) => {
    e.preventDefault();
    if (!ready || busy) return;
    setError("");
    setBusy(true);
    try {
      dispatch(signedIn(await authApi.changePassword(current, next)));
      onDone?.();
    } catch (err) {
      setError(errorMessage(err, "The password was not changed."));
    } finally {
      setBusy(false);
    }
  };

  return (
    <Stack component="form" onSubmit={submit} noValidate gap={2}>
      <ErrorBanner>{error}</ErrorBanner>
      {/* lets password managers attach the new password to the right account */}
      <input type="text" name="username" autoComplete="username" value={user.username} readOnly hidden />
      <TextField
        label="Current password"
        type="password"
        autoFocus
        value={current}
        onChange={(e) => setCurrent(e.target.value)}
        inputProps={{ autoComplete: "current-password" }}
      />
      <TextField
        label="New password"
        type="password"
        value={next}
        error={Boolean(problem)}
        helperText={problem ?? `At least ${MIN_PASSWORD} characters. A few unrelated words make a strong one.`}
        onChange={(e) => setNext(e.target.value)}
        inputProps={{ autoComplete: "new-password" }}
      />
      <TextField
        label="Confirm new password"
        type="password"
        value={confirm}
        error={mismatch}
        helperText={mismatch ? "Does not match" : " "}
        onChange={(e) => setConfirm(e.target.value)}
        inputProps={{ autoComplete: "new-password" }}
      />
      <Stack direction="row" gap={1} justify="flex-end">
        <Button onClick={onCancel} disabled={busy}>
          {cancelLabel}
        </Button>
        <Button type="submit" variant="contained" disabled={!ready} loading={busy}>
          Change password
        </Button>
      </Stack>
      <Text variant="caption">Changing it signs you out on every other device.</Text>
    </Stack>
  );
}
