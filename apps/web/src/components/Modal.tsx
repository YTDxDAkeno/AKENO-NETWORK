import { X } from 'lucide-react';
import { useEffect, useId, useRef, type ReactNode } from 'react';
import { cn } from './cn';

/**
 * Accessible modal dialog built on the native <dialog> element: focus is trapped, Escape closes,
 * and the page behind becomes inert.
 */
export function Modal({
  open,
  onClose,
  title,
  description,
  children,
  footer,
  size = 'md',
}: {
  open: boolean;
  onClose: () => void;
  title: string;
  description?: ReactNode;
  children?: ReactNode;
  footer?: ReactNode;
  size?: 'sm' | 'md' | 'lg';
}) {
  const ref = useRef<HTMLDialogElement>(null);
  const titleId = useId();
  const descriptionId = useId();

  useEffect(() => {
    const dialog = ref.current;
    if (!dialog) return;
    if (open && !dialog.open) dialog.showModal();
    if (!open && dialog.open) dialog.close();
  }, [open]);

  return (
    <dialog
      ref={ref}
      aria-labelledby={titleId}
      aria-describedby={description ? descriptionId : undefined}
      onClose={onClose}
      onCancel={(event) => {
        event.preventDefault();
        onClose();
      }}
      onClick={(event) => {
        if (event.target === ref.current) onClose();
      }}
      className={cn(
        'card m-auto w-[calc(100%-2rem)] max-h-[calc(100dvh-2rem)] overflow-hidden p-0 text-ink backdrop:bg-transparent open:animate-fade-in',
        size === 'sm' && 'max-w-sm',
        size === 'md' && 'max-w-lg',
        size === 'lg' && 'max-w-2xl',
      )}
    >
      {open ? (
        <div className="flex max-h-[calc(100dvh-2rem)] flex-col">
          <header className="flex items-start justify-between gap-4 border-b border-line px-5 py-4">
            <div>
              <h2 id={titleId} className="text-lg font-semibold">
                {title}
              </h2>
              {description ? (
                <p id={descriptionId} className="mt-1 text-sm text-muted">
                  {description}
                </p>
              ) : null}
            </div>
            <button
              type="button"
              onClick={onClose}
              className="rounded-lg p-1.5 text-muted hover:bg-surface-2 hover:text-ink"
              aria-label="Close dialog"
            >
              <X aria-hidden className="size-5" />
            </button>
          </header>
          <div className="scrollbar-thin overflow-y-auto px-5 py-4">{children}</div>
          {footer ? <footer className="flex flex-wrap justify-end gap-2 border-t border-line px-5 py-3.5">{footer}</footer> : null}
        </div>
      ) : null}
    </dialog>
  );
}
