import { LoaderCircle } from 'lucide-react';
import { cn } from './cn';

export function Spinner({ className, label = 'Loading' }: { className?: string; label?: string }) {
  return (
    <span role="status" className={cn('inline-flex items-center', className)}>
      <LoaderCircle aria-hidden className="size-4 animate-spin" />
      <span className="sr-only">{label}</span>
    </span>
  );
}

export function PageLoader({ label = 'Loading…' }: { label?: string }) {
  return (
    <div className="flex min-h-[40vh] items-center justify-center gap-3 text-muted" role="status" aria-live="polite">
      <LoaderCircle aria-hidden className="size-5 animate-spin text-accent" />
      <span>{label}</span>
    </div>
  );
}

export function Skeleton({ className }: { className?: string }) {
  return <div aria-hidden className={cn('animate-pulse rounded-lg bg-surface-3/70', className)} />;
}
