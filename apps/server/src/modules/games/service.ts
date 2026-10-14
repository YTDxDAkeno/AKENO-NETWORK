import { and, asc, desc, eq, ilike, inArray, isNull, or, sql } from 'drizzle-orm';
import {
  GAME_ACTIVITY,
  type DetectedTitleDto,
  type GameArtwork,
  type GameArtworkSlot,
  type GameDto,
  type GameTitleDto,
  type GameTitlePlatform,
  type MediaKind,
  type createGameSchema,
  type updateGameSchema,
} from '@akeno/shared';
import type { z } from 'zod';
import type { AppContext } from '../../app/context.js';
import { gameActivitySessions, gameTitleMappings, gameTitles, games } from '../../database/schema/index.js';
import { isUniqueViolation } from '../../utils/db-errors.js';
import { Errors } from '../../utils/errors.js';

type GameRow = typeof games.$inferSelect;

interface CatalogEntry {
  gameId: string;
  name: string;
  art: GameArtwork | null;
}

const SLOT_COLUMN = { icon: 'iconMediaId', cover: 'coverMediaId', banner: 'bannerMediaId' } as const;
const SLOT_KIND: Record<GameArtworkSlot, MediaKind> = { icon: 'game_icon', cover: 'game_cover', banner: 'game_banner' };

/** PPSA = PS5, CUSA = PS4; other prefixes are unknown. */
export function titlePlatform(titleId: string): GameTitlePlatform | null {
  if (titleId.startsWith('PPSA')) return 'PS5';
  if (titleId.startsWith('CUSA')) return 'PS4';
  return null;
}

/**
 * Exact-match key for game names: Unicode-normalized, case-folded, trademark signs removed and
 * punctuation collapsed. Two names match only when these keys are identical — there is no fuzzy
 * or "similar title" matching, so artwork can never attach to the wrong game by resemblance.
 */
export function normalizeGameName(name: string): string {
  return name
    .normalize('NFKC')
    .toLowerCase()
    .replace(/[™®©]/g, '')
    .replace(/[^\p{L}\p{N}]+/gu, ' ')
    .trim();
}

function artworkOf(row: Pick<GameRow, 'id' | 'iconMediaId' | 'coverMediaId' | 'bannerMediaId'>): GameArtwork | null {
  if (!row.iconMediaId && !row.coverMediaId && !row.bannerMediaId) return null;
  return { gameId: row.id, icon: row.iconMediaId, cover: row.coverMediaId, banner: row.bannerMediaId };
}

/**
 * Canonical game records, verified title-ID mappings and artwork (the "game artwork service").
 *
 * Resolution order for a detected title ID:
 *   1. a verified mapping (created by an administrator, or migrated from Alpha 0.2 curated names)
 *      to a canonical game, whose administrator-approved artwork is used;
 *   2. otherwise no artwork (clients show a clean fallback) and the name the device read from the
 *      console's own metadata, if any.
 * Unknown title IDs stay unknown until an administrator verifies them. External providers are
 * never queried on the request path (see docs/GAME_ARTWORK.md for the provider review).
 *
 * The catalogue is small and read on every presence update, so it is cached in memory (this
 * backend runs as a single instance, like presence) and rebuilt after every change.
 */
export class GameCatalogService {
  private byTitle = new Map<string, CatalogEntry>();
  private byName = new Map<string, CatalogEntry>();

  constructor(private readonly ctx: AppContext) {}

  async load(): Promise<void> {
    const [gameRows, mappingRows] = await Promise.all([
      this.ctx.db.select().from(games),
      this.ctx.db.select({ titleId: gameTitleMappings.titleId, gameId: gameTitleMappings.gameId }).from(gameTitleMappings),
    ]);
    const entries = new Map<string, CatalogEntry>();
    const byName = new Map<string, CatalogEntry>();
    const ambiguous = new Set<string>();
    for (const row of gameRows) {
      const entry: CatalogEntry = { gameId: row.id, name: row.name, art: artworkOf(row) };
      entries.set(row.id, entry);
      for (const name of [row.name, ...row.aliases]) {
        const key = normalizeGameName(name);
        if (!key) continue;
        if (byName.has(key) && byName.get(key)!.gameId !== row.id) ambiguous.add(key);
        else byName.set(key, entry);
      }
    }
    // A name shared by two games matches neither.
    for (const key of ambiguous) byName.delete(key);
    const byTitle = new Map<string, CatalogEntry>();
    for (const row of mappingRows) {
      const entry = entries.get(row.gameId);
      if (entry) byTitle.set(row.titleId, entry);
    }
    this.byTitle = byTitle;
    this.byName = byName;
  }

