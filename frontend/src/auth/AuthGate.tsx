import { useCallback, useEffect, useState } from 'react'
import App from '../App'
import { AuthShell, LoginPage } from './LoginPage'
import { ChangePasswordForm } from './ChangePassword'
import { AuthUnreachable, fetchSession, onSignedOut, signOut, type AuthUser } from './auth'
import './auth.css'

type Boot =
  | { kind: 'checking' }
  | { kind: 'unreachable'; message: string }
  | { kind: 'signedOut' }
  | { kind: 'signedIn'; user: AuthUser }

/**
 * Shows the app only to a signed-in user. On every load it asks the backend
 * for the session; with none, it shows the sign-in page. A user signed in with
 * a temporary password must change it before reaching the app (the backend
 * refuses every other call until then anyway).
 */
export function AuthGate() {
  const [boot, setBoot] = useState<Boot>({ kind: 'checking' })
  const [signingOut, setSigningOut] = useState(false)

  const check = useCallback(async () => {
    setBoot({ kind: 'checking' })
    try {
      const user = await fetchSession()
      setBoot(user ? { kind: 'signedIn', user } : { kind: 'signedOut' })
    } catch (e) {
      // A login page is the wrong answer when the server never answered.
      setBoot({ kind: 'unreachable', message: e instanceof AuthUnreachable ? e.message : String(e) })
    }
  }, [])

  useEffect(() => {
    void check()
  }, [check])

  useEffect(() => onSignedOut(() => setBoot({ kind: 'signedOut' })), [])

  const setUser = useCallback((user: AuthUser) => setBoot({ kind: 'signedIn', user }), [])

  const doSignOut = useCallback(async () => {
    setSigningOut(true)
    try {
      await signOut()
    } catch {
      /* the next load re-checks the session */
    } finally {
      setSigningOut(false)
      setBoot({ kind: 'signedOut' })
    }
  }, [])

  if (boot.kind === 'signedIn' && boot.user.mustChangePassword) {
    return (
      <AuthShell>
        <p className="auth-lead">
          Welcome, <b>{boot.user.displayName || boot.user.username}</b>. You signed in with a temporary
          password; choose your own to continue.
        </p>
        <ChangePasswordForm user={boot.user} onChanged={setUser} onCancel={doSignOut} cancelLabel="Sign out" />
      </AuthShell>
    )
  }
  if (boot.kind === 'signedIn') {
    return <App user={boot.user} onUser={setUser} onSignOut={doSignOut} signingOut={signingOut} />
  }
  return (
    <LoginPage
      checking={boot.kind === 'checking'}
      unreachable={boot.kind === 'unreachable' ? boot.message : null}
      onSignedIn={setUser}
      onRetry={check}
    />
  )
}
