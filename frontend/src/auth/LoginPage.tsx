import { useEffect, useRef, useState, type FormEvent } from 'react'
import { useTheme } from '../theme'
import { login, type AuthUser } from './auth'

/** The sign-in page: username and password. */
export function LoginPage({
  checking,
  unreachable,
  onSignedIn,
  onRetry,
}: {
  /** The session check on page load is still running. */
  checking: boolean
  /** Set when the backend never answered the session check. */
  unreachable: string | null
  onSignedIn: (user: AuthUser) => void
  onRetry: () => void
}) {
  useTheme() // apply the remembered light/dark choice here too
  const [username, setUsername] = useState('')
  const [password, setPassword] = useState('')
  const [error, setError] = useState('')
  const [busy, setBusy] = useState(false)
  const passwordRef = useRef<HTMLInputElement>(null)

  useEffect(() => {
    document.title = 'Sign in · OrbLinx MCC'
  }, [])

  const submit = async (e: FormEvent) => {
    e.preventDefault()
    if (busy) return
    setError('')
    setBusy(true)
    try {
      onSignedIn(await login(username.trim(), password))
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err))
      setPassword('')
      passwordRef.current?.focus()
    } finally {
      setBusy(false)
    }
  }

  return (
    <AuthShell>
      {unreachable ? (
        <>
          <div className="auth-error" role="alert">
            {unreachable}
          </div>
          <button type="button" className="auth-btn primary" onClick={onRetry}>
            Try again
          </button>
        </>
      ) : (
        <form className="auth-form" onSubmit={submit} noValidate>
          {error && (
            <div className="auth-error" role="alert">
              {error}
            </div>
          )}
          <label>
            Username
            <input
              name="username"
              autoComplete="username"
              autoCapitalize="none"
              spellCheck={false}
              autoFocus
              required
              maxLength={64}
              value={username}
              onChange={(e) => setUsername(e.target.value)}
              disabled={checking}
            />
          </label>
          <label>
            Password
            <input
              ref={passwordRef}
              name="password"
              type="password"
              autoComplete="current-password"
              required
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              disabled={checking}
            />
          </label>
          <button
            type="submit"
            className="auth-btn primary"
            disabled={checking || busy || !username.trim() || !password}
            aria-busy={checking || busy}
          >
            {(checking || busy) && <span className="auth-spin" aria-hidden />}
            {checking ? 'Checking your session…' : busy ? 'Signing in…' : 'Sign in'}
          </button>
          <p className="auth-hint">Accounts are created by an administrator.</p>
        </form>
      )}
    </AuthShell>
  )
}

/** Logo, product name and a card: shared by the sign-in and forced password-change pages. */
export function AuthShell({ children }: { children: React.ReactNode }) {
  return (
    <main className="auth">
      <section className="auth-card" aria-label="Sign in">
        <div className="auth-brand">
          <img className="logo-ink" src="/xdlinx/logo-ink-800.png" alt="XDLINX Space Labs" />
          <img className="logo-white" src="/xdlinx/logo-white-800.png" alt="XDLINX Space Labs" />
        </div>
        <h1>OrbLinx MCC</h1>
        <p className="auth-sub">Mission control · XMCC telemetry</p>
        {children}
      </section>
    </main>
  )
}
