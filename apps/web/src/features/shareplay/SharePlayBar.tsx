import { Eye, MonitorUp, SquareStop, LogOut } from 'lucide-react';
import { Link } from 'react-router';
import { cn } from '../../components/cn';
import { isStreamActive, useStream, useStreamController } from './SharePlayProvider';

/**
 * Compact SharePlay status on every page while broadcasting or watching (the stream continues
 * across pages). While broadcasting it is also the capture indicator.
 */
export function SharePlayBar({ className }: { className?: string }) {
  const controller = useStreamController();
  const stream = useStream();
  if (!stream || !isStreamActive(stream)) return null;
  const host = stream.role === 'host';
  return (
    <div className={cn('flex items-center gap-2 rounded-xl border px-3 py-2', host ? 'border-danger/40 bg-danger-soft' : 'border-line bg-surface-2', className)} data-testid="shareplay-bar">
      <Link to="/parties" className="flex min-w-0 flex-1 items-center gap-2">
        {host ? <MonitorUp aria-hidden className="size-4 shrink-0 text-danger" /> : <Eye aria-hidden className="size-4 shrink-0 text-accent" />}
        <span className="min-w-0">
          <span className="block truncate text-xs font-semibold text-ink">{host ? 'You are streaming' : 'Watching SharePlay'}</span>
          <span className="block truncate text-[11px] text-muted">
            {stream.status === 'live' ? (host ? (stream.captureLabel ?? 'Live') : 'Live') : stream.status === 'reconnecting' ? 'Reconnecting…' : 'Connecting…'}
          </span>
        </span>
      </Link>
      <button
        type="button"
        onClick={() => void controller?.leave()}
        className="rounded-lg p-2 text-danger hover:bg-danger-soft"
        aria-label={host ? 'Stop SharePlay' : 'Leave stream'}
      >
        {host ? <SquareStop aria-hidden className="size-4" /> : <LogOut aria-hidden className="size-4" />}
      </button>
    </div>
  );
}
