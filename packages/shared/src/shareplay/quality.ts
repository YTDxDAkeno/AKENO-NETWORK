/**
 * SharePlay stream quality presets. These are encoder targets for the broadcaster's browser,
 * not promises: the capture source decides what it can deliver (a capture card may be limited
 * to 1080p30, a Remote Play window to its own resolution), and WebRTC congestion control lowers
 * the bitrate (and with simulcast, the layer a viewer receives) when the network needs it.
 *
 * The default favours responsive remote co-op over picture quality: 720p at 30 frames.
 */
export const SHAREPLAY_QUALITIES = ['480p30', '720p30', '720p60', '1080p30', '1080p60'] as const;
export type SharePlayQuality = (typeof SHAREPLAY_QUALITIES)[number];

export interface SharePlayQualityPreset {
  id: SharePlayQuality;
  label: string;
  width: number;
  height: number;
  frameRate: number;
  /** Upper bound for the main layer in bits per second. */
  maxBitrate: number;
  /** A low simulcast layer for slow viewers (null: no extra layer). */
  lowLayer: { width: number; height: number; frameRate: number; maxBitrate: number } | null;
}

export const SHAREPLAY_QUALITY_PRESETS: Record<SharePlayQuality, SharePlayQualityPreset> = {
  '480p30': { id: '480p30', label: '480p · 30 fps (slow connections)', width: 854, height: 480, frameRate: 30, maxBitrate: 1_200_000, lowLayer: null },
  '720p30': {
    id: '720p30',
    label: '720p · 30 fps (recommended)',
    width: 1280,
    height: 720,
    frameRate: 30,
    maxBitrate: 2_500_000,
    lowLayer: { width: 640, height: 360, frameRate: 30, maxBitrate: 600_000 },
  },
  '720p60': {
    id: '720p60',
    label: '720p · 60 fps',
    width: 1280,
    height: 720,
    frameRate: 60,
    maxBitrate: 4_000_000,
    lowLayer: { width: 640, height: 360, frameRate: 30, maxBitrate: 600_000 },
  },
  '1080p30': {
    id: '1080p30',
    label: '1080p · 30 fps',
    width: 1920,
    height: 1080,
    frameRate: 30,
    maxBitrate: 4_500_000,
    lowLayer: { width: 854, height: 480, frameRate: 30, maxBitrate: 1_000_000 },
  },
  '1080p60': {
    id: '1080p60',
    label: '1080p · 60 fps',
    width: 1920,
    height: 1080,
    frameRate: 60,
    maxBitrate: 6_500_000,
    lowLayer: { width: 854, height: 480, frameRate: 30, maxBitrate: 1_000_000 },
  },
};

export const SHAREPLAY_DEFAULT_QUALITY: SharePlayQuality = '720p30';

/** The presets up to and including `max` (in the order of SHAREPLAY_QUALITIES). */
export function qualitiesUpTo(max: SharePlayQuality): SharePlayQuality[] {
  const index = SHAREPLAY_QUALITIES.indexOf(max);
  return SHAREPLAY_QUALITIES.slice(0, index + 1);
}

/** Game audio (Opus, stereo, no voice processing). */
export const SHAREPLAY_AUDIO_BITRATE = 128_000;
