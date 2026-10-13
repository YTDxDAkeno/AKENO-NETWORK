import { useEffect, useState } from 'react';
import { relativeTime } from '../lib/format';

/** Renders a relative timestamp that refreshes every minute. */
export function RelativeTime({ iso, className }: { iso: string; className?: string }) {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const timer = window.setInterval(() => setNow(Date.now()), 60_000);
    return () => window.clearInterval(timer);
  }, []);
  return (
    <time dateTime={iso} title={new Date(iso).toLocaleString('en-US')} className={className}>
      {relativeTime(iso, now)}
    </time>
  );
}
