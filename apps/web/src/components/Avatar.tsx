import type { SelfPresenceStatus } from '@akeno/shared';
import { AvatarGlyph, avatarPreset } from '../lib/avatars';
import { presenceLabel } from '../lib/format';
import { cn } from './cn';

const sizes = {
  xs: { box: 'size-7', icon: 'size-3.5', dot: 'size-2.5 ring-2' },
  sm: { box: 'size-9', icon: 'size-4', dot: 'size-3 ring-2' },
  md: { box: 'size-11', icon: 'size-5', dot: 'size-3.5 ring-[3px]' },
  lg: { box: 'size-16', icon: 'size-7', dot: 'size-4 ring-[3px]' },
  xl: { box: 'size-28', icon: 'size-12', dot: 'size-6 ring-4' },
} as const;

const statusColor: Record<SelfPresenceStatus, string> = {
  online: 'bg-status-online',
  away: 'bg-status-away',
  dnd: 'bg-status-dnd',
  invisible: 'bg-status-offline',
  offline: 'bg-status-offline',
};

export function PresenceDot({ status, className }: { status: SelfPresenceStatus; className?: string }) {
  return (
    <span
      className={cn('inline-block rounded-full', statusColor[status], className)}
      role="img"
      aria-label={presenceLabel(status)}
      title={presenceLabel(status)}
    />
  );
}

export function Avatar({
  avatar,
  name,
  size = 'md',
  status,
  className,
}: {
  avatar: string;
  name: string;
  size?: keyof typeof sizes;
  status?: SelfPresenceStatus;
  className?: string;
}) {
  const preset = avatarPreset(avatar);
  const dims = sizes[size];
  return (
    <span className={cn('relative inline-flex shrink-0', className)}>
      <span
        className={cn('inline-flex items-center justify-center rounded-full text-white shadow-inner', dims.box)}
        style={{ backgroundImage: `linear-gradient(135deg, ${preset.from}, ${preset.to})` }}
        role="img"
        aria-label={`${name}'s avatar`}
      >
        <AvatarGlyph avatar={avatar} className={cn(dims.icon, 'drop-shadow-[0_1px_1px_rgb(0_0_0/0.35)]')} />
      </span>
      {status ? (
        <PresenceDot status={status} className={cn('absolute -bottom-0.5 -right-0.5 ring-surface', dims.dot)} />
      ) : null}
    </span>
  );
}
