import { useEffect, useState } from "react";
import usersApi from "../api/usersApi.js";
import { errorMessage } from "../api/apiError.js";
import { MIN_PASSWORD, passwordProblem } from "../auth/passwordPolicy.js";
import {
  Box,
  Button,
  ErrorBanner,
  SelectField,
  Stack,
  StatusChip,
  Table,
  TableBody,
  TableCell,
  TableContainer,
  TableHead,
  TableRow,
  Text,
  TextField,
} from "./common/index.js";

const ROLES = [
  { value: "OPERATOR", label: "Operator" },
  { value: "ADMIN", label: "Admin" },
];

const when = (iso) => (iso ? new Date(iso).toLocaleString() : "never");

function statusOf(a) {
  if (!a.enabled) return <StatusChip tone="bad" label="disabled" />;
  if (a.locked) return <StatusChip tone="warn" label="locked" />;
  if (a.mustChangePassword) return <StatusChip tone="info" label="temporary password" />;
  return <StatusChip tone="ok" label="active" />;
}

/**
 * Sign-in accounts: add, change role, enable/disable, set a temporary password,
 * delete. New and reset passwords are temporary: the user must choose their own
 * at the next sign-in. Without USERS_MANAGE (`canManage` false) the list is read-only.
 */
export default function UsersAdmin({ me, canManage }) {
  const [list, setList] = useState(null);
  const [error, setError] = useState("");
  const [notice, setNotice] = useState("");
  /** Bumped after every change, to load the list again. */
  const [version, setVersion] = useState(0);

  useEffect(() => {
    let dead = false;
    usersApi
      .list()
      .then((l) => !dead && setList(l))
      .catch((e) => !dead && setError(errorMessage(e, "Could not load the accounts.")));
    return () => {
      dead = true;
    };
  }, [version]);

  const run = async (what, done) => {
    setError("");
    setNotice("");
    try {
      await what();
      if (done) setNotice(done);
    } catch (e) {
      setError(errorMessage(e, "The change was not saved."));
    }
    setVersion((v) => v + 1);
  };

  const reset = (a) => {
    const pw = window.prompt(
      `Temporary password for ${a.username} (at least ${MIN_PASSWORD} characters).\n` +
        "They will be signed out everywhere and must choose a new one at next sign-in.",
    );
    if (pw == null) return;
    const problem = passwordProblem(pw, a.username);
    if (problem) return setError(problem);
    void run(() => usersApi.resetPassword(a.id, pw), `Password reset for ${a.username}.`);
  };

  const remove = (a) => {
    if (!window.confirm(`Delete ${a.username}? This cannot be undone.`)) return;
    void run(() => usersApi.remove(a.id), `${a.username} deleted.`);
  };

  return (
    <Stack gap={2}>
      <ErrorBanner>{error}</ErrorBanner>
      <ErrorBanner severity="success">{notice}</ErrorBanner>
      <TableContainer sx={{ maxHeight: 360 }}>
        <Table>
          <TableHead>
            <TableRow>
              <TableCell>User</TableCell>
              <TableCell sx={{ width: 140 }}>Role</TableCell>
              <TableCell sx={{ width: 150 }}>Status</TableCell>
              <TableCell sx={{ width: 170 }}>Last sign-in</TableCell>
              <TableCell sx={{ width: 260 }} />
            </TableRow>
          </TableHead>
          <TableBody>
            {list === null && (
              <TableRow>
                <TableCell colSpan={5}>Loading…</TableCell>
              </TableRow>
            )}
            {list?.map((a) => (
              <TableRow key={a.id}>
                <TableCell>
                  <b>{a.username}</b> {a.id === me.id && <Text component="span" variant="caption">(you)</Text>}
                  <Text variant="caption">{a.displayName}</Text>
                </TableCell>
                <TableCell>
                  {canManage ? (
                    <SelectField
                      minWidth={110}
                      value={a.role}
                      options={ROLES}
                      aria-label={`Role of ${a.username}`}
                      onChange={(e) => void run(() => usersApi.update(a.id, { role: e.target.value }))}
                    />
                  ) : (
                    <Text variant="caption">{ROLES.find((r) => r.value === a.role)?.label}</Text>
                  )}
                </TableCell>
                <TableCell>{statusOf(a)}</TableCell>
                <TableCell>
                  <Text variant="caption">{when(a.lastLoginAt)}</Text>
                </TableCell>
                <TableCell>
                  {canManage && (
                    <Stack direction="row" gap={0.5} wrap>
                      <Button onClick={() => reset(a)}>Reset password</Button>
                      {a.id !== me.id && (
                        <Button
                          onClick={() => void run(() => usersApi.update(a.id, { enabled: a.locked ? true : !a.enabled }))}
                        >
                          {a.enabled ? (a.locked ? "Unlock" : "Disable") : "Enable"}
                        </Button>
                      )}
                      {a.id !== me.id && (
                        <Button tone="danger" onClick={() => remove(a)}>
                          Delete
                        </Button>
                      )}
                    </Stack>
                  )}
                </TableCell>
              </TableRow>
            ))}
          </TableBody>
        </Table>
      </TableContainer>
      {canManage && (
        <NewUser onCreate={(a) => run(() => usersApi.create(a), `${a.username} added. Give them the temporary password.`)} />
      )}
    </Stack>
  );
}

function NewUser({ onCreate }) {
  const [username, setUsername] = useState("");
  const [displayName, setDisplayName] = useState("");
  const [role, setRole] = useState("OPERATOR");
  const [password, setPassword] = useState("");
  const problem = password ? passwordProblem(password, username) : null;

  const submit = async (e) => {
    e.preventDefault();
    if (!username.trim() || !password || problem) return;
    await onCreate({ username: username.trim(), displayName: displayName.trim(), role, password });
    setUsername("");
    setDisplayName("");
    setPassword("");
  };

  return (
    <Box component="form" onSubmit={submit} autoComplete="off">
      <Text variant="title" sx={{ mb: 1 }}>
        Add a user
      </Text>
      <Stack direction="row" gap={1} wrap align="flex-start">
        <TextField label="Username" value={username} fullWidth={false} onChange={(e) => setUsername(e.target.value)} inputProps={{ maxLength: 40 }} />
        <TextField label="Full name" value={displayName} fullWidth={false} onChange={(e) => setDisplayName(e.target.value)} inputProps={{ maxLength: 60 }} />
        <SelectField label="Role" value={role} options={ROLES} minWidth={120} onChange={(e) => setRole(e.target.value)} />
        <TextField
          label={`Temporary password (${MIN_PASSWORD}+)`}
          type="password"
          fullWidth={false}
          value={password}
          error={Boolean(problem)}
          helperText={problem ?? " "}
          onChange={(e) => setPassword(e.target.value)}
          inputProps={{ autoComplete: "new-password" }}
        />
        <Button type="submit" variant="contained" size="medium" disabled={!username.trim() || !password || Boolean(problem)}>
          Add
        </Button>
      </Stack>
    </Box>
  );
}