  // ---- Resolution (hot path, in memory) ------------------------------------------------------

  nameForTitle(titleId: string): string | null {
    return this.byTitle.get(titleId)?.name ?? null;
  }

  artForTitle(titleId: string): GameArtwork | null {
    return this.byTitle.get(titleId)?.art ?? null;
  }

  /** Artwork for a free-text game name (favorites): exact normalized match only. */
  artForName(name: string): GameArtwork | null {
    return this.byName.get(normalizeGameName(name))?.art ?? null;
  }

  /** Game sessions: a title ID identifier wins over the name. */
  artForSession(identifier: string | null, name: string): GameArtwork | null {
    if (identifier && GAME_ACTIVITY.titleIdPattern.test(identifier.trim().toUpperCase())) {
      const byId = this.byTitle.get(identifier.trim().toUpperCase());
      if (byId) return byId.art;
    }
    return this.artForName(name);
  }

  // ---- Administration ------------------------------------------------------------------------

  private async toDtos(rows: GameRow[]): Promise<GameDto[]> {
    if (rows.length === 0) return [];
    const mappings = await this.ctx.db
      .select()
      .from(gameTitleMappings)
      .where(
        inArray(
          gameTitleMappings.gameId,
          rows.map((row) => row.id),
        ),
      )
      .orderBy(asc(gameTitleMappings.titleId));
    const editors = await this.ctx.users.getSummaries(rows.flatMap((row) => (row.updatedById ? [row.updatedById] : [])));
    return rows.map((row) => ({
      id: row.id,
      name: row.name,
      aliases: row.aliases,
      titleIds: mappings
        .filter((mapping) => mapping.gameId === row.id)
        .map((mapping) => ({
          titleId: mapping.titleId,
          platform: titlePlatform(mapping.titleId),
          source: mapping.source,
          updatedAt: mapping.updatedAt.toISOString(),
        })),
      artwork: { gameId: row.id, icon: row.iconMediaId, cover: row.coverMediaId, banner: row.bannerMediaId },
      artworkSource: row.artworkSource ?? null,
      artworkAttribution: row.artworkAttribution,
      artworkLicense: row.artworkLicense,
      createdAt: row.createdAt.toISOString(),
      updatedAt: row.updatedAt.toISOString(),
      updatedBy: row.updatedById ? (editors.get(row.updatedById)?.onlineId ?? null) : null,
    }));
  }

  async list(options: { q?: string | undefined; missingArtwork?: boolean | undefined; limit: number }): Promise<GameDto[]> {
    const conditions = [];
    if (options.q) {
      const pattern = `%${options.q.replace(/[\\%_]/g, (match) => `\\${match}`)}%`;
      const titleMatches = this.ctx.db
        .select({ gameId: gameTitleMappings.gameId })
        .from(gameTitleMappings)
        .where(ilike(gameTitleMappings.titleId, pattern));
      conditions.push(or(ilike(games.name, pattern), inArray(games.id, titleMatches)));
    }
    if (options.missingArtwork) conditions.push(or(isNull(games.iconMediaId), isNull(games.coverMediaId)));
    const rows = await this.ctx.db
      .select()
      .from(games)
      .where(conditions.length > 0 ? and(...conditions) : undefined)
      .orderBy(asc(sql`lower(${games.name})`))
      .limit(options.limit);
    return this.toDtos(rows);
  }

  async get(gameId: string): Promise<GameDto> {
    const [row] = await this.ctx.db.select().from(games).where(eq(games.id, gameId));
    if (!row) throw Errors.notFound('The game');
    const [dto] = await this.toDtos([row]);
    return dto!;
  }

