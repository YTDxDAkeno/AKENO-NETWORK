import type { ReactNode } from 'react';
import { cn } from './cn';

type Tone = 'neutral' | 'accent' | 'success' | 'warning' | 'danger' | 'violet';

const tones: Record<Tone, string> = {
  neutral: 'bg-surface-3 text-muted',
  accent: 'bg-accent-soft text-accent',
  success: 'bg-success/15 text-success',
  warning: 'bg-warning/15 text-warning',
  danger: 'bg-danger-soft text-danger',
  violet: 'bg-violet/15 text-violet',
};

export function Badge({ tone = 'neutral', children, className, icon }: { tone?: Tone; children: ReactNode; className?: string; icon?: ReactNode }) {
  return (
    <span className={cn('inline-flex items-center gap-1 rounded-full px-2 py-0.5 text-xs font-medium', tones[tone], className)}>
      {icon}
      {children}
    </span>
  );
}

export function CountBadge({ count, label }: { count: number; label: string }) {
  if (count <= 0) return null;
  return (
    <span
      className="inline-flex min-w-5 items-center justify-center rounded-full bg-accent px-1.5 text-[11px] font-semibold leading-5 text-on-accent"
      aria-label={label}
    >
      {count > 99 ? '99+' : count}
    </span>
  );
}
