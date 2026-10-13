import { useId } from 'react';
import { cn } from './cn';

/** Original AKENO NETWORK mark: an angular "A" with a core dot. */
export function LogoMark({ className }: { className?: string }) {
  const gradientId = useId();
  return (
    <svg viewBox="0 0 64 64" className={cn('size-9', className)} aria-hidden="true">
      <defs>
        <linearGradient id={gradientId} x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#4f7cff" />
          <stop offset="1" stopColor="#8b5cf6" />
        </linearGradient>
      </defs>
      <rect width="64" height="64" rx="16" fill="var(--akeno-surface-2)" />
      <path d="M32 10 L52 52 H42 L32 30 L22 52 H12 Z" fill={`url(#${gradientId})`} />
      <circle cx="32" cy="44" r="4" fill="var(--akeno-ink)" />
    </svg>
  );
}

export function Logo({ className, compact = false }: { className?: string; compact?: boolean }) {
  return (
    <span className={cn('inline-flex items-center gap-2.5', className)}>
      <LogoMark />
      {compact ? null : (
        <span className="font-display text-[15px] font-bold leading-none tracking-[0.18em] text-ink">
          AKENO
          <span className="block pt-0.5 text-[10px] font-medium tracking-[0.42em] text-muted">NETWORK</span>
        </span>
      )}
    </span>
  );
}
