import type {
  PresencePreference,
  PresenceView,
  PresenceVisibility,
  SelfPresenceStatus,
  VisiblePresenceStatus,
} from '@akeno/shared';

export interface PresenceSubject {
  connected: boolean;
  /** True when every live connection of the subject reports the client as idle. */
  allIdle: boolean;
  preference: PresencePreference;
  presenceVisibility: PresenceVisibility;
  lastSeenVisibility: PresenceVisibility;
  lastSeenAt: Date | null;
  activity: string | null;
  deleted?: boolean;
}

export interface PresenceViewer {
  isSelf: boolean;
  isFriend: boolean;
  /** Either side has blocked the other. */
  blocked: boolean;
}

/** The status the subject sees for themselves. */
export function effectiveSelfStatus(subject: Pick<PresenceSubject, 'connected' | 'allIdle' | 'preference'>): SelfPresenceStatus {
  if (!subject.connected) return 'offline';
  if (subject.preference === 'invisible') return 'invisible';
  if (subject.preference === 'dnd') return 'dnd';
  if (subject.preference === 'away' || subject.allIdle) return 'away';
  return 'online';
}

function audienceAllows(visibility: PresenceVisibility, viewer: PresenceViewer): boolean {
  if (visibility === 'everyone') return true;
  if (visibility === 'friends') return viewer.isFriend;
  return false;
}

/**
 * Applies privacy rules to produce what a given viewer may observe.
 *
 * - Blocked relationships always see "offline" with no last-seen or activity.
 * - Invisible users appear offline, and their last-seen value is the last time they were
 *   visibly online (the server does not update it while they are invisible).
 * - Last seen is only revealed when both the presence and last-seen audiences include the viewer.
 */
export function computePresenceView(subject: PresenceSubject, viewer: PresenceViewer): PresenceView {
  const hidden: PresenceView = { status: 'offline', lastSeenAt: null, activity: null };
  if (subject.deleted) return hidden;
  if (viewer.isSelf) {
    const self = effectiveSelfStatus(subject);
    return {
      status: self === 'invisible' ? 'offline' : self,
      lastSeenAt: subject.lastSeenAt ? subject.lastSeenAt.toISOString() : null,
      activity: subject.activity,
    };
  }
  if (viewer.blocked) return hidden;
  if (!audienceAllows(subject.presenceVisibility, viewer)) return hidden;

  const self = effectiveSelfStatus(subject);
  const status: VisiblePresenceStatus = self === 'invisible' ? 'offline' : self;
  const canSeeLastSeen = audienceAllows(subject.lastSeenVisibility, viewer);
  return {
    status,
    lastSeenAt: status === 'offline' && canSeeLastSeen && subject.lastSeenAt ? subject.lastSeenAt.toISOString() : null,
    activity: status === 'offline' ? null : subject.activity,
  };
}
