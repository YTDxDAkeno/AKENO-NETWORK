import { CircleAlert, CircleCheck, Info, X } from 'lucide-react';
import { createContext, useCallback, useContext, useMemo, useRef, useState, type ReactNode } from 'react';
import { cn } from './cn';

type ToastTone = 'success' | 'error' | 'info';

interface ToastItem {
  id: number;
  tone: ToastTone;
  title: string;
  description?: string;
}

interface ToastApi {
  success: (title: string, description?: string) => void;
  error: (title: string, description?: string) => void;
  info: (title: string, description?: string) => void;
}

const ToastContext = createContext<ToastApi | null>(null);

const icons = { success: CircleCheck, error: CircleAlert, info: Info };

export function ToastProvider({ children }: { children: ReactNode }) {
  const [items, setItems] = useState<ToastItem[]>([]);
  const nextId = useRef(1);

  const dismiss = useCallback((id: number) => setItems((current) => current.filter((item) => item.id !== id)), []);

  const push = useCallback(
    (tone: ToastTone, title: string, description?: string) => {
      const id = nextId.current++;
      setItems((current) => [...current.slice(-3), { id, tone, title, ...(description ? { description } : {}) }]);
      window.setTimeout(() => dismiss(id), tone === 'error' ? 7000 : 4500);
    },
    [dismiss],
  );

  const api = useMemo<ToastApi>(
    () => ({
      success: (title, description) => push('success', title, description),
      error: (title, description) => push('error', title, description),
      info: (title, description) => push('info', title, description),
    }),
    [push],
  );

  return (
    <ToastContext.Provider value={api}>
      {children}
      <div
        aria-live="polite"
        aria-relevant="additions"
        className="pointer-events-none fixed inset-x-0 bottom-20 z-50 flex flex-col items-center gap-2 px-4 lg:bottom-6 lg:items-end lg:pr-6"
      >
        {items.map((item) => {
          const Icon = icons[item.tone];
          return (
            <div
              key={item.id}
              role={item.tone === 'error' ? 'alert' : 'status'}
              className="card pointer-events-auto flex w-full max-w-sm animate-fade-in items-start gap-3 px-4 py-3"
            >
              <Icon
                aria-hidden
                className={cn(
                  'mt-0.5 size-5 shrink-0',
                  item.tone === 'success' && 'text-success',
                  item.tone === 'error' && 'text-danger',
                  item.tone === 'info' && 'text-accent',
                )}
              />
              <div className="min-w-0 flex-1">
                <p className="text-sm font-medium text-ink">{item.title}</p>
                {item.description ? <p className="mt-0.5 text-sm text-muted">{item.description}</p> : null}
              </div>
              <button
                type="button"
                onClick={() => dismiss(item.id)}
                className="rounded-md p-0.5 text-subtle hover:text-ink"
                aria-label="Dismiss notification"
              >
                <X aria-hidden className="size-4" />
              </button>
            </div>
          );
        })}
      </div>
    </ToastContext.Provider>
  );
}

export function useToast(): ToastApi {
  const context = useContext(ToastContext);
  if (!context) throw new Error('useToast must be used inside ToastProvider');
  return context;
}
