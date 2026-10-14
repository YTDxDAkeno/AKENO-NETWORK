import type { ReactNode } from 'react';
import { Link } from 'react-router';
import type { PresenceView, UserSummary } from '@akeno/shared';
import { presenceDescription } from '../lib/format';
import { Avatar } from './Avatar';
import { GameActivityLine } from './GameActivity';
import { cn } from './cn';

/** A user line with avatar, name, Online ID / presence and optional actions. */
export function UserRow({
  user,
  presence,
  secondary,
  actions,
  size = 'md',
  className,
  linkToProfile = true,
}: {
  user: UserSummary;
  presence?: PresenceView;
  secondary?: ReactNode;
  actions?: ReactNode;
  size?: 'sm' | 'md';
  className?: string;
  linkToProfile?: boolean;
}) {
  const name = (
    <span className="block truncate font-medium text-ink">
      {user.displayName}
      <span className="ml-1.5 text-xs font-normal text-subtle">@{user.onlineId}</span>
    </span>
  );
  return (
    <div className={cn('flex items-center gap-3', className)}>
      <Avatar avatar={user.avatar} name={user.displayName} size={size} {...(presence ? { status: presence.status } : {})} />
      <div className="min-w-0 flex-1">
        {linkToProfile && !user.deleted ? (
          <Link to={`/u/${user.onlineId}`} className="block min-w-0 hover:underline">
            {name}
          </Link>
        ) : (
          name
        )}
        <span className="block truncate text-xs text-muted">
          {presence ? <span data-testid="presence-text">{presenceDescription(presence)}</span> : null}
          {presence && secondary ? <span aria-hidden> · </span> : null}
          {secondary}
          {presence?.activity ? (
            <span className="text-subtle">
              {' '}
              · {presence.activity} <span className="italic">(self-reported)</span>
            </span>
          ) : null}
        </span>
        {presence?.game ? <GameActivityLine game={presence.game} className="mt-0.5 max-w-full text-xs" /> : null}
      </div>
      {actions ? <div className="flex shrink-0 items-center gap-1.5">{actions}</div> : null}
    </div>
  );
}
