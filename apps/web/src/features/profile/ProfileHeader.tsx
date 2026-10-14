import type { ReactNode } from 'react';
import { mediaUrl, type BadgeId, type ProfileAppearance, type SelfPresenceStatus } from '@akeno/shared';
import { Avatar } from '../../components/Avatar';
import { BadgeChip, BadgeIcon } from '../../components/BadgeIcon';
import { cn } from '../../components/cn';
import { defaultBannerStyle } from '../../lib/theme';

export interface ProfileHeaderIdentity {
  displayName: string;
  onlineId: string;
  avatar: string;
  avatarImage: string | null;
}

/** The banner: uploaded image (responsive WebP + JPEG fallback) or the theme's default. */
export function ProfileBanner({ appearance, className }: { appearance: ProfileAppearance; className?: string }) {
  return (
    <div className={cn('relative w-full overflow-hidden', className)} style={appearance.bannerImage ? undefined : defaultBannerStyle(appearance)} data-testid="profile-banner">
      {appearance.bannerImage ? (
        <picture>
          <source
            type="image/webp"
            srcSet={`${mediaUrl(appearance.bannerImage, 'sm')} 600w, ${mediaUrl(appearance.bannerImage, 'md')} 960w, ${mediaUrl(appearance.bannerImage, 'lg')} 1500w`}
            sizes="(min-width: 1024px) 960px, 100vw"
          />
          <img src={mediaUrl(appearance.bannerImage, 'fallback')} alt="" decoding="async" className="absolute inset-0 size-full object-cover" />
        </picture>
      ) : (
        // A quiet geometric texture so the default banner is not a flat block.
        <svg className="absolute inset-0 size-full opacity-[0.14]" aria-hidden preserveAspectRatio="none" viewBox="0 0 600 200">
          <path d="M0 160 L140 60 L260 140 L380 40 L600 170" fill="none" stroke="#fff" strokeWidth="1.2" />
          <path d="M0 190 L160 100 L300 170 L430 80 L600 200" fill="none" stroke="#fff" strokeWidth="0.8" />
        </svg>
      )}
      {appearance.bannerOverlay ? (
        <div className="absolute inset-0 bg-[linear-gradient(180deg,rgb(4_6_12/0.15)_0%,rgb(4_6_12/0.55)_100%)]" aria-hidden />
      ) : null}
    </div>
  );
}

/**
 * Profile header: banner, overlapping avatar, name, Online ID, badges and actions. Text never
 * sits on the banner itself, so any uploaded image keeps the name and buttons readable.
 */
export function ProfileHeader({
  identity,
  appearance,
  status,
  featuredBadges,
  otherBadges = [],
  meta,
  actions,
  compact = false,
}: {
  identity: ProfileHeaderIdentity;
  appearance: ProfileAppearance;
  status?: SelfPresenceStatus;
  featuredBadges: BadgeId[];
  otherBadges?: BadgeId[];
  meta?: ReactNode;
  actions?: ReactNode;
  compact?: boolean;
}) {
  return (
    <div>
      <ProfileBanner appearance={appearance} className={compact ? 'aspect-[3/1] max-h-36' : 'aspect-[3/1] max-h-64 sm:max-h-72'} />
      <div className={cn('relative', compact ? 'px-4 pb-4' : 'px-5 pb-5 sm:px-8 sm:pb-6')}>
        <div className={cn('flex flex-col gap-4 sm:flex-row sm:items-end sm:justify-between', compact ? '-mt-10' : '-mt-14 sm:-mt-16')}>
          <div className="flex min-w-0 items-end gap-4">
            <Avatar
              avatar={identity.avatar}
              image={identity.avatarImage}
              name={identity.displayName}
              size={compact ? 'lg' : '2xl'}
              {...(status ? { status } : {})}
              className="rounded-full ring-4 ring-[var(--akeno-surface)]"
            />
            <div className="min-w-0 pb-1">
              <h1 className={cn('truncate font-bold text-ink', compact ? 'text-lg' : 'text-2xl sm:text-3xl')} data-testid="profile-display-name">
                {identity.displayName}
              </h1>
              <p className="truncate text-sm text-muted">@{identity.onlineId}</p>
            </div>
          </div>
          {actions ? <div className="flex flex-wrap items-center gap-2">{actions}</div> : null}
        </div>
        {featuredBadges.length > 0 || otherBadges.length > 0 ? (
          <div className="mt-4 flex flex-wrap items-center gap-2" data-testid="profile-badges">
            {featuredBadges.map((id) => (
              <BadgeChip key={id} id={id} />
            ))}
            {otherBadges.map((id) => (
              <BadgeIcon key={id} id={id} size="md" />
            ))}
          </div>
        ) : null}
        {meta ? <div className="mt-4">{meta}</div> : null}
      </div>
    </div>
  );
}
