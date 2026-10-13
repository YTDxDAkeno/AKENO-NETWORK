import { CircleAlert, RefreshCw } from 'lucide-react';
import type { ReactNode } from 'react';
import { errorMessage } from '../lib/api';
import { Button } from './Button';
import { cn } from './cn';

export function EmptyState({
  icon,
  title,
  description,
  action,
  className,
}: {
  icon?: ReactNode;
  title: string;
  description?: ReactNode;
  action?: ReactNode;
  className?: string;
}) {
  return (
    <div className={cn('flex flex-col items-center justify-center gap-2 px-6 py-10 text-center', className)}>
      {icon ? <div className="mb-1 rounded-2xl bg-surface-2 p-3 text-subtle">{icon}</div> : null}
      <p className="font-medium text-ink">{title}</p>
      {description ? <p className="max-w-sm text-sm text-muted">{description}</p> : null}
      {action ? <div className="mt-2">{action}</div> : null}
    </div>
  );
}

export function ErrorState({ error, onRetry, className }: { error: unknown; onRetry?: () => void; className?: string }) {
  return (
    <div role="alert" className={cn('flex flex-col items-center gap-3 px-6 py-10 text-center', className)}>
      <CircleAlert aria-hidden className="size-7 text-danger" />
      <p className="max-w-md text-sm text-muted">{errorMessage(error)}</p>
      {onRetry ? (
        <Button size="sm" onClick={onRetry} icon={<RefreshCw aria-hidden className="size-4" />}>
          Try again
        </Button>
      ) : null}
    </div>
  );
}

export function InlineError({ message }: { message: string | null | undefined }) {
  if (!message) return null;
  return (
    <div role="alert" className="flex items-start gap-2 rounded-xl border border-danger/30 bg-danger-soft px-3 py-2.5 text-sm text-danger">
      <CircleAlert aria-hidden className="mt-0.5 size-4 shrink-0" />
      <span>{message}</span>
    </div>
  );
}

export function Notice({ icon, children, tone = 'info' }: { icon?: ReactNode; children: ReactNode; tone?: 'info' | 'warning' }) {
  return (
    <div
      className={cn(
        'flex items-start gap-2.5 rounded-xl border px-3.5 py-3 text-sm',
        tone === 'warning' ? 'border-warning/30 bg-warning/10 text-ink' : 'border-accent/25 bg-accent-soft text-ink',
      )}
    >
      {icon ? <span className={cn('mt-0.5 shrink-0', tone === 'warning' ? 'text-warning' : 'text-accent')}>{icon}</span> : null}
      <div className="min-w-0">{children}</div>
    </div>
  );
}
