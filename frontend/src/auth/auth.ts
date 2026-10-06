// Username/password sign-in. The tokens live in HttpOnly cookies, so this code
// never sees them; it only drives the endpoints:
//
//   POST /auth/login    {username, password} -> {user}, sets the session cookies
//   POST /auth/refresh  -> {user}, renews the 15-minute access token
//   POST /auth/logout   ends the session
//   GET  /api/me, POST /api/me/password, /api/admin/users (admins)
//
// Every write carries X-XSRF-TOKEN, copied from the readable XSRF-TOKEN cookie
// (double-submit CSRF); installAuthFetch() adds it to every same-origin fetch.

export interface AuthUser {
  id: number
  username: string
  displayName: string
  role: 'ADMIN' | 'OPERATOR'
  /** A temporary password (first admin, new or reset account) must be changed before anything else. */
  mustChangePassword: boolean
}

export interface Account {
  id: number
  username: string
  displayName: string
  role: 'ADMIN' | 'OPERATOR'
  enabled: boolean
  locked: boolean
  mustChangePassword: boolean
  createdAt: string
  lastLoginAt: string | null
}

/** Thrown when the backend could not be reached at all (as opposed to "not signed in"). */
export class AuthUnreachable extends Error {}

export const MIN_PASSWORD = 12

const NOT_RUNNING = 'The MCC backend is not reachable. Start it with "python mcc.py start", then try again.'

const SIGNED_OUT_EVENT = 'mcc:signed-out'
const nativeFetch = window.fetch.bind(window)
const SAFE = new Set(['GET', 'HEAD', 'OPTIONS'])

function xsrfToken(): string | null {
  const m = document.cookie.match(/(?:^|;\s*)XSRF-TOKEN=([^;]*)/)
  return m ? decodeURIComponent(m[1]) : null
}

/** The XSRF-TOKEN cookie, fetched first if this page has none yet (e.g. index.html served by Vite). */
async function csrfToken(): Promise<string | null> {
  const have = xsrfToken()
  if (have) return have
  try {
    await nativeFetch('/auth/csrf', { credentials: 'same-origin' })
  } catch {
    /* the request it guards will fail and say so */
  }
  return xsrfToken()
}

/** JSON call that throws an Error carrying the backend's {error} text. */
async function call<T>(method: string, url: string, body?: unknown): Promise<T> {
  let r: Response
  try {
    r = await window.fetch(url, {
      method,
      headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
      body: body === undefined ? undefined : JSON.stringify(body),
    })
  } catch {
    throw new AuthUnreachable('The MCC backend is not reachable.')
  }
  if (!r.ok) {
    let msg = `Request failed (HTTP ${r.status})`
    try {
      const j = await r.json()
      if (j && typeof j.error === 'string' && j.error) msg = j.error
    } catch {
      /* keep the HTTP text */
    }
    throw new Error(msg)
  }
  if (r.status === 204) return undefined as T
  return (await r.json()) as T
}

export async function login(username: string, password: string): Promise<AuthUser> {
  const { user } = await call<{ user: AuthUser }>('POST', '/auth/login', { username, password })
  return user
}

/**
 * Renews the access token from the refresh cookie and returns the user.
 * @returns null when nobody is signed in (an expected 401 on a first visit)
 * @throws AuthUnreachable when the backend did not answer
 */
export async function fetchSession(): Promise<AuthUser | null> {
  let r: Response
  try {
    r = await nativeFetch('/auth/refresh', { method: 'POST', credentials: 'same-origin' })
  } catch {
    throw new AuthUnreachable(NOT_RUNNING)
  }
  if (r.status === 401 || r.status === 403) return null
  // A backend from before sign-in existed has no /auth endpoints.
  if (r.status === 404) {
    throw new AuthUnreachable(
      'The MCC backend on port 8080 is an older build without sign-in. Stop it, rebuild the jar ' +
        '(mvn -DskipTests package in backend/) and start it again.',
    )
  }
  // The Vite dev server answers 500/502/504 when it cannot reach the backend.
  if (r.status >= 500) throw new AuthUnreachable(NOT_RUNNING)
  if (!r.ok) throw new AuthUnreachable(`The MCC backend answered HTTP ${r.status}.`)
  return ((await r.json()) as { user: AuthUser }).user
}