  async create(actorId: string | null, input: z.output<typeof createGameSchema>, ipAddress: string | null): Promise<GameDto> {
    const titleIds = [...new Set(input.titleIds)];
    let gameId: string;
    try {
      gameId = await this.ctx.db.transaction(async (tx) => {
        const [row] = await tx
          .insert(games)
          .values({
            name: input.name,
            aliases: dedupeNames(input.aliases),
            artworkSource: input.artworkSource,
            artworkAttribution: input.artworkAttribution,
            artworkLicense: input.artworkLicense,
            updatedById: actorId,
          })
          .returning({ id: games.id });
        if (titleIds.length > 0) {
          await tx.insert(gameTitleMappings).values(titleIds.map((titleId) => ({ titleId, gameId: row!.id, source: 'admin', updatedById: actorId })));
        }
        return row!.id;
      });
    } catch (error) {
      if (isUniqueViolation(error)) throw Errors.conflict('One of these title IDs already belongs to another game.');
      throw error;
    }
    await this.ctx.audit.record({ actorId, action: 'admin.game_created', targetType: 'game', targetId: gameId, metadata: { name: input.name, titleIds }, ipAddress });
    await this.changed(titleIds);
    return this.get(gameId);
  }

  async update(actorId: string | null, gameId: string, input: z.output<typeof updateGameSchema>, ipAddress: string | null): Promise<GameDto> {
    const patch: Partial<typeof games.$inferInsert> = { updatedAt: new Date(), updatedById: actorId };
    if (input.name !== undefined) patch.name = input.name;
    if (input.aliases !== undefined) patch.aliases = dedupeNames(input.aliases);
    if (input.artworkSource !== undefined) patch.artworkSource = input.artworkSource;
    if (input.artworkAttribution !== undefined) patch.artworkAttribution = input.artworkAttribution;
    if (input.artworkLicense !== undefined) patch.artworkLicense = input.artworkLicense;
    const updated = await this.ctx.db.update(games).set(patch).where(eq(games.id, gameId)).returning({ id: games.id });
    if (updated.length === 0) throw Errors.notFound('The game');
    await this.ctx.audit.record({ actorId, action: 'admin.game_updated', targetType: 'game', targetId: gameId, metadata: { fields: Object.keys(input) }, ipAddress });
    await this.changed(await this.titlesOf(gameId));
    return this.get(gameId);
  }

  async remove(actorId: string | null, gameId: string, ipAddress: string | null): Promise<void> {
    const titleIds = await this.titlesOf(gameId);
    const media = await this.ctx.db.transaction(async (tx) => {
      const [row] = await tx.delete(games).where(eq(games.id, gameId)).returning();
      if (!row) throw Errors.notFound('The game');
      return this.ctx.media.softDelete([row.iconMediaId, row.coverMediaId, row.bannerMediaId], tx);
    });
    await this.ctx.media.removeFiles(media);
    await this.ctx.audit.record({ actorId, action: 'admin.game_deleted', targetType: 'game', targetId: gameId, metadata: { titleIds }, ipAddress });
    await this.changed(titleIds);
  }

  /** Maps a title ID to a game (moves it if it belonged to another game). */
  async mapTitle(actorId: string | null, gameId: string, titleId: string, ipAddress: string | null): Promise<GameDto> {
    const [game] = await this.ctx.db.select({ id: games.id }).from(games).where(eq(games.id, gameId));
    if (!game) throw Errors.notFound('The game');
    const now = new Date();
    await this.ctx.db
      .insert(gameTitleMappings)
      .values({ titleId, gameId, source: 'admin', updatedById: actorId, updatedAt: now })
      .onConflictDoUpdate({ target: gameTitleMappings.titleId, set: { gameId, source: 'admin', updatedById: actorId, updatedAt: now } });
    await this.ctx.audit.record({ actorId, action: 'admin.game_title_mapped', targetType: 'game', targetId: gameId, metadata: { titleId }, ipAddress });
    await this.changed([titleId]);
    return this.get(gameId);
  }

  async unmapTitle(actorId: string | null, titleId: string, ipAddress: string | null): Promise<void> {
    const deleted = await this.ctx.db.delete(gameTitleMappings).where(eq(gameTitleMappings.titleId, titleId)).returning();
    if (deleted.length === 0) throw Errors.notFound('The game title');
    await this.ctx.audit.record({ actorId, action: 'admin.game_title_unmapped', targetType: 'game', targetId: deleted[0]!.gameId, metadata: { titleId }, ipAddress });
    await this.changed([titleId]);
  }

