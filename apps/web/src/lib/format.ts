import { PRESENCE_LABELS, type SelfPresenceStatus } from '@akeno/shared';

const MINUTE = 60_000;
const HOUR = 60 * MINUTE;
const DAY = 24 * HOUR;

/** "just now", "5 minutes ago", "yesterday", or a short date. */
export function relativeTime(iso: string | null | undefined, now = Date.now()): string {
  if (!iso) return '';
  const time = new Date(iso).getTime();
  const diff = now - time;
  if (diff < 45_000) return 'just now';
  if (diff < HOUR) {
    const minutes = Math.max(1, Math.round(diff / MINUTE));
    return `${minutes} minute${minutes === 1 ? '' : 's'} ago`;
  }
  if (diff < DAY) {
    const hours = Math.round(diff / HOUR);
    return `${hours} hour${hours === 1 ? '' : 's'} ago`;
  }
  if (diff < 2 * DAY) return 'yesterday';
  if (diff < 7 * DAY) return `${Math.round(diff / DAY)} days ago`;
  return new Date(iso).toLocaleDateString('en-US', { month: 'short', day: 'numeric', year: 'numeric' });
}

export function clockTime(iso: string): string {
  return new Date(iso).toLocaleTimeString('en-US', { hour: 'numeric', minute: '2-digit' });
}

export function dayLabel(iso: string, now = new Date()): string {
  const date = new Date(iso);
  const startOfToday = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();
  const startOfDay = new Date(date.getFullYear(), date.getMonth(), date.getDate()).getTime();
  if (startOfDay === startOfToday) return 'Today';
  if (startOfDay === startOfToday - DAY) return 'Yesterday';
  return date.toLocaleDateString('en-US', { weekday: 'long', month: 'long', day: 'numeric' });
}

export function longDate(iso: string): string {
  return new Date(iso).toLocaleDateString('en-US', { month: 'long', day: 'numeric', year: 'numeric' });
}

export function presenceLabel(status: SelfPresenceStatus): string {
  return PRESENCE_LABELS[status];
}

/** Human-friendly description of a presence view, e.g. "Online" or "Last seen 2 hours ago". */
export function presenceDescription(presence: { status: SelfPresenceStatus; lastSeenAt: string | null }): string {
  if (presence.status === 'offline') {
    return presence.lastSeenAt ? `Last seen ${relativeTime(presence.lastSeenAt)}` : 'Offline';
  }
  return presenceLabel(presence.status);
}

export function pluralize(count: number, singular: string, plural = `${singular}s`): string {
  return `${count} ${count === 1 ? singular : plural}`;
}

/** Compact duration: "under a minute", "45 min", "2 h 5 min", "3 h". */
export function formatDuration(totalSeconds: number): string {
  const seconds = Math.max(0, Math.floor(totalSeconds));
  if (seconds < 60) return 'under a minute';
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes} min`;
  const hours = Math.floor(minutes / 60);
  const rest = minutes % 60;
  return rest === 0 ? `${hours} h` : `${hours} h ${rest} min`;
}

/** Playtime for display; null means the owner hides playtime from this viewer. */
export function formatPlaytime(totalSeconds: number | null): string {
  return totalSeconds === null ? 'hidden' : formatDuration(totalSeconds);
}

/** Seconds elapsed since an ISO timestamp (never negative). */
export function secondsSince(iso: string, now = Date.now()): number {
  return Math.max(0, Math.floor((now - new Date(iso).getTime()) / 1000));
}

/** "PS5 game" / "PS4 game" from a PlayStation title ID prefix. */
export function gameGeneration(titleId: string): string | null {
  if (titleId.startsWith('PPSA')) return 'PS5 game';
  if (titleId.startsWith('CUSA')) return 'PS4 game';
  return null;
}
