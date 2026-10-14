import { useEffect, useState } from 'react';
import { Clock, Gamepad2, History } from 'lucide-react';
import type { CurrentGameActivity, GameActivityResponse } from '@akeno/shared';
import { formatDuration, formatPlaytime, gameGeneration, longDate, secondsSince } from '../lib/format';
import { RelativeTime } from './RelativeTime';
import { cn } from './cn';

const PLATFORM_LABELS: Record<string, string> = { PS5: 'PlayStation 5' };

/** Re-renders every 30 seconds so running durations stay current. */
function useNow(intervalMs = 30_000): number {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const timer = window.setInterval(() => setNow(Date.now()), intervalMs);
    return () => window.clearInterval(timer);
  }, [intervalMs]);
  return now;
}

export function gameTitle(game: { name: string | null; titleId: string }): string {
  return game.name ?? game.titleId;
}

/** One-line activity for user lists: "Playing Racing Game · 1 h 5 min". */
export function GameActivityLine({ game, className }: { game: CurrentGameActivity; className?: string }) {
  const now = useNow();
  return (
    <span className={cn('inline-flex min-w-0 items-center gap-1 text-success', className)} data-testid="game-activity-line">
      <Gamepad2 aria-hidden className="size-3.5 shrink-0" />
      <span className="truncate">
        Playing <span className="font-medium">{gameTitle(game)}</span> · {formatDuration(secondsSince(game.startedAt, now))}
      </span>
    </span>
  );
}

/** Discord-style card for the game a user is playing right now. */
export function GameActivityCard({ game }: { game: CurrentGameActivity }) {
  const now = useNow();
  const generation = gameGeneration(game.titleId);
  return (
    <div className="flex items-center gap-4 rounded-2xl border border-line bg-surface-2 p-4" data-testid="game-activity-card">
      <div
        aria-hidden
        className="grid size-16 shrink-0 place-items-center rounded-xl bg-[linear-gradient(135deg,#4f7cff,#8b5cf6)] text-white shadow-inner"
      >
        <Gamepad2 className="size-8" />
      </div>
      <div className="min-w-0 flex-1">
        <p className="text-xs font-semibold uppercase tracking-wider text-success">Playing now</p>
        <p className="truncate text-lg font-semibold text-ink" title={game.titleId}>
          {gameTitle(game)}
        </p>
        <p className="text-sm text-muted">
          {PLATFORM_LABELS[game.platform] ?? game.platform}
          {generation && generation !== 'PS5 game' ? ` · ${generation}` : ''} · for{' '}
          <span className="font-medium text-ink">{formatDuration(secondsSince(game.startedAt, now))}</span>
        </p>
      </div>
      <p className="hidden max-w-36 text-right text-xs text-subtle sm:block">Detected automatically by AKENO on the console</p>
    </div>
  );
}

/** Recent games, last played, last session and total tracked playtime. */
export function GameHistory({ activity, isSelf }: { activity: GameActivityResponse; isSelf: boolean }) {
  if (!activity.historyVisible) {
    return <p className="text-sm text-muted">This player does not share their play history with you.</p>;
  }
  if (activity.recentGames.length === 0) {
    return (
      <p className="text-sm text-muted">
        {isSelf
          ? 'No games tracked yet. Games appear here when AKENO for PS5 detects them on your paired console.'
          : 'No tracked games yet.'}
      </p>
    );
  }
  return (
    <div>
      <ul className="divide-y divide-line" data-testid="game-history">
        {activity.recentGames.map((game) => (
          <li key={game.titleId} className="flex items-center gap-3 py-3">
            <span aria-hidden className="grid size-10 shrink-0 place-items-center rounded-lg bg-surface-3 text-accent">
              <Gamepad2 className="size-5" />
            </span>
            <div className="min-w-0 flex-1">
              <p className="truncate font-medium text-ink" title={game.titleId}>
                {gameTitle(game)}
                {game.playingNow ? <span className="ml-2 text-xs font-semibold text-success">Playing now</span> : null}
                {game.excluded ? (
                  <span className="ml-2 text-xs font-semibold text-warning" title="Excluded in your privacy settings: never tracked and hidden from others">
                    Excluded
                  </span>
                ) : null}
              </p>
              <p className="text-xs text-muted">
                {game.playingNow ? (
                  'Session running'
                ) : (
                  <>
                    Last played <RelativeTime iso={game.lastPlayedAt} />
                    {game.lastSessionSeconds !== null ? ` · last session ${formatDuration(game.lastSessionSeconds)}` : ''}
                  </>
                )}
              </p>
            </div>
            <div className="shrink-0 text-right">
              <p
                className="inline-flex items-center gap-1 text-sm font-semibold text-ink"
                title={game.totalSeconds === null ? 'This player does not share their playtime' : 'Total tracked playtime'}
              >
                <Clock aria-hidden className="size-3.5 text-subtle" /> {formatPlaytime(game.totalSeconds)}
              </p>
              <p className="text-xs text-subtle">
                {game.sessionCount} session{game.sessionCount === 1 ? '' : 's'}
              </p>
            </div>
          </li>
        ))}
      </ul>
      {activity.trackedSince ? (
        <p className="mt-2 inline-flex items-center gap-1.5 text-xs text-subtle">
          <History aria-hidden className="size-3.5" />
          Tracked by AKENO since {longDate(activity.trackedSince)}. Earlier playtime is not known.
        </p>
      ) : null}
    </div>
  );
}
