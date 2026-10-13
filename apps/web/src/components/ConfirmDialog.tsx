import { createContext, useCallback, useContext, useRef, useState, type ReactNode } from 'react';
import { Button } from './Button';
import { Modal } from './Modal';

interface ConfirmOptions {
  title: string;
  message: ReactNode;
  confirmLabel?: string;
  tone?: 'danger' | 'primary';
}

type Confirm = (options: ConfirmOptions) => Promise<boolean>;

const ConfirmContext = createContext<Confirm | null>(null);

/** Provides a promise-based confirmation dialog for destructive actions. */
export function ConfirmProvider({ children }: { children: ReactNode }) {
  const [options, setOptions] = useState<ConfirmOptions | null>(null);
  const resolver = useRef<((value: boolean) => void) | null>(null);

  const confirm = useCallback<Confirm>((next) => {
    setOptions(next);
    return new Promise<boolean>((resolve) => {
      resolver.current = resolve;
    });
  }, []);

  const finish = (value: boolean) => {
    resolver.current?.(value);
    resolver.current = null;
    setOptions(null);
  };

  return (
    <ConfirmContext.Provider value={confirm}>
      {children}
      <Modal
        open={options !== null}
        onClose={() => finish(false)}
        title={options?.title ?? ''}
        size="sm"
        footer={
          <>
            <Button variant="ghost" onClick={() => finish(false)}>
              Cancel
            </Button>
            <Button variant={options?.tone === 'primary' ? 'primary' : 'danger'} onClick={() => finish(true)} autoFocus>
              {options?.confirmLabel ?? 'Confirm'}
            </Button>
          </>
        }
      >
        <div className="text-sm text-muted">{options?.message}</div>
      </Modal>
    </ConfirmContext.Provider>
  );
}

export function useConfirm(): Confirm {
  const context = useContext(ConfirmContext);
  if (!context) throw new Error('useConfirm must be used inside ConfirmProvider');
  return context;
}
