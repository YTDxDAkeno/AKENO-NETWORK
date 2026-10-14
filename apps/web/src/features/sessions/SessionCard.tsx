import { Globe, Lock, MapPin, Monitor, Users } from 'lucide-react';
import { Link } from 'react-router';
import { GAME_SESSION_STATUS_LABELS, type GameSessionDto } from '@akeno/shared';
import { Avatar } from '../../components/Avatar';
import { Badge } from '../../components/Badge';
import { cn } from '../../components/cn';
import { GameArt } from '../../components/GameArt';

const STATE_LABEL = { host: 'Hosting', member: 'Joined', requested: 'Requested', invited: 'Invited', none: null } as const;

export function SessionCard({ session }: { session: GameSessionDto }) {
  const fill = Math.round((session.participantCount / session.maxParticipants) * 100);
  const full = session.participantCount >= session.maxParticipants;
  const state = STATE_LABEL[session.viewerState];
  return (
    <Link to={`/sessions/${session.id}`} className="card group flex flex-col gap-4 p-5 transition-shadow hover:shadow-glow" data-testid="session-card">
      <div className="flex items-start justify-between gap-3">
        <GameArt art={session.art} name={session.gameName} className="size-12" sizes="48px" decorative />
        <div className="min-w-0 flex-1">
          <h3 className="truncate text-lg font-semibold group-hover:text-accent">{session.gameName}</h3>
          <div className="mt-1 flex items-center gap-2 text-xs text-muted">
            <Avatar avatar={session.host.avatar} image={session.host.avatarImage} name={session.host.displayName} size="xs" />
            Hosted by {session.host.displayName}
          </div>
        </div>
        <Badge tone={session.status === 'waiting' ? 'success' : session.status === 'active' ? 'accent' : 'neutral'}>
          {GAME_SESSION_STATUS_LABELS[session.status]}
        </Badge>
      </div>
      {session.description ? <p className="line-clamp-2 text-sm text-muted">{session.description}</p> : null}
      <div className="flex flex-wrap gap-1.5">
        {session.platform ? <Badge icon={<Monitor aria-hidden className="size-3" />}>{session.platform}</Badge> : null}
        {session.region ? <Badge icon={<MapPin aria-hidden className="size-3" />}>{session.region}</Badge> : null}
        <Badge icon={session.visibility === 'public' ? <Globe aria-hidden className="size-3" /> : <Lock aria-hidden className="size-3" />}>
          {session.visibility === 'public' ? 'Public' : session.visibility === 'friends' ? 'Friends' : 'Private'}
        </Badge>
        {session.joinMode === 'approval' ? <Badge tone="warning">Host approval</Badge> : null}
        {state ? <Badge tone="violet">{state}</Badge> : null}
      </div>
      <div className="mt-auto">
        <div className="mb-1.5 flex items-center justify-between text-xs text-muted">
          <span className="inline-flex items-center gap-1">
            <Users aria-hidden className="size-3.5" /> {session.participantCount}/{session.maxParticipants} players
          </span>
          {full ? <span className="font-medium text-warning">Full</span> : null}
        </div>
        <div className="h-1.5 overflow-hidden rounded-full bg-surface-3" role="progressbar" aria-valuemin={0} aria-valuemax={session.maxParticipants} aria-valuenow={session.participantCount} aria-label="Players">
          <div className={cn('h-full rounded-full', full ? 'bg-warning' : 'bg-accent')} style={{ width: `${fill}%` }} />
        </div>
      </div>
    </Link>
  );
}
