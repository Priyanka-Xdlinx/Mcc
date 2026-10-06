import { useEffect, useRef, useState } from 'react'
import type { AuthUser } from './auth'
import { ChangePasswordForm } from './ChangePassword'
import { UsersAdmin } from './UsersAdmin'

/** The signed-in user in the header: change password, manage users (admins), sign out. */
export function UserMenu({
  user,
  onUser,
  onSignOut,
  signingOut,
}: {
  user: AuthUser
  onUser: (u: AuthUser) => void
  onSignOut: () => void
  signingOut: boolean
}) {
  const [open, setOpen] = useState(false)
  const [dialog, setDialog] = useState<'password' | 'users' | null>(null)
  const ref = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (!open) return
    const key = (e: KeyboardEvent) => e.key === 'Escape' && setOpen(false)
    const down = (e: PointerEvent) => {
      if (!ref.current?.contains(e.target as Node)) setOpen(false)
    }
    window.addEventListener('keydown', key)
    window.addEventListener('pointerdown', down)
    return () => {
      window.removeEventListener('keydown', key)
      window.removeEventListener('pointerdown', down)
    }
  }, [open])

  const pick = (d: 'password' | 'users') => {
    setOpen(false)
    setDialog(d)
  }

  return (
    <div className="usermenu" ref={ref}>
      <button
        type="button"
        className="usermenu-btn"
        aria-haspopup="menu"
        aria-expanded={open}
        onClick={() => setOpen((v) => !v)}
        title={`Signed in as ${user.username}`}
      >
        <span className="usermenu-avatar" aria-hidden>
          {(user.displayName || user.username).slice(0, 1).toUpperCase()}
        </span>
        <span className="usermenu-name">{user.displayName || user.username}</span>
      </button>
      {open && (
        <div className="usermenu-list" role="menu">
          <div className="usermenu-who">
            <b>{user.username}</b>
            <span>{user.role === 'ADMIN' ? 'Administrator' : 'Operator'}</span>
          </div>
          <button type="button" role="menuitem" onClick={() => pick('password')}>
            Change password
          </button>
          {user.role === 'ADMIN' && (
            <button type="button" role="menuitem" onClick={() => pick('users')}>
              Manage users
            </button>
          )}
          <button type="button" role="menuitem" onClick={onSignOut} disabled={signingOut}>
            {signingOut ? 'Signing out…' : 'Sign out'}
          </button>
        </div>
      )}
      {dialog && (
        <Dialog title={dialog === 'password' ? 'Change password' : 'Users'} wide={dialog === 'users'}
                onClose={() => setDialog(null)}>
          {dialog === 'password' ? (
            <ChangePasswordForm
              user={user}
              onCancel={() => setDialog(null)}
              onChanged={(u) => {
                onUser(u)
                setDialog(null)
              }}
            />
          ) : (
            <UsersAdmin me={user} />
          )}
        </Dialog>
      )}
    </div>
  )
}

export function Dialog({
  title,
  wide = false,
  onClose,
  children,
}: {
  title: string
  wide?: boolean
  onClose: () => void
  children: React.ReactNode
}) {
  useEffect(() => {
    const key = (e: KeyboardEvent) => e.key === 'Escape' && onClose()
    window.addEventListener('keydown', key)
    return () => window.removeEventListener('keydown', key)
  }, [onClose])
  return (
    <div className="auth-backdrop" onMouseDown={(e) => e.target === e.currentTarget && onClose()}>
      <div className={`auth-dialog${wide ? ' wide' : ''}`} role="dialog" aria-modal aria-label={title}>
        <div className="auth-dialog-bar">
          <b>{title}</b>
          <button type="button" className="auth-x" onClick={onClose} aria-label="Close">
            ✕
          </button>
        </div>
        <div className="auth-dialog-body">{children}</div>
      </div>
    </div>
  )
}
