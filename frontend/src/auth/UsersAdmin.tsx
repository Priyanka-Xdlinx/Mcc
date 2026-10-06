import { useCallback, useEffect, useState, type FormEvent } from 'react'
import { accounts, passwordProblem, MIN_PASSWORD, type Account, type AuthUser } from './auth'

const fmt = (iso: string | null) => (iso ? new Date(iso).toLocaleString() : 'never')

/**
 * Accounts, for admins: add, change role, enable/disable, set a temporary
 * password, delete. New and reset passwords are temporary: the user must
 * choose their own at next sign-in.
 */
export function UsersAdmin({ me }: { me: AuthUser }) {
  const [list, setList] = useState<Account[] | null>(null)
  const [error, setError] = useState('')
  const [notice, setNotice] = useState('')

  const load = useCallback(async () => {
    try {
      setList(await accounts.list())
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    }
  }, [])

  useEffect(() => {
    void load()
  }, [load])

  const run = async (what: () => Promise<unknown>, done?: string) => {
    setError('')
    setNotice('')
    try {
      await what()
      if (done) setNotice(done)
      await load()
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    }
  }

  const reset = (a: Account) => {
    const pw = window.prompt(
      `Temporary password for ${a.username} (at least ${MIN_PASSWORD} characters).\n` +
        'They will be signed out everywhere and must choose a new one at next sign-in.',
    )
    if (pw == null) return
    const problem = passwordProblem(pw, a.username)
    if (problem) return setError(problem)
    void run(() => accounts.resetPassword(a.id, pw), `Password reset for ${a.username}.`)
  }

  const remove = (a: Account) => {
    if (!window.confirm(`Delete ${a.username}? This cannot be undone.`)) return
    void run(() => accounts.remove(a.id), `${a.username} deleted.`)
  }

  return (
    <div className="users">
      {error && <div className="auth-error" role="alert">{error}</div>}
      {notice && <div className="auth-ok" role="status">{notice}</div>}
      <div className="users-scroll">
        <table className="users-table">
          <thead>
            <tr>
              <th>User</th>
              <th>Role</th>
              <th>Status</th>
              <th>Last sign-in</th>
              <th />
            </tr>
          </thead>
          <tbody>
            {list === null && (
              <tr>
                <td colSpan={5} className="users-empty">Loading…</td>
              </tr>
            )}
            {list?.map((a) => (
              <tr key={a.id}>
                <td>
                  <b>{a.username}</b>
                  {a.id === me.id && <span className="users-tag">you</span>}
                  <div className="users-dim">{a.displayName}</div>
                </td>
                <td>
                  <select
                    value={a.role}
                    aria-label={`Role of ${a.username}`}
                    onChange={(e) => void run(() => accounts.update(a.id, { role: e.target.value }))}
                  >
                    <option value="OPERATOR">Operator</option>
                    <option value="ADMIN">Admin</option>
                  </select>
                </td>
                <td>
                  {!a.enabled ? (
                    <span className="users-tag bad">disabled</span>
                  ) : a.locked ? (
                    <span className="users-tag warn">locked</span>
                  ) : a.mustChangePassword ? (
                    <span className="users-tag">temporary password</span>
                  ) : (
                    <span className="users-tag ok">active</span>
                  )}
                </td>
                <td className="users-dim">{fmt(a.lastLoginAt)}</td>
                <td className="users-actions">
                  <button type="button" onClick={() => reset(a)}>Reset password</button>
                  {a.id !== me.id && (
                    <button type="button"
                            onClick={() => void run(() => accounts.update(a.id, { enabled: a.locked ? true : !a.enabled }))}>
                      {a.enabled ? (a.locked ? 'Unlock' : 'Disable') : 'Enable'}
                    </button>
                  )}
                  {a.id !== me.id && (
                    <button type="button" className="danger" onClick={() => remove(a)}>Delete</button>
                  )}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      <NewUser onCreate={(a) => run(() => accounts.create(a), `${a.username} added. Give them the temporary password.`)} />
    </div>
  )
}

function NewUser({
  onCreate,
}: {
  onCreate: (a: { username: string; displayName: string; role: string; password: string }) => Promise<void>
}) {
  const [username, setUsername] = useState('')
  const [displayName, setDisplayName] = useState('')
  const [role, setRole] = useState('OPERATOR')
  const [password, setPassword] = useState('')
  const problem = password ? passwordProblem(password, username) : null

  const submit = async (e: FormEvent) => {
    e.preventDefault()
    if (!username.trim() || !password || problem) return
    await onCreate({ username: username.trim(), displayName: displayName.trim(), role, password })
    setUsername('')
    setDisplayName('')
    setPassword('')
  }

  return (
    <form className="users-new" onSubmit={submit} autoComplete="off">
      <b>Add a user</b>
      <input placeholder="Username" value={username} maxLength={40} autoComplete="off"
             onChange={(e) => setUsername(e.target.value)} aria-label="Username" />
      <input placeholder="Full name" value={displayName} maxLength={60}
             onChange={(e) => setDisplayName(e.target.value)} aria-label="Full name" />
      <select value={role} onChange={(e) => setRole(e.target.value)} aria-label="Role">
        <option value="OPERATOR">Operator</option>
        <option value="ADMIN">Admin</option>
      </select>
      <input type="password" placeholder={`Temporary password (${MIN_PASSWORD}+)`} value={password}
             autoComplete="new-password" aria-label="Temporary password" aria-invalid={!!problem}
             onChange={(e) => setPassword(e.target.value)} title={problem ?? undefined} />
      <button type="submit" className="auth-btn primary" disabled={!username.trim() || !password || !!problem}>
        Add
      </button>
      {problem && <span className="auth-field-hint bad">{problem}</span>}
    </form>
  )
}