export async function signOut(): Promise<void> {
  await call<void>('POST', '/auth/logout')
}

export async function changePassword(currentPassword: string, newPassword: string): Promise<AuthUser> {
  const { user } = await call<{ user: AuthUser }>('POST', '/api/me/password', { currentPassword, newPassword })
  return user
}

export const accounts = {
  list: () => call<Account[]>('GET', '/api/admin/users'),
  create: (a: { username: string; displayName: string; role: string; password: string }) =>
    call<Account>('POST', '/api/admin/users', a),
  update: (id: number, change: { displayName?: string; role?: string; enabled?: boolean }) =>
    call<Account>('PATCH', `/api/admin/users/${id}`, change),
  resetPassword: (id: number, password: string) => call<void>('PUT', `/api/admin/users/${id}/password`, { password }),
  remove: (id: number) => call<void>('DELETE', `/api/admin/users/${id}`),
}

/** Called when a session ends behind the page's back (expired, revoked, account disabled). */
export function onSignedOut(handler: () => void): () => void {
  window.addEventListener(SIGNED_OUT_EVENT, handler)
  return () => window.removeEventListener(SIGNED_OUT_EVENT, handler)
}

// One shared refresh, so a page that fires five requests at once when the
// token expires does one refresh and not five.
let inFlightRefresh: Promise<AuthUser | null> | null = null

function refreshOnce(): Promise<AuthUser | null> {
  inFlightRefresh ??= fetchSession()
    .catch(() => null)
    .finally(() => {
      inFlightRefresh = null
    })
  return inFlightRefresh
}

/**
 * Wraps window.fetch for every same-origin call, so the existing
 * fetch('/api/...') calls need no changes:
 *  - writes carry X-XSRF-TOKEN
 *  - a 401 from /api/ refreshes the access token once and retries; access
 *    tokens live 15 minutes, so a 401 usually means only that one expired
 *    while the tab was open. If the session itself has ended, the page is
 *    told to show the sign-in screen.
 */
export function installAuthFetch(): void {
  window.fetch = async (input: RequestInfo | URL, init?: RequestInit): Promise<Response> => {
    const raw = typeof input === 'string' ? input : input instanceof URL ? input.href : input.url
    const url = new URL(raw, window.location.href)
    if (url.origin !== window.location.origin) return nativeFetch(input, init)

    const method = (init?.method ?? (input instanceof Request ? input.method : 'GET')).toUpperCase()
    const withXsrf = async (): Promise<RequestInit | undefined> => {
      if (SAFE.has(method)) return init
      const token = await csrfToken()
      if (!token) return init
      const headers = new Headers(init?.headers ?? (input instanceof Request ? input.headers : undefined))
      headers.set('X-XSRF-TOKEN', token)
      return { ...init, headers }
    }

    const r = await nativeFetch(input, await withXsrf())
    if (r.status !== 401 || !url.pathname.startsWith('/api/')) return r
    if (!(await refreshOnce())) {
      window.dispatchEvent(new Event(SIGNED_OUT_EVENT))
      return r
    }
    return nativeFetch(input, await withXsrf())
  }
}

/** Mirrors the backend's PasswordPolicy for instant feedback; the server has the final say. */
export function passwordProblem(password: string, username: string): string | null {
  if ([...password].length < MIN_PASSWORD) return `At least ${MIN_PASSWORD} characters`
  if (new TextEncoder().encode(password).length > 72) return 'At most 72 bytes'
  if (username && password.toLowerCase().includes(username.toLowerCase())) return 'Must not contain the username'
  return null
}
