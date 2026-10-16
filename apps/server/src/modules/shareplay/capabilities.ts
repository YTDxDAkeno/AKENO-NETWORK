import type { SharePlayCapability, SharePlaySource } from '@akeno/shared';

/**
 * Capture sources and what is known about each one. These are statements about the platform,
 * not configuration: the PS5-native adapter stays unsupported until a verified capture
 * interface exists (docs/SHAREPLAY_STATUS.md, "PS5 capture"). It never falls back to test video.
 */
export const PS5_NATIVE_CAPTURE_UNSUPPORTED =
  'Streaming straight from the PS5 is not possible yet: no verified way exists for a payload to capture the video of a running game. Use a capture card or the PS Remote Play window on a PC instead.';

export const VIRTUAL_CONTROLLER_UNSUPPORTED =
  'Turning remote input into a real second controller on the PS5 is not possible yet: no verified virtual-controller interface exists for PS5 payloads. Player 2 input reaches the host’s browser and the AKENO console app for diagnostics only; it does not control games.';

export function sourceCapabilities(unavailable: string | null): Record<SharePlaySource, SharePlayCapability> {
  return {
    screen: unavailable ? { supported: false, reason: unavailable } : { supported: true, reason: null },
    'capture-device': unavailable ? { supported: false, reason: unavailable } : { supported: true, reason: null },
    'ps5-native': { supported: false, reason: PS5_NATIVE_CAPTURE_UNSUPPORTED },
  };
}
