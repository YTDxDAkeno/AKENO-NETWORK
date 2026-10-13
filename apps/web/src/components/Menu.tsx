import { useEffect, useId, useRef, useState, type ReactNode } from 'react';
import { cn } from './cn';

export interface MenuItem {
  label: string;
  onSelect: () => void;
  icon?: ReactNode;
  tone?: 'default' | 'danger';
  disabled?: boolean;
}

/** Accessible dropdown menu (button + role="menu") with arrow-key navigation. */
export function Menu({
  label,
  trigger,
  items,
  align = 'end',
  triggerClassName,
}: {
  label: string;
  trigger: ReactNode;
  items: MenuItem[];
  align?: 'start' | 'end';
  triggerClassName?: string;
}) {
  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLDivElement>(null);
  const buttonRef = useRef<HTMLButtonElement>(null);
  const menuId = useId();

  useEffect(() => {
    if (!open) return;
    const onPointer = (event: PointerEvent) => {
      if (!rootRef.current?.contains(event.target as Node)) setOpen(false);
    };
    document.addEventListener('pointerdown', onPointer);
    const first = rootRef.current?.querySelector<HTMLButtonElement>('[role="menuitem"]:not([disabled])');
    first?.focus();
    return () => document.removeEventListener('pointerdown', onPointer);
  }, [open]);

  const onKeyDown = (event: React.KeyboardEvent) => {
    const entries = Array.from(rootRef.current?.querySelectorAll<HTMLButtonElement>('[role="menuitem"]:not([disabled])') ?? []);
    const index = entries.indexOf(document.activeElement as HTMLButtonElement);
    if (event.key === 'Escape') {
      event.preventDefault();
      setOpen(false);
      buttonRef.current?.focus();
    } else if (event.key === 'ArrowDown') {
      event.preventDefault();
      entries[(index + 1) % entries.length]?.focus();
    } else if (event.key === 'ArrowUp') {
      event.preventDefault();
      entries[(index - 1 + entries.length) % entries.length]?.focus();
    } else if (event.key === 'Tab') {
      setOpen(false);
    }
  };

  return (
    <div ref={rootRef} className="relative inline-flex" onKeyDown={open ? onKeyDown : undefined}>
      <button
        ref={buttonRef}
        type="button"
        aria-haspopup="menu"
        aria-expanded={open}
        aria-controls={open ? menuId : undefined}
        aria-label={label}
        onClick={() => setOpen((value) => !value)}
        className={cn('inline-flex items-center rounded-lg', triggerClassName)}
      >
        {trigger}
      </button>
      {open ? (
        <div
          id={menuId}
          role="menu"
          aria-label={label}
          className={cn(
            'card absolute top-full z-40 mt-2 min-w-48 animate-fade-in overflow-hidden p-1',
            align === 'end' ? 'right-0' : 'left-0',
          )}
        >
          {items.map((item) => (
            <button
              key={item.label}
              type="button"
              role="menuitem"
              disabled={item.disabled}
              onClick={() => {
                setOpen(false);
                item.onSelect();
              }}
              className={cn(
                'flex w-full items-center gap-2.5 rounded-lg px-3 py-2 text-left text-sm transition-colors focus:outline-none disabled:opacity-50',
                item.tone === 'danger'
                  ? 'text-danger hover:bg-danger-soft focus:bg-danger-soft'
                  : 'text-ink hover:bg-surface-2 focus:bg-surface-2',
              )}
            >
              {item.icon ? <span className="text-current [&>svg]:size-4">{item.icon}</span> : null}
              {item.label}
            </button>
          ))}
        </div>
      ) : null}
    </div>
  );
}
