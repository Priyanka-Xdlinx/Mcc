// REST client for display pages, users and workspaces (see the display contract).
// Every call resolves or throws an Error whose message is the backend's {error}
// text when it sent one, so callers can show it to the operator as-is.

export const SLOTS = 20

export interface User {
  id: number
  name: string
  pages: number
}

export interface Page {
  id: number
  ownerId: number
  ownerName: string
  name: string
  params: (number | null)[]
  createdAt: string
  updatedAt: string
}

export interface Draft {
  name: string
  params: (number | null)[]
}

/** One window as stored in the workspace layout JSON (frontend-owned). */
export interface LayoutWindow {
  key: string
  pageId: number | null
  draft: Draft | null
  x: number
  y: number
  w: number
  h: number
  z: number
  min: boolean
  /** Permanent page number of this window; never renumbered when others close. */
  pageNo: number
}

export interface Layout {
  v: 1
  windows: LayoutWindow[]
}

export class ApiError extends Error {
  constructor(message: string, readonly status: number) {
    super(message)
  }
}

async function call<T>(method: string, url: string, user: number | null, body?: unknown): Promise<T> {
  const headers: Record<string, string> = {}
  if (body !== undefined) headers['Content-Type'] = 'application/json'
  if (user != null) headers['X-MCC-User'] = String(user)
  let r: Response
  try {
    r = await fetch(url, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) })
  } catch {
    throw new ApiError('The MCC backend is not reachable.', 0)
  }
  if (!r.ok) {
    let msg = `${method} ${url} failed (HTTP ${r.status})`
    try {
      const j = await r.json()
      if (j && typeof j.error === 'string' && j.error) msg = j.error
    } catch {
      /* not JSON: keep the HTTP text */
    }
    throw new ApiError(msg, r.status)
  }
  if (r.status === 204) return undefined as T
  const text = await r.text()
  return (text ? JSON.parse(text) : undefined) as T
}

export const api = {
  users: () => call<User[]>('GET', '/api/users', null),
  createUser: (name: string) => call<User>('POST', '/api/users', null, { name }),
  /** The operator this browser picked, remembered by the backend in a cookie. */
  session: () => call<{ user: User | null }>('GET', '/api/session', null),
  setSession: (userId: number | null) => call<{ user: User | null }>('PUT', '/api/session', null, { userId }),
  pages: () => call<Page[]>('GET', '/api/pages', null),
  page: (id: number) => call<Page>('GET', `/api/pages/${id}`, null),
  createPage: (user: number, d: Draft) => call<Page>('POST', '/api/pages', user, d),
  updatePage: (user: number, id: number, d: Draft) => call<Page>('PUT', `/api/pages/${id}`, user, d),
  deletePage: (user: number, id: number) => call<void>('DELETE', `/api/pages/${id}`, user),
  workspace: (user: number) =>
    call<{ layout: unknown; updatedAt: string | null }>('GET', `/api/users/${user}/workspace`, null),
  saveWorkspace: (user: number, layout: Layout) =>
    call<void>('PUT', `/api/users/${user}/workspace`, user, { layout }),
}

export function emptyParams(): (number | null)[] {
  return Array.from({ length: SLOTS }, () => null)
}

/** Exactly 20 entries, each an integer id or null, whatever came in. */
export function normParams(p: unknown): (number | null)[] {
  const out = emptyParams()
  if (Array.isArray(p)) {
    for (let i = 0; i < SLOTS; i++) {
      const v = p[i]
      out[i] = typeof v === 'number' && Number.isInteger(v) ? v : null
    }
  }
  return out
}

const num = (v: unknown, d: number) => (typeof v === 'number' && Number.isFinite(v) ? v : d)

/** Parses a stored layout defensively; anything unusable is dropped. */
export function parseLayout(raw: unknown): LayoutWindow[] | null {
  let v = raw
  if (typeof v === 'string') {
    try {
      v = JSON.parse(v)
    } catch {
      return null
    }
  }
  if (!v || typeof v !== 'object') return null
  const wins = (v as { windows?: unknown }).windows
  if (!Array.isArray(wins)) return null
  const out: LayoutWindow[] = []
  wins.forEach((w, i) => {
    if (!w || typeof w !== 'object') return
    const o = w as Record<string, unknown>
    const d = o.draft as Record<string, unknown> | null | undefined
    const pageId = typeof o.pageId === 'number' && Number.isInteger(o.pageId) ? o.pageId : null
    const draft: Draft | null =
      d && typeof d === 'object'
        ? { name: typeof d.name === 'string' ? d.name : 'Untitled page', params: normParams(d.params) }
        : null
    if (pageId == null && !draft) return
    out.push({
      key: typeof o.key === 'string' && o.key ? o.key : `r${i}`,
      pageId,
      draft,
      x: num(o.x, 24 + i * 28),
      y: num(o.y, 16 + i * 28),
      w: num(o.w, 820),
      h: num(o.h, 560),
      z: num(o.z, i + 1),
      min: o.min === true,
      pageNo: typeof o.pageNo === 'number' && Number.isInteger(o.pageNo) && o.pageNo > 0 ? o.pageNo : 0,
    })
  })
  return uniquePageNos(out)
}

/** Smallest positive page number not used by any window. */
export function nextPageNo(ws: { pageNo: number }[]): number {
  const used = new Set(ws.map((w) => w.pageNo))
  let n = 1
  while (used.has(n)) n++
  return n
}

/** Keeps each window's number, and gives a new one to any missing (0) or repeated number. */
export function uniquePageNos<T extends { pageNo: number }>(ws: T[]): T[] {
  const seen = new Set<number>()
  const bad: number[] = []
  ws.forEach((w, i) => {
    if (w.pageNo > 0 && !seen.has(w.pageNo)) seen.add(w.pageNo)
    else bad.push(i)
  })
  if (!bad.length) return ws
  const out = ws.slice()
  for (const i of bad) {
    const n = nextPageNo(Array.from(seen, (pageNo) => ({ pageNo })))
    seen.add(n)
    out[i] = { ...out[i], pageNo: n }
  }
  return out
}
