import { useState, type CSSProperties } from 'react';
import { mediaUrl, type GameArtwork } from '@akeno/shared';
import { cn } from './cn';

export type GameArtVariant = 'icon' | 'cover' | 'banner';

/** Muted, deterministic tones for fallback tiles (never random per render). */
const FALLBACK_TONES = ['#26324f', '#2f2a4f', '#203f45', '#43302a', '#2c3f2b', '#40283c'];

function toneFor(seed: string): string {
  let hash = 0;
  for (let index = 0; index < seed.length; index++) hash = (hash * 31 + seed.charCodeAt(index)) | 0;
  return FALLBACK_TONES[Math.abs(hash) % FALLBACK_TONES.length]!;
}

/** "Elden Ring" → "ER", "Baldur's Gate 3" → "BG", "PPSA01234" → "PP". */
export function gameInitials(name: string): string {
  const words = name
    .replace(/[^\p{L}\p{N}\s]/gu, ' ')
    .split(/\s+/)
    .filter(Boolean);
  if (words.length === 0) return '?';
  if (words.length === 1) return words[0]!.slice(0, 2).toUpperCase();
  return (words[0]![0]! + words[1]![0]!).toUpperCase();
}

const ASPECT: Record<GameArtVariant, string> = { icon: 'aspect-square', cover: 'aspect-[2/3]', banner: 'aspect-video' };
const VIEWBOX: Record<GameArtVariant, string> = { icon: '0 0 100 100', cover: '0 0 100 150', banner: '0 0 160 90' };

function Picture({
  mediaId,
  alt,
  sizes,
  eager,
  className,
  onError,
}: {
  mediaId: string;
  alt: string;
  sizes: string;
  eager?: boolean;
  className?: string;
  onError: () => void;
}) {
  const [loaded, setLoaded] = useState(false);
  return (
    <picture>
      <source
        type="image/webp"
        srcSet={`${mediaUrl(mediaId, 'sm')} 160w, ${mediaUrl(mediaId, 'md')} 320w, ${mediaUrl(mediaId, 'lg')} 640w`}
        sizes={sizes}
      />
      <img
        src={mediaUrl(mediaId, 'fallback')}
        alt={alt}
        loading={eager ? 'eager' : 'lazy'}
        decoding="async"
        draggable={false}
        data-loaded={loaded}
        onLoad={() => setLoaded(true)}
        onError={onError}
        className={cn('img-fade absolute inset-0 size-full object-cover', className)}
      />
    </picture>
  );
}

/**
 * Game artwork in the right shape. Square icons are never stretched into covers: a missing cover
 * shows the icon centred inside the cover frame, and missing artwork shows a clean, labelled
 * fallback tile. Images are lazy-loaded and the box reserves its space (no layout shift).
 */
export function GameArt({
  art,
  name,
  variant = 'icon',
  className,
  sizes,
  eager = false,
  rounded = variant === 'icon' ? 'rounded-xl' : 'rounded-lg',
  decorative = false,
}: {
  art: GameArtwork | null | undefined;
  /** Game name (or title ID when unknown) for the alt text and fallback. */
  name: string;
  variant?: GameArtVariant;
  className?: string;
  /** Rendered width for responsive image selection, e.g. "48px" or "(min-width: 640px) 160px, 33vw". */
  sizes?: string;
  eager?: boolean;
  rounded?: string;
  /** The game name is shown next to the image, so screen readers skip it. */
  decorative?: boolean;
}) {
  const [failed, setFailed] = useState<Set<string>>(() => new Set());
  const usable = (id: string | null | undefined): id is string => Boolean(id) && !failed.has(id!);
  const fail = (id: string) => () => setFailed((current) => new Set(current).add(id));
  const alt = decorative ? '' : `${name} artwork`;
  const defaultSizes = variant === 'icon' ? '64px' : variant === 'cover' ? '160px' : '640px';

  // Pick the best artwork for this shape without distorting it.
  let primary: string | null = null;
  let inset: string | null = null;
  if (variant === 'icon') primary = usable(art?.icon) ? art!.icon : usable(art?.cover) ? art!.cover : null;
  if (variant === 'cover') {
    primary = usable(art?.cover) ? art!.cover : null;
    if (!primary && usable(art?.icon)) inset = art!.icon;
  }
  if (variant === 'banner') {
    primary = usable(art?.banner) ? art!.banner : null;
    if (!primary && usable(art?.cover)) inset = art!.cover;
    else if (!primary && usable(art?.icon)) inset = art!.icon;
  }

  const frame = cn('relative isolate block shrink-0 overflow-hidden bg-surface-3', ASPECT[variant], rounded, className);
  if (primary) {
    return (
      <span className={frame} data-testid="game-art" data-art-variant={variant}>
        <Picture mediaId={primary} alt={alt} sizes={sizes ?? defaultSizes} eager={eager} onError={fail(primary)} />
      </span>
    );
  }

  const tone: CSSProperties = { backgroundColor: toneFor(name) };
  if (inset) {
    // Shown inside the frame at its own aspect ratio, on a soft blurred copy of itself.
    return (
      <span className={frame} style={tone} data-testid="game-art" data-art-variant={variant}>
        <span className="absolute inset-0 scale-110 opacity-40 blur-xl" aria-hidden>
          <Picture mediaId={inset} alt="" sizes="160px" onError={fail(inset)} />
        </span>
        <span className={cn('absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2 overflow-hidden rounded-xl shadow-lg', variant === 'banner' ? 'h-3/4 aspect-[2/3]' : 'w-3/4 aspect-square')}>
          <Picture mediaId={inset} alt={alt} sizes={sizes ?? '160px'} eager={eager} onError={fail(inset)} />
        </span>
      </span>
    );
  }

  return (
    <span className={frame} style={tone} role={decorative ? undefined : 'img'} aria-label={decorative ? undefined : `${name} (no artwork)`} aria-hidden={decorative || undefined} data-testid="game-art-fallback" data-art-variant={variant}>
      <span className="absolute inset-0 bg-[linear-gradient(160deg,rgb(255_255_255/0.08),transparent_55%)]" aria-hidden />
      {/* SVG text scales with the tile at every size. */}
      <svg viewBox={VIEWBOX[variant]} className="absolute inset-0 size-full" aria-hidden>
        <text
          x="50%"
          y="50%"
          dominantBaseline="central"
          textAnchor="middle"
          fill="rgb(255 255 255 / 0.82)"
          style={{ fontFamily: 'var(--font-display)' }}
          fontWeight="700"
          fontSize={variant === 'banner' ? 30 : 34}
          letterSpacing="1"
        >
          {gameInitials(name)}
        </text>
      </svg>
    </span>
  );
}
