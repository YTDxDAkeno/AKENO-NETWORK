import type { LanPlayGameEntry } from './contracts.js';

/**
 * LAN Play game compatibility database (research seed).
 *
 * Every entry records what is *documented* about a title's offline LAN / system-link mode and
 * cites where that came from. None of these has been hardware-tested across the AKENO tunnel, so
 * every `evidence` is "not-verified" or weaker and every `crossPlatform` is "not-verified" or
 * "incompatible". A matching title on two platforms is never assumed to interoperate: different
 * builds, regions and net-code make cross-platform LAN the exception, not the rule.
 *
 * This is a curated list of facts and source links only. No copyrighted game assets or artwork
 * are stored here; the website reuses AKENO's existing game catalogue artwork where a title is
 * already in it.
 */
export const LAN_PLAY_GAMES: LanPlayGameEntry[] = [
  {
    id: 'f1-23-ps5',
    title: 'F1 23',
    platform: 'ps5',
    edition: 'PS5 native',
    offlineLan: true,
    lanModeType: 'Offline multiplayer (2 players, same network); unofficial LAN play',
    maxPlayers: 2,
    authRequired: false,
    crossPlatform: 'not-verified',
    evidence: 'community-reported',
    notes:
      'A 2-player offline mode is official. Players report PS4/PS5 LAN races on the same network, but store listings mark LAN/system-link as unsupported. Treat as unverified until hardware-tested; championship saves are reportedly unavailable in LAN play.',
    sources: [
      { url: 'https://www.co-optimus.com/game/13658/playstation-5/f1-23.html', verifiedAt: '2026-10-16' },
      { url: 'https://forums.ea.com/discussions/f1-23-en/playstation-4-lan-multiplayer/8372086', verifiedAt: '2026-10-16' },
    ],
  },
  {
    id: 'borderlands-2-ps4',
    title: 'Borderlands 2',
    platform: 'ps4',
    edition: 'PS4 (played on PS5 via backward compatibility)',
    offlineLan: false,
    lanModeType: 'Split-screen and online co-op; no documented system-link LAN mode',
    maxPlayers: 4,
    authRequired: true,
    crossPlatform: 'incompatible',
    evidence: 'not-verified',
    notes:
      'A research candidate from the spec. The PS4 version offers local split-screen and PSN online co-op; no documented offline system-link LAN mode has been confirmed. Needs verification before it is offered.',
    sources: [{ url: 'https://store.playstation.com/', verifiedAt: '2026-10-16' }],
  },
  {
    id: 'borderlands-pre-sequel-ps4',
    title: 'Borderlands: The Pre-Sequel',
    platform: 'ps4',
    edition: 'PS4 (played on PS5 via backward compatibility)',
    offlineLan: false,
    lanModeType: 'Split-screen and online co-op; no documented system-link LAN mode',
    maxPlayers: 4,
    authRequired: true,
    crossPlatform: 'incompatible',
    evidence: 'not-verified',
    notes: 'Same situation as Borderlands 2. Research candidate only; offline LAN not confirmed.',
    sources: [{ url: 'https://store.playstation.com/', verifiedAt: '2026-10-16' }],
  },
];

export function findLanPlayGame(id: string): LanPlayGameEntry | undefined {
  return LAN_PLAY_GAMES.find((game) => game.id === id);
}
