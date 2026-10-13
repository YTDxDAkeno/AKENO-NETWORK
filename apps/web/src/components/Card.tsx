import type { HTMLAttributes, ReactNode } from 'react';
import { cn } from './cn';

export function Card({ className, ...props }: HTMLAttributes<HTMLElement>) {
  return <section className={cn('card', className)} {...props} />;
}

export function CardHeader({
  title,
  description,
  action,
  icon,
  className,
  titleId,
}: {
  title: ReactNode;
  description?: ReactNode;
  action?: ReactNode;
  icon?: ReactNode;
  className?: string;
  titleId?: string;
}) {
  return (
    <header className={cn('flex items-start justify-between gap-3 border-b border-line px-5 py-4', className)}>
      <div className="flex min-w-0 items-start gap-3">
        {icon ? <span className="mt-0.5 text-accent">{icon}</span> : null}
        <div className="min-w-0">
          <h2 id={titleId} className="truncate text-base font-semibold text-ink">
            {title}
          </h2>
          {description ? <p className="mt-0.5 text-sm text-muted">{description}</p> : null}
        </div>
      </div>
      {action ? <div className="shrink-0">{action}</div> : null}
    </header>
  );
}
