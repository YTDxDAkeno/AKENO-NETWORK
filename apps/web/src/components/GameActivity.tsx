import { useEffect, useState } from 'react';
import { Clock, History } from 'lucide-react';
import type { CurrentGameActivity, GameActivityResponse, GameHistoryEntry } from '@akeno/shared';
import { formatDuration, formatDurationShort, formatPlaytime, gameGeneration, longDate, secondsSince } from '../lib/format';
import { GameArt } from './GameArt';
import { RelativeTime } from './RelativeTime';
import { cn } from './cn';

const PLATFORM_LABELS: Record<string, string> = { PS5: 'PlayStation 5' };

/** Re-renders every 30 seconds so running durations stay current. */
export function useNow(intervalMs = 30_000): number {
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

/** "PlayStation 5 · PS4 game" — the console it runs on and, if different, the game's generation. */
export function platformLine(game: { platform: string; titleId: string }): string {
  const platform = PLATFORM_LABELS[game.platform] ?? game.platform;
  const generation = gameGeneration(game.titleId);
  return generation && generation !== 'PS5 game' ? `${platform} · ${generation}` : platform;
}

/** One-line activity for user lists: [icon] Playing Elden Ring · 1 h 5 min. */
export function GameActivityLine({ game, className }: { game: CurrentGameActivity; className?: string }) {
  const now = useNow();
  return (
    <span className={cn('inline-flex min-w-0 items-center gap-1.5 text-success', className)} data-testid="game-activity-line">
      <GameArt art={game.art} name={gameTitle(game)} className="size-4" rounded="rounded-[4px]" sizes="16px" decorative />
      <span className="truncate">
        Playing <span className="font-medium">{gameTitle(game)}</span> · {formatDuration(secondsSince(game.startedAt, now))}
      </span>
    </span>
  );
}

/** The game a user is playing right now, with its artwork. */
export function GameActivityCard({ game, compact = false }: { game: CurrentGameActivity; compact?: boolean }) {
  const now = useNow();
  const title = gameTitle(game);
  const hasBanner = Boolean(game.art?.banner);
  return (
    <div
      className={cn('relative isolate overflow-hidden rounded-2xl border border-line bg-surface-2', compact ? 'p-3' : 'p-4 sm:p-5')}
      data-testid="game-activity-card"
    >
      {hasBanner && !compact ? (
        <>
          <GameArt art={game.art} name={title} variant="banner" className="absolute inset-0 -z-10 size-full !aspect-auto" rounded="" sizes="(min-width: 1024px) 720px, 100vw" decorative />
          <div className="absolute inset-0 -z-10 bg-[linear-gradient(90deg,rgb(6_9_18/0.92)_0%,rgb(6_9_18/0.7)_55%,rgb(6_9_18/0.35)_100%)]" aria-hidden />
        </>
      ) : null}
      <div className="flex items-center gap-4">
        <GameArt art={game.art} name={title} className={compact ? 'size-14' : 'size-20 sm:size-24'} sizes={compact ? '56px' : '96px'} eager decorative />
        <div className={cn('min-w-0 flex-1', hasBanner && !compact && 'text-white')}>
          <p className="inline-flex items-center gap-1.5 text-xs font-semibold uppercase tracking-wider text-success">
            <span className="size-1.5 rounded-full bg-success" aria-hidden /> Playing now
          </p>
          <p className={cn('truncate font-semibold', compact ? 'text-base' : 'text-lg sm:text-xl', !(hasBanner && !compact) && 'text-ink')} title={game.titleId}>
            {title}
          </p>
          <p className={cn('text-sm', hasBanner && !compact ? 'text-white/75' : 'text-muted')}>
            {platformLine(game)} · for{' '}
            <span className={cn('font-medium', hasBanner && !compact ? 'text-white' : 'text-ink')}>{formatDuration(secondsSince(game.startedAt, now))}</span>
          </p>
        </div>
      </div>
    </div>
  );
}

function RecentGameTile({ game }: { game: GameHistoryEntry }) {
  const title = gameTitle(game);
  return (
    <li className="group min-w-0" data-testid="recent-game">
      <div className="relative">
        <GameArt art={game.art} name={title} variant="cover" sizes="(min-width: 1024px) 150px, 30vw" className="w-full shadow-[0_8px_20px_-12px_rgb(0_0_0/0.8)]" />
        {game.playingNow ? (
          <span className="absolute left-1.5 top-1.5 rounded-md bg-success px-1.5 py-0.5 text-[10px] font-bold uppercase tracking-wide text-[#05210f]">Now</span>
        ) : null}
        {game.excluded ? (
          <span className="absolute right-1.5 top-1.5 rounded-md bg-warning px-1.5 py-0.5 text-[10px] font-bold uppercase text-[#2b1a00]" title="Excluded in your privacy settings">
            Excluded
          </span>
        ) : null}
      </div>
      <p className="mt-2 truncate text-sm font-medium text-ink" title={`${title} (${game.titleId})`}>
        {title}
      </p>
      <p className="truncate text-xs text-muted">
        {game.playingNow ? (
          'Playing now'
        ) : (
          <>
            Last played <RelativeTime iso={game.lastPlayedAt} />
          </>
        )}
      </p>
      {!game.playingNow && game.lastSessionSeconds !== null ? (
        <p className="truncate text-xs text-subtle">Last session {formatDurationShort(game.lastSessionSeconds)}</p>
      ) : null}
      <p className="truncate text-xs text-subtle" title={game.totalSeconds === null ? 'This player does not share their playtime' : 'Total tracked playtime'}>
        <Clock aria-hidden className="mr-1 inline size-3 align-[-2px]" />
        {formatPlaytime(game.totalSeconds)} · {game.sessionCount} session{game.sessionCount === 1 ? '' : 's'}
      </p>
    </li>
  );
}

/** Recently played games as cover tiles, with last played, last session and total tracked playtime. */
export function GameHistory({ activity, isSelf, limit }: { activity: GameActivityResponse; isSelf: boolean; limit?: number }) {
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
  const games = limit ? activity.recentGames.slice(0, limit) : activity.recentGames;
  return (
    <div>
      <ul className="grid grid-cols-3 gap-x-3 gap-y-4 sm:grid-cols-4 lg:grid-cols-5" data-testid="game-history">
        {games.map((game) => (
          <RecentGameTile key={game.titleId} game={game} />
        ))}
      </ul>
      {activity.trackedSince ? (
        <p className="mt-4 inline-flex items-center gap-1.5 text-xs text-subtle">
          <History aria-hidden className="size-3.5" />
          Tracked by AKENO since {longDate(activity.trackedSince)}. Earlier playtime is not known.
        </p>
      ) : null}
    </div>
  );
}
