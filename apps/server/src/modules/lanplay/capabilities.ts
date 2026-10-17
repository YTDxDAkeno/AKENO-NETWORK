import { LAN_PLAY_PLATFORMS, type LanPlayPlatform, type LanPlayPlatformPair } from '@akeno/shared';

/**
 * Honest, design-level statements about each platform pair. These describe what a *verified*
 * transport would still require for that pair; they never assert that it works. While no
 * transport is connected, `unavailable` is set as the reason on every pair so the UI shows
 * "Transport not available" rather than any pair-specific promise.
 */
const PAIR_NOTES: Record<string, { reason: string }> = {
  'pc:pc': {
    reason:
      'PC ↔ PC is the first target for a real transport: each PC can run the AKENO LAN Bridge itself, with no console in the path. Still requires a verified bridge, which is not available yet.',
  },
  'pc:ps5': {
    reason:
      'PS5 ↔ PC requires a Windows AKENO LAN Bridge next to the PS5 and two game builds whose LAN protocols are actually compatible. Not available yet, and compatibility must be shown per title.',
  },
  'ps5:ps5': {
    reason:
      'PS5 ↔ PS5 requires a companion bridge next to each console (a normal PS5 payload cannot tunnel another game process’s traffic) and a title with a working offline LAN mode. Research only until hardware-tested.',
  },
  'ps4:ps4': { reason: 'PS4 support is reserved for the future and is not implemented.' },
  'pc:ps4': { reason: 'PS4 support is reserved for the future and is not implemented.' },
  'ps4:ps5': { reason: 'PS4 support is reserved for the future and is not implemented.' },
};

function pairKey(a: LanPlayPlatform, b: LanPlayPlatform): string {
  return [a, b].sort().join(':');
}

/**
 * Capability of every platform pair. `transportReason` is the current blocker (e.g. no transport
 * connected); when present it overrides the pair note so no pair looks ready.
 */
export function platformPairs(transportReason: string | null): LanPlayPlatformPair[] {
  const pairs: LanPlayPlatformPair[] = [];
  for (let i = 0; i < LAN_PLAY_PLATFORMS.length; i++) {
    for (let j = i; j < LAN_PLAY_PLATFORMS.length; j++) {
      const a = LAN_PLAY_PLATFORMS[i]!;
      const b = LAN_PLAY_PLATFORMS[j]!;
      const note = PAIR_NOTES[pairKey(a, b)]?.reason ?? 'Not verified.';
      const future = a === 'ps4' || b === 'ps4';
      pairs.push({
        a,
        b,
        capability: {
          supported: false,
          reason: transportReason ?? note,
          evidence: future ? 'blocked' : 'requires-bridge',
        },
      });
    }
  }
  return pairs;
}
