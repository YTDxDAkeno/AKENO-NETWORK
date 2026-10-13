import { useId, type ReactNode } from 'react';
import { cn } from './cn';

export interface TabItem<T extends string> {
  value: T;
  label: string;
  badge?: ReactNode;
}

/** Accessible tab list (WAI-ARIA tabs pattern, arrow keys move between tabs). */
export function Tabs<T extends string>({
  items,
  value,
  onChange,
  label,
  className,
}: {
  items: Array<TabItem<T>>;
  value: T;
  onChange: (value: T) => void;
  label: string;
  className?: string;
}) {
  const id = useId();
  const onKeyDown = (event: React.KeyboardEvent<HTMLDivElement>) => {
    if (event.key !== 'ArrowRight' && event.key !== 'ArrowLeft') return;
    event.preventDefault();
    const index = items.findIndex((item) => item.value === value);
    const next = items[(index + (event.key === 'ArrowRight' ? 1 : -1) + items.length) % items.length];
    if (next) {
      onChange(next.value);
      document.getElementById(`${id}-${next.value}`)?.focus();
    }
  };
  return (
    <div
      role="tablist"
      aria-label={label}
      onKeyDown={onKeyDown}
      className={cn('scrollbar-thin flex gap-1 overflow-x-auto rounded-xl border border-line bg-surface p-1', className)}
    >
      {items.map((item) => {
        const selected = item.value === value;
        return (
          <button
            key={item.value}
            id={`${id}-${item.value}`}
            type="button"
            role="tab"
            aria-selected={selected}
            tabIndex={selected ? 0 : -1}
            onClick={() => onChange(item.value)}
            className={cn(
              'inline-flex shrink-0 items-center gap-2 rounded-lg px-3.5 py-2 text-sm font-medium transition-colors',
              selected ? 'bg-surface-3 text-ink shadow-sm' : 'text-muted hover:text-ink',
            )}
          >
            {item.label}
            {item.badge}
          </button>
        );
      })}
    </div>
  );
}
