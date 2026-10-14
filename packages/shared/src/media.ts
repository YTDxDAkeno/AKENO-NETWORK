/**
 * Uploaded images (profile pictures, profile banners, game artwork).
 *
 * The server validates and re-encodes every upload into the fixed variants below and stores
 * them under a random, non-guessable media ID. Clients only ever receive the media ID and build
 * URLs with mediaUrl(); the variant file names are part of the URL contract.
 */

export const MEDIA_KINDS = ['avatar', 'banner', 'game_icon', 'game_cover', 'game_banner'] as const;
export type MediaKind = (typeof MEDIA_KINDS)[number];

export const MEDIA_FORMATS = ['webp', 'jpeg'] as const;
export type MediaFormat = (typeof MEDIA_FORMATS)[number];

export interface MediaVariantSpec {
  /** File name in the URL, e.g. "md.webp". */
  file: string;
  /** Largest width and height; smaller sources are never enlarged. */
  width: number;
  height: number;
  format: MediaFormat;
}

export interface MediaKindSpec {
  /** Width:height of every variant. Uploads are center-cropped to this ratio. */
  aspect: { width: number; height: number };
  /** Smallest accepted source after cropping (prevents blurry, upscaled artwork). */
  minWidth: number;
  minHeight: number;
  variants: readonly MediaVariantSpec[];
}

const square = (lg: number, md: number, sm: number, fallback: number): readonly MediaVariantSpec[] => [
  { file: 'lg.webp', width: lg, height: lg, format: 'webp' },
  { file: 'md.webp', width: md, height: md, format: 'webp' },
  { file: 'sm.webp', width: sm, height: sm, format: 'webp' },
  { file: 'fallback.jpg', width: fallback, height: fallback, format: 'jpeg' },
];

/**
 * lg/md/sm are WebP for current browsers; fallback.jpg is a small JPEG for older engines (for
 * example the PS5 system browser, whose WebP support is not verified).
 */
export const MEDIA_SPECS: Record<MediaKind, MediaKindSpec> = {
  avatar: { aspect: { width: 1, height: 1 }, minWidth: 128, minHeight: 128, variants: square(512, 256, 128, 256) },
  banner: {
    aspect: { width: 3, height: 1 },
    minWidth: 600,
    minHeight: 200,
    variants: [
      { file: 'lg.webp', width: 1500, height: 500, format: 'webp' },
      { file: 'md.webp', width: 960, height: 320, format: 'webp' },
      { file: 'sm.webp', width: 600, height: 200, format: 'webp' },
      { file: 'fallback.jpg', width: 960, height: 320, format: 'jpeg' },
    ],
  },
  game_icon: { aspect: { width: 1, height: 1 }, minWidth: 128, minHeight: 128, variants: square(512, 256, 128, 256) },
  game_cover: {
    aspect: { width: 2, height: 3 },
    minWidth: 200,
    minHeight: 300,
    variants: [
      { file: 'lg.webp', width: 600, height: 900, format: 'webp' },
      { file: 'md.webp', width: 300, height: 450, format: 'webp' },
      { file: 'sm.webp', width: 150, height: 225, format: 'webp' },
      { file: 'fallback.jpg', width: 300, height: 450, format: 'jpeg' },
    ],
  },
  game_banner: {
    aspect: { width: 16, height: 9 },
    minWidth: 480,
    minHeight: 270,
    variants: [
      { file: 'lg.webp', width: 1280, height: 720, format: 'webp' },
      { file: 'md.webp', width: 640, height: 360, format: 'webp' },
      { file: 'sm.webp', width: 320, height: 180, format: 'webp' },
      { file: 'fallback.jpg', width: 640, height: 360, format: 'jpeg' },
    ],
  },
};

export type MediaVariantName = 'lg' | 'md' | 'sm' | 'fallback';

export const MEDIA_LIMITS = {
  /** Largest accepted upload. Clients crop and downscale before uploading. */
  uploadMaxBytes: 8 * 1024 * 1024,
  /** Decoding limit: larger images are rejected before they are fully decoded. */
  inputMaxPixels: 36_000_000,
  inputMaxDimension: 8192,
  /** Profile uploads (avatar + banner) per user per 24 hours. */
  dailyUploadsPerUser: 30,
  /** Accepted source formats, detected from the file contents (never from names or MIME types). */
  acceptedTypes: ['image/jpeg', 'image/png', 'image/webp', 'image/gif'] as const,
} as const;

/** Media IDs are 32 lowercase hex characters (128 random bits). */
export const MEDIA_ID_PATTERN = /^[0-9a-f]{32}$/;

export const MEDIA_PATH_PREFIX = '/media';

/** URL of one variant of an uploaded image. */
export function mediaUrl(mediaId: string, variant: MediaVariantName = 'md'): string {
  return `${MEDIA_PATH_PREFIX}/${mediaId}/${variant === 'fallback' ? 'fallback.jpg' : `${variant}.webp`}`;
}

/** Every file name a media object may have (used to validate request paths on the server). */
export const MEDIA_FILE_NAMES = ['lg.webp', 'md.webp', 'sm.webp', 'fallback.jpg'] as const;
