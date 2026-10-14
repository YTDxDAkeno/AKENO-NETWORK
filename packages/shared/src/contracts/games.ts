/**
 * Canonical game records and their artwork (see docs/GAME_ARTWORK.md).
 *
 * The server owns game metadata resolution: clients receive a stable GameArtwork reference with
 * media IDs (see mediaUrl()) and never call artwork providers themselves.
 */

export type GameArtworkSlot = 'icon' | 'cover' | 'banner';

export const GAME_ARTWORK_SLOTS: readonly GameArtworkSlot[] = ['icon', 'cover', 'banner'];

/** Where artwork came from. Recorded for every game so administrators can review usage rights. */
export const GAME_ARTWORK_SOURCES = ['admin_upload', 'publisher_press_kit', 'licensed_provider', 'own_capture', 'other'] as const;
export type GameArtworkSource = (typeof GAME_ARTWORK_SOURCES)[number];

export const GAME_ARTWORK_SOURCE_LABELS: Record<GameArtworkSource, string> = {
  admin_upload: 'Uploaded by an administrator',
  publisher_press_kit: 'Publisher press kit / media site',
  licensed_provider: 'Licensed metadata provider',
  own_capture: 'Own screenshot or photo',
  other: 'Other (see notes)',
};

/** Artwork of a verified canonical game. A null slot means no artwork of that shape exists. */
export interface GameArtwork {
  gameId: string;
  /** Square icon (media ID). */
  icon: string | null;
  /** Vertical 2:3 box art (media ID). */
  cover: string | null;
  /** Wide 16:9 artwork for large cards (media ID). */
  banner: string | null;
}

/** Platform a PlayStation title ID belongs to (PPSA = PS5, CUSA = PS4). */
export type GameTitlePlatform = 'PS5' | 'PS4';

export interface GameTitleMappingDto {
  titleId: string;
  platform: GameTitlePlatform | null;
  source: string;
  updatedAt: string;
}

/** A canonical game as administrators see it. */
export interface GameDto {
  id: string;
  name: string;
  aliases: string[];
  titleIds: GameTitleMappingDto[];
  artwork: GameArtwork;
  artworkSource: GameArtworkSource | null;
  artworkAttribution: string;
  artworkLicense: string;
  createdAt: string;
  updatedAt: string;
  updatedBy: string | null;
}

/** A title ID reported by paired devices, for the missing-artwork report. */
export interface DetectedTitleDto {
  titleId: string;
  platform: GameTitlePlatform | null;
  /** Most recent name a device read from the console's metadata (unverified). */
  reportedName: string | null;
  players: number;
  sessions: number;
  lastPlayedAt: string;
  game: { id: string; name: string; hasIcon: boolean; hasCover: boolean } | null;
}