  async setArtwork(actorId: string | null, gameId: string, slot: GameArtworkSlot, data: Buffer, ipAddress: string | null): Promise<GameDto> {
    const [existing] = await this.ctx.db.select().from(games).where(eq(games.id, gameId));
    if (!existing) throw Errors.notFound('The game');
    const stored = await this.ctx.media.store(SLOT_KIND[slot], actorId, data);
    let replaced: string[];
    try {
      replaced = await this.ctx.db.transaction(async (tx) => {
        const [row] = await tx.select().from(games).where(eq(games.id, gameId)).for('update');
        if (!row) throw Errors.notFound('The game');
        const column = SLOT_COLUMN[slot];
        await tx
          .update(games)
          .set({ [column]: stored.id, updatedAt: new Date(), updatedById: actorId, artworkSource: row.artworkSource ?? 'admin_upload' })
          .where(eq(games.id, gameId));
        return this.ctx.media.softDelete([row[column]], tx);
      });
    } catch (error) {
      await this.ctx.media.discard(stored.id);
      throw error;
    }
    await this.ctx.media.removeFiles(replaced);
    await this.ctx.audit.record({ actorId, action: 'admin.game_artwork_set', targetType: 'game', targetId: gameId, metadata: { slot, mediaId: stored.id }, ipAddress });
    await this.changed(await this.titlesOf(gameId));
    return this.get(gameId);
  }

  async removeArtwork(actorId: string | null, gameId: string, slot: GameArtworkSlot, ipAddress: string | null): Promise<GameDto> {
    const removed = await this.ctx.db.transaction(async (tx) => {
      const [row] = await tx.select().from(games).where(eq(games.id, gameId)).for('update');
      if (!row) throw Errors.notFound('The game');
      const column = SLOT_COLUMN[slot];
      if (!row[column]) return [];
      await tx.update(games).set({ [column]: null, updatedAt: new Date(), updatedById: actorId }).where(eq(games.id, gameId));
      return this.ctx.media.softDelete([row[column]], tx);
    });
    await this.ctx.media.removeFiles(removed);
    await this.ctx.audit.record({ actorId, action: 'admin.game_artwork_removed', targetType: 'game', targetId: gameId, metadata: { slot }, ipAddress });
    await this.changed(await this.titlesOf(gameId));
    return this.get(gameId);
  }

  /**
   * Title IDs devices have reported, with how often they were played and whether they are
   * mapped to a game with artwork. This is the administrators' missing-artwork report.
   */
  async detectedTitles(limit = 200): Promise<DetectedTitleDto[]> {
    const s = gameActivitySessions;
    const rows = await this.ctx.db
      .select({
        titleId: s.titleId,
        reportedName: sql<string | null>`(array_agg(${s.titleName} ORDER BY ${s.startedAt} DESC) FILTER (WHERE ${s.titleName} IS NOT NULL))[1]`,
        players: sql<number>`count(DISTINCT ${s.userId})::int`,
        sessions: sql<number>`count(*)::int`,
        lastPlayedAt: sql<Date>`max(coalesce(${s.endedAt}, ${s.lastHeartbeatAt}))`,
        gameId: gameTitleMappings.gameId,
        gameName: games.name,
        iconMediaId: games.iconMediaId,
        coverMediaId: games.coverMediaId,
      })
      .from(s)
      .leftJoin(gameTitleMappings, eq(gameTitleMappings.titleId, s.titleId))
      .leftJoin(games, eq(games.id, gameTitleMappings.gameId))
      .groupBy(s.titleId, gameTitleMappings.gameId, games.name, games.iconMediaId, games.coverMediaId)
      .orderBy(desc(sql`count(DISTINCT ${s.userId})`), desc(sql`max(coalesce(${s.endedAt}, ${s.lastHeartbeatAt}))`))
      .limit(limit);
    return rows.map((row) => ({
      titleId: row.titleId,
      platform: titlePlatform(row.titleId),
      reportedName: row.reportedName,
      players: Number(row.players),
      sessions: Number(row.sessions),
      lastPlayedAt: new Date(row.lastPlayedAt).toISOString(),
      game: row.gameId
        ? { id: row.gameId, name: row.gameName ?? '', hasIcon: Boolean(row.iconMediaId), hasCover: Boolean(row.coverMediaId) }
        : null,
    }));
  }

  /** Rebuilds the in-memory catalogue from PostgreSQL ("invalidate cache"). */
  async reload(actorId: string | null, ipAddress: string | null): Promise<{ games: number; titles: number }> {
    await this.load();
    await this.ctx.audit.record({ actorId, action: 'admin.game_catalog_reloaded', ipAddress });
    await this.ctx.activity.republishTitles([...this.byTitle.keys()]);
    return { games: new Set([...this.byTitle.values()].map((entry) => entry.gameId)).size, titles: this.byTitle.size };
  }

