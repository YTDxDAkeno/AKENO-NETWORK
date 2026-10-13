import { Wifi, WifiOff } from 'lucide-react';
import { useEffect, useState } from 'react';
import { cn } from '../../components/cn';
import { useRealtime } from '../providers/RealtimeProvider';

export function ConnectionPill() {
  const { status } = useRealtime();
  const live = status === 'open';
  const label = live ? 'Live' : status === 'reconnecting' ? 'Reconnecting…' : status === 'connecting' ? 'Connecting…' : 'Offline';
  return (
    <span
      className={cn(
        'hidden items-center gap-1.5 rounded-full border px-2.5 py-1 text-xs font-medium sm:inline-flex',
        live ? 'border-success/30 text-success' : 'border-warning/40 text-warning',
      )}
      title={live ? 'Realtime updates are connected' : 'Realtime updates are not connected'}
      data-testid="connection-status"
      data-status={status}
    >
      {live ? <Wifi aria-hidden className="size-3.5" /> : <WifiOff aria-hidden className="size-3.5" />}
      {label}
    </span>
  );
}

/** Shown when the realtime connection has been down for more than two seconds. */
export function ConnectionBanner() {
  const { status } = useRealtime();
  const down = status === 'reconnecting' || status === 'connecting';
  const [visible, setVisible] = useState(false);
  useEffect(() => {
    if (!down) {
      const reset = window.setTimeout(() => setVisible(false), 0);
      return () => window.clearTimeout(reset);
    }
    const timer = window.setTimeout(() => setVisible(true), 2000);
    return () => window.clearTimeout(timer);
  }, [down]);
  if (!visible || !down) return null;
  return (
    <div role="status" className="mb-4 flex items-center gap-2 rounded-xl border border-warning/40 bg-warning/10 px-4 py-2.5 text-sm text-ink">
      <WifiOff aria-hidden className="size-4 text-warning" />
      Connection lost. Reconnecting… New messages and updates will sync automatically.
    </div>
  );
}
