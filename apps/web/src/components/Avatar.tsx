import { useState } from 'react';
import { mediaUrl, type SelfPresenceStatus } from '@akeno/shared';
import { AvatarGlyph, avatarPreset } from '../lib/avatars';
import { presenceLabel } from '../lib/format';
import { cn } from './cn';

const sizes = {
  xs: { box: 'size-7', px: 28, icon: 'size-3.5', dot: 'size-2.5 ring-2' },
  sm: { box: 'size-9', px: 36, icon: 'size-4', dot: 'size-3 ring-2' },
  md: { box: 'size-11', px: 44, icon: 'size-5', dot: 'size-3.5 ring-[3px]' },
  lg: { box: 'size-16', px: 64, icon: 'size-7', dot: 'size-4 ring-[3px]' },
  xl: { box: 'size-28', px: 112, icon: 'size-12', dot: 'size-6 ring-4' },
  '2xl': { box: 'size-28 sm:size-36', px: 144, icon: 'size-14', dot: 'size-7 ring-4' },
} as const;

export type AvatarSize = keyof typeof sizes;

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

/** Uploaded picture with WebP variants and a JPEG fallback; reports failures so the preset can show. */
function AvatarImage({ image, px, onError }: { image: string; px: number; onError: () => void }) {
  const [loaded, setLoaded] = useState(false);
  return (
    <picture>
      <source
        type="image/webp"
        srcSet={`${mediaUrl(image, 'sm')} 128w, ${mediaUrl(image, 'md')} 256w, ${mediaUrl(image, 'lg')} 512w`}
        sizes={`${px}px`}
      />
      <img
        src={mediaUrl(image, 'fallback')}
        alt=""
        width={px}
        height={px}
        loading="lazy"
        decoding="async"
        draggable={false}
        data-loaded={loaded}
        onLoad={() => setLoaded(true)}
        onError={onError}
        className="img-fade absolute inset-0 size-full rounded-full object-cover"
      />
    </picture>
  );
}

/**
 * A user's avatar: the uploaded picture when there is one, otherwise (or if it fails to load)
 * the preset badge. Always circular, with an optional presence dot.
 */
export function Avatar({
  avatar,
  image,
  name,
  size = 'md',
  status,
  className,
  speaking = false,
}: {
  avatar: string;
  image?: string | null;
  name: string;
  size?: AvatarSize;
  status?: SelfPresenceStatus;
  className?: string;
  /** Voice activity outline (driven by real audio levels in voice parties). */
  speaking?: boolean;
}) {
  const preset = avatarPreset(avatar);
  const dims = sizes[size];
  const [failed, setFailed] = useState<string | null>(null);
  const showImage = Boolean(image) && failed !== image;
  return (
    <span className={cn('relative inline-flex shrink-0 rounded-full', className)}>
      <span
        className={cn(
          'relative inline-flex items-center justify-center overflow-hidden rounded-full text-white transition-shadow duration-150',
          dims.box,
          speaking && 'shadow-[0_0_0_2px_var(--akeno-surface),0_0_0_4px_var(--color-status-online)]',
        )}
        style={showImage ? { backgroundColor: 'var(--akeno-surface-3)' } : { backgroundImage: `linear-gradient(135deg, ${preset.from}, ${preset.to})` }}
        role="img"
        aria-label={`${name}'s avatar`}
        data-speaking={speaking || undefined}
      >
        {showImage ? (
          <AvatarImage image={image!} px={dims.px} onError={() => setFailed(image ?? null)} />
        ) : (
          <AvatarGlyph avatar={avatar} className={cn(dims.icon, 'drop-shadow-[0_1px_1px_rgb(0_0_0/0.35)]')} />
        )}
      </span>
      {status ? (
        <PresenceDot status={status} className={cn('absolute -bottom-0.5 -right-0.5 ring-surface', dims.dot)} />
      ) : null}
    </span>
  );
}
