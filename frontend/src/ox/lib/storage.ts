// Browser storage can throw (private mode, blocked site data, quota) or be absent (tests, SSR).
// Every access goes through these helpers so a failure degrades to "not persisted", never a crash.

type Kind = 'local' | 'session';

function area(kind: Kind): Storage | null {
  try {
    return kind === 'local' ? window.localStorage : window.sessionStorage;
  } catch {
    return null;
  }
}

export function readString(kind: Kind, key: string): string | null {
  try {
    return area(kind)?.getItem(key) ?? null;
  } catch {
    return null;
  }
}

export function writeString(kind: Kind, key: string, value: string): boolean {
  try {
    const s = area(kind);
    if (!s) return false;
    s.setItem(key, value);
    return true;
  } catch {
    return false;
  }
}

export function remove(kind: Kind, key: string): void {
  try {
    area(kind)?.removeItem(key);
  } catch {
    /* ignore */
  }
}

export function readJson(kind: Kind, key: string): unknown {
  const raw = readString(kind, key);
  if (raw === null) return undefined;
  try {
    return JSON.parse(raw) as unknown;
  } catch {
    return undefined;
  }
}

export function writeJson(kind: Kind, key: string, value: unknown): boolean {
  return writeString(kind, key, JSON.stringify(value));
}
