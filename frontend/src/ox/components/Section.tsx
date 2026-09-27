import { useState, type ReactNode } from 'react';
import { readJson, writeJson } from '../lib/storage';

const KEY = 'mcc.open';

function initialOpen(id: string | undefined, dflt: boolean): boolean {
  if (!id) return dflt;
  const o = readJson('local', KEY);
  if (o && typeof o === 'object' && typeof (o as Record<string, unknown>)[id] === 'boolean')
    return (o as Record<string, boolean>)[id]!;
  return dflt;
}

/** Collapsible section as in the prototype. With `persistId`, the open state is remembered. */
export function Section(props: {
  title: string;
  count?: ReactNode;
  end?: ReactNode;
  persistId?: string;
  defaultOpen?: boolean;
  children: ReactNode;
  id?: string;
}) {
  const [open, setOpen] = useState(() => initialOpen(props.persistId, props.defaultOpen ?? true));
  return (
    <details
      className="sec"
      id={props.id}
      open={open}
      onToggle={(e) => {
        const o = (e.currentTarget as HTMLDetailsElement).open;
        setOpen(o);
        if (props.persistId) {
          const cur = readJson('local', KEY);
          writeJson('local', KEY, { ...(cur && typeof cur === 'object' ? cur : {}), [props.persistId]: o });
        }
      }}
    >
      <summary>
        <span className="chev" aria-hidden="true" />
        <h2>{props.title}</h2>
        {props.count !== undefined && <span className="count">{props.count}</span>}
        {props.end && (
          // Controls inside <summary> must not toggle the section.
          <span className="end" onClick={(e) => e.stopPropagation()} onKeyDown={(e) => e.stopPropagation()}>
            {props.end}
          </span>
        )}
      </summary>
      {open && props.children}
    </details>
  );
}
