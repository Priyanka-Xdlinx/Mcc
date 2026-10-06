import { useState, type FormEvent } from 'react'
import { changePassword, passwordProblem, MIN_PASSWORD, type AuthUser } from './auth'

/**
 * Current + new + confirm. Used both as the forced step after signing in with a
 * temporary password and from the account menu.
 */
export function ChangePasswordForm({
  user,
  onChanged,
  onCancel,
  cancelLabel = 'Cancel',
}: {
  user: AuthUser
  onChanged: (user: AuthUser) => void
  onCancel: () => void
  cancelLabel?: string
}) {
  const [current, setCurrent] = useState('')
  const [next, setNext] = useState('')
  const [confirm, setConfirm] = useState('')
  const [error, setError] = useState('')
  const [busy, setBusy] = useState(false)

  const problem = next ? passwordProblem(next, user.username) : null
  const mismatch = confirm !== '' && confirm !== next
  const ready = current && next && !problem && confirm === next

  const submit = async (e: FormEvent) => {
    e.preventDefault()
    if (!ready || busy) return
    setError('')
    setBusy(true)
    try {
      onChanged(await changePassword(current, next))
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err))
    } finally {
      setBusy(false)
    }
  }

  return (
    <form className="auth-form" onSubmit={submit} noValidate>
      {error && (
        <div className="auth-error" role="alert">
          {error}
        </div>
      )}
      {/* lets password managers attach the new password to the right account */}
      <input type="text" name="username" autoComplete="username" value={user.username} readOnly hidden />
      <label>
        Current password
        <input type="password" autoComplete="current-password" autoFocus value={current}
               onChange={(e) => setCurrent(e.target.value)} />
      </label>
      <div className="auth-field">
        <label>
          New password
          <input type="password" autoComplete="new-password" value={next} aria-invalid={!!problem}
                 aria-describedby="pw-new-hint" onChange={(e) => setNext(e.target.value)} />
        </label>
        <span id="pw-new-hint" className={`auth-field-hint${problem ? ' bad' : ''}`}>
          {problem ?? `At least ${MIN_PASSWORD} characters. A few unrelated words make a strong one.`}
        </span>
      </div>
      <div className="auth-field">
        <label>
          Confirm new password
          <input type="password" autoComplete="new-password" value={confirm} aria-invalid={mismatch}
                 aria-describedby="pw-confirm-hint" onChange={(e) => setConfirm(e.target.value)} />
        </label>
        <span id="pw-confirm-hint" className="auth-field-hint bad">{mismatch ? 'Does not match' : ''}</span>
      </div>
      <div className="auth-actions">
        <button type="button" className="auth-btn" onClick={onCancel} disabled={busy}>
          {cancelLabel}
        </button>
        <button type="submit" className="auth-btn primary" disabled={!ready || busy} aria-busy={busy}>
          {busy && <span className="auth-spin" aria-hidden />}
          Change password
        </button>
      </div>
      <p className="auth-hint">Changing it signs you out on every other device.</p>
    </form>
  )
}