  // ---- Alpha 0.2 compatible curated names (PUT/DELETE /admin/game-titles/:titleId) ---------------

  async listTitleNames(): Promise<GameTitleDto[]> {
    const rows = await this.ctx.db
      .select({ titleId: gameTitleMappings.titleId, name: games.name, updatedAt: gameTitleMappings.updatedAt })
      .from(gameTitleMappings)
      .innerJoin(games, eq(games.id, gameTitleMappings.gameId))
      .orderBy(asc(gameTitleMappings.titleId));
    return rows.map((row) => ({ titleId: row.titleId, name: row.name, updatedAt: row.updatedAt.toISOString() }));
  }

  /**
   * Sets the name shown for a title ID. A mapped title renames its game (when the game has no
   * other title IDs) or moves to a game of that exact name; an unmapped title gets a new game.
   */
  async setTitleName(actorId: string | null, titleId: string, name: string, ipAddress: string | null): Promise<GameTitleDto> {
    const now = new Date();
    await this.ctx.db.transaction(async (tx) => {
      const [mapping] = await tx.select().from(gameTitleMappings).where(eq(gameTitleMappings.titleId, titleId)).for('update');
      if (mapping) {
        await tx.update(games).set({ name, updatedAt: now, updatedById: actorId }).where(eq(games.id, mapping.gameId));
        await tx.update(gameTitleMappings).set({ updatedAt: now, updatedById: actorId }).where(eq(gameTitleMappings.titleId, titleId));
      } else {
        const [game] = await tx.insert(games).values({ name, updatedById: actorId }).returning({ id: games.id });
        await tx.insert(gameTitleMappings).values({ titleId, gameId: game!.id, source: 'admin', updatedById: actorId });
      }
      // Keep the Alpha 0.2 table in step so a rollback shows the same names.
      await tx
        .insert(gameTitles)
        .values({ titleId, name, updatedById: actorId, updatedAt: now })
        .onConflictDoUpdate({ target: gameTitles.titleId, set: { name, updatedById: actorId, updatedAt: now } });
    });
    await this.ctx.audit.record({ actorId, action: 'admin.game_title_named', targetType: 'game_title', targetId: titleId, metadata: { name }, ipAddress });
    await this.changed([titleId]);
    return { titleId, name, updatedAt: now.toISOString() };
  }

  async removeTitleName(actorId: string | null, titleId: string, ipAddress: string | null): Promise<void> {
    await this.ctx.db.transaction(async (tx) => {
      const [mapping] = await tx.delete(gameTitleMappings).where(eq(gameTitleMappings.titleId, titleId)).returning();
      await tx.delete(gameTitles).where(eq(gameTitles.titleId, titleId));
      if (!mapping) throw Errors.notFound('The game title');
      // A game left without title IDs, aliases or artwork was only a name; remove it too.
      const [rest] = await tx.select({ n: sql<number>`count(*)::int` }).from(gameTitleMappings).where(eq(gameTitleMappings.gameId, mapping.gameId));
      const [game] = await tx.select().from(games).where(eq(games.id, mapping.gameId));
      if (game && Number(rest?.n ?? 0) === 0 && game.aliases.length === 0 && !artworkOf(game)) {
        await tx.delete(games).where(eq(games.id, game.id));
      }
    });
    await this.ctx.audit.record({ actorId, action: 'admin.game_title_unmapped', targetType: 'game_title', targetId: titleId, ipAddress });
    await this.changed([titleId]);
  }

  private async titlesOf(gameId: string): Promise<string[]> {
    const rows = await this.ctx.db.select({ titleId: gameTitleMappings.titleId }).from(gameTitleMappings).where(eq(gameTitleMappings.gameId, gameId));
    return rows.map((row) => row.titleId);
  }

  /** Reloads the cache and refreshes presence of everyone currently playing an affected title. */
  private async changed(titleIds: string[]): Promise<void> {
    await this.load();
    await this.ctx.activity.republishTitles(titleIds);
  }
}

function dedupeNames(names: readonly string[]): string[] {
  const seen = new Set<string>();
  return names.filter((name) => {
    const key = normalizeGameName(name);
    if (!key || seen.has(key)) return false;
    seen.add(key);
    return true;
  });
}
