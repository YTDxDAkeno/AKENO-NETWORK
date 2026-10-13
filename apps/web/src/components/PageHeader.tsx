import type { ReactNode } from 'react';
import { useDocumentTitle } from '../hooks/useDocumentTitle';

export function PageHeader({
  title,
  description,
  actions,
  icon,
}: {
  title: string;
  description?: ReactNode;
  actions?: ReactNode;
  icon?: ReactNode;
}) {
  useDocumentTitle(title);
  return (
    <div className="mb-6 flex flex-col gap-4 sm:flex-row sm:items-end sm:justify-between">
      <div className="flex items-start gap-3">
        {icon ? <div className="mt-1 rounded-xl bg-accent-soft p-2 text-accent [&>svg]:size-5">{icon}</div> : null}
        <div>
          <h1 className="text-2xl font-semibold text-ink sm:text-3xl">{title}</h1>
          {description ? <p className="mt-1 max-w-2xl text-sm text-muted">{description}</p> : null}
        </div>
      </div>
      {actions ? <div className="flex flex-wrap gap-2">{actions}</div> : null}
    </div>
  );
}
