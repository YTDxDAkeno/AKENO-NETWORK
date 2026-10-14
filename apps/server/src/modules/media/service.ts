import { randomBytes } from 'node:crypto';
import { constants as fsConstants } from 'node:fs';
import { access, mkdir, readdir, rename, rm, stat, statfs, writeFile } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { and, count, eq, gt, inArray, isNotNull, isNull, lt, sql } from 'drizzle-orm';
import { MEDIA_FILE_NAMES, MEDIA_ID_PATTERN, MEDIA_LIMITS, type MediaKind } from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import type { DbExecutor } from '../../database/client.js';
import { mediaObjects } from '../../database/schema/index.js';
import { AppError } from '../../utils/errors.js';
import { Semaphore, processImage } from './image-processing.js';

type MediaRow = typeof mediaObjects.$inferSelect;

/** Kinds counted against the per-user daily upload quota. */
const PROFILE_KINDS: MediaKind[] = ['avatar', 'banner'];

/** Soft-deleted rows are kept this long (quota accounting), then purged. */
const PURGE_AFTER_MS = 48 * 60 * 60 * 1000;
/** Files without a database row older than this are orphans (crash between write and insert). */
const ORPHAN_AGE_MS = 60 * 60 * 1000;

export const FILE_CONTENT_TYPES: Record<string, string> = {
  'lg.webp': 'image/webp',
  'md.webp': 'image/webp',
  'sm.webp': 'image/webp',
  'fallback.jpg': 'image/jpeg',
};

/**
 * Stores uploaded images on a persistent volume (MEDIA_DIR) with their metadata in PostgreSQL.
 *
 * Layout: MEDIA_DIR/objects/<first two hex chars>/<media id>/<variant file>. Paths are built only
 * from a validated media ID and a fixed list of file names, so request input can never reach
 * other parts of the filesystem. Files are written to MEDIA_DIR/tmp first and moved into place
 * with one rename, so readers never see half-written objects.
 */
export class MediaService {
  private readonly root: string;
  private readonly objectsDir: string;
  private readonly tmpDir: string;
  private readonly semaphore: Semaphore;
  private availableFlag = false;
  private unavailableReason = 'Media storage has not been initialized.';

  constructor(private readonly ctx: AppContext) {
    this.root = resolve(ctx.config.media.dir);
    this.objectsDir = join(this.root, 'objects');
    this.tmpDir = join(this.root, 'tmp');
    this.semaphore = new Semaphore(ctx.config.media.concurrency);
  }

  get available(): boolean {
    return this.availableFlag;
  }

  /**
   * Creates the directories and checks that they are writable. Failure only disables uploads;
   * the rest of AKENO NETWORK keeps working.
   */
  async init(): Promise<void> {
    try {
      await mkdir(this.objectsDir, { recursive: true, mode: 0o750 });
      await mkdir(this.tmpDir, { recursive: true, mode: 0o750 });
      const probe = join(this.tmpDir, `.write-test-${randomBytes(4).toString('hex')}`);
      await writeFile(probe, 'ok');
      await rm(probe, { force: true });
      this.availableFlag = true;
    } catch (error) {
      this.availableFlag = false;
      this.unavailableReason = 'Image uploads are unavailable because the media storage is not writable.';
      this.ctx.log.error({ err: error, dir: this.root }, 'Media storage is not writable; uploads are disabled');
    }
  }

  private requireAvailable(): void {
    if (!this.availableFlag) throw new AppError(503, 'MEDIA_UNAVAILABLE', this.unavailableReason);
  }

  /** Absolute path of a stored variant, or null for anything that is not a valid reference. */
  filePath(mediaId: string, file: string): string | null {
    if (!MEDIA_ID_PATTERN.test(mediaId)) return null;
    if (!(MEDIA_FILE_NAMES as readonly string[]).includes(file)) return null;
    return join(this.objectDir(mediaId), file);
  }

  private objectDir(mediaId: string): string {
    return join(this.objectsDir, mediaId.slice(0, 2), mediaId);
  }

  // ---- Quotas --------------------------------------------------------------------------------

  private async checkQuotas(kind: MediaKind, ownerId: string | null, incomingBytes: number): Promise<void> {
    if (ownerId && PROFILE_KINDS.includes(kind)) {
      const [row] = await this.ctx.db
        .select({ uploads: count() })
        .from(mediaObjects)
        .where(
          and(
            eq(mediaObjects.ownerId, ownerId),
            inArray(mediaObjects.kind, PROFILE_KINDS),
            gt(mediaObjects.createdAt, new Date(Date.now() - 24 * 60 * 60 * 1000)),
          ),
        );
      if ((row?.uploads ?? 0) >= MEDIA_LIMITS.dailyUploadsPerUser) {
        throw new AppError(
          429,
          'MEDIA_QUOTA_EXCEEDED',
          `You can upload up to ${MEDIA_LIMITS.dailyUploadsPerUser} profile images per day. Please try again tomorrow.`,
        );
      }
    }
    const used = await this.usedBytes();
    if (used + incomingBytes > this.ctx.config.media.maxTotalBytes) {
      this.ctx.log.warn({ used, limit: this.ctx.config.media.maxTotalBytes }, 'Media storage limit reached');
      throw new AppError(507, 'MEDIA_STORAGE_FULL', 'The server has no room for more images right now. Please tell an administrator.');
    }
    try {
      const fs = await statfs(this.root);
      const free = Number(fs.bavail) * Number(fs.bsize);
      if (free - incomingBytes < this.ctx.config.media.minFreeBytes) {
        this.ctx.log.warn({ free }, 'Media volume is low on free space');
        throw new AppError(507, 'MEDIA_STORAGE_FULL', 'The server has no room for more images right now. Please tell an administrator.');
      }
    } catch (error) {
      if (error instanceof AppError) throw error;
      // statfs is unsupported on some filesystems; the byte limit above still applies.
    }
  }

  async usedBytes(executor: DbExecutor = this.ctx.db): Promise<number> {
    const [row] = await executor
      .select({ total: sql<string>`coalesce(sum(${mediaObjects.totalBytes}), 0)` })
      .from(mediaObjects)
      .where(isNull(mediaObjects.deletedAt));
    return Number(row?.total ?? 0);
  }

  // ---- Writing -------------------------------------------------------------------------------

  /**
   * Validates, re-encodes and stores an image. Returns the new media row. The caller attaches it
   * (profile, game) and soft-deletes whatever it replaces.
   */
  async store(kind: MediaKind, ownerId: string | null, data: Buffer): Promise<MediaRow> {
    this.requireAvailable();
    await this.checkQuotas(kind, ownerId, data.length * 2);
    const processed = await this.semaphore.run(() => processImage(kind, data));
    const id = randomBytes(16).toString('hex');
    const staging = join(this.tmpDir, id);
    const target = this.objectDir(id);
    await mkdir(staging, { recursive: true, mode: 0o750 });
    try {
      for (const variant of processed.variants) {
        await writeFile(join(staging, variant.file), variant.data, { mode: 0o640, flag: 'wx' });
      }
      await mkdir(join(this.objectsDir, id.slice(0, 2)), { recursive: true, mode: 0o750 });
      await rename(staging, target);
    } catch (error) {
      await rm(staging, { recursive: true, force: true });
      this.ctx.log.error({ err: error }, 'Could not write media files');
      throw new AppError(503, 'MEDIA_UNAVAILABLE', 'The image could not be saved. Please try again later.');
    }
    const variants = processed.variants.map((variant) => ({
      file: variant.file,
      width: variant.width,
      height: variant.height,
      bytes: variant.data.length,
    }));
    try {
      const [row] = await this.ctx.db
        .insert(mediaObjects)
        .values({
          id,
          kind,
          ownerId,
          width: processed.width,
          height: processed.height,
          variants,
          totalBytes: variants.reduce((sum, variant) => sum + variant.bytes, 0),
          sourceSha256: processed.sourceSha256,
        })
        .returning();
      if (!row) throw new Error('Media insert returned no row');
      return row;
    } catch (error) {
      await rm(target, { recursive: true, force: true });
      throw error;
    }
  }

  /**
   * Marks media as deleted inside the caller's transaction. Call removeFiles() with the same IDs
   * after the transaction committed.
   */
  async softDelete(ids: ReadonlyArray<string | null | undefined>, executor: DbExecutor = this.ctx.db): Promise<string[]> {
    const valid = ids.filter((id): id is string => typeof id === 'string' && MEDIA_ID_PATTERN.test(id));
    if (valid.length === 0) return [];
    await executor
      .update(mediaObjects)
      .set({ deletedAt: new Date() })
      .where(and(inArray(mediaObjects.id, valid), isNull(mediaObjects.deletedAt)));
    return valid;
  }

  /** Removes the files of media objects (idempotent; missing files are fine). */
  async removeFiles(ids: readonly string[]): Promise<void> {
    for (const id of ids) {
      if (!MEDIA_ID_PATTERN.test(id)) continue;
      try {
        await rm(this.objectDir(id), { recursive: true, force: true });
      } catch (error) {
        this.ctx.log.warn({ err: error, mediaId: id }, 'Could not remove media files; the orphan sweep will retry');
      }
    }
  }

  /** Deletes an unattached object immediately (for example when attaching it failed). */
  async discard(id: string): Promise<void> {
    await this.softDelete([id]);
    await this.removeFiles([id]);
  }

  // ---- Maintenance ---------------------------------------------------------------------------

  /** Purges old soft-deleted rows and removes orphaned files and stale staging directories. */
  async maintenance(now = Date.now()): Promise<{ purged: number; orphans: number }> {
    const purgedRows = await this.ctx.db
      .delete(mediaObjects)
      .where(and(isNotNull(mediaObjects.deletedAt), lt(mediaObjects.deletedAt, new Date(now - PURGE_AFTER_MS))))
      .returning({ id: mediaObjects.id });
    await this.removeFiles(purgedRows.map((row) => row.id));
    if (!this.availableFlag) return { purged: purgedRows.length, orphans: 0 };

    let orphans = 0;
    for (const entry of await safeReaddir(this.tmpDir)) {
      const path = join(this.tmpDir, entry);
      if (await olderThan(path, now - ORPHAN_AGE_MS)) {
        await rm(path, { recursive: true, force: true });
        orphans += 1;
      }
    }
    for (const prefix of await safeReaddir(this.objectsDir)) {
      if (!/^[0-9a-f]{2}$/.test(prefix)) continue;
      const ids = (await safeReaddir(join(this.objectsDir, prefix))).filter((id) => MEDIA_ID_PATTERN.test(id));
      if (ids.length === 0) continue;
      const live = await this.ctx.db
        .select({ id: mediaObjects.id })
        .from(mediaObjects)
        .where(and(inArray(mediaObjects.id, ids), isNull(mediaObjects.deletedAt)));
      const liveIds = new Set(live.map((row) => row.id));
      for (const id of ids) {
        if (liveIds.has(id)) continue;
        const path = this.objectDir(id);
        if (await olderThan(path, now - ORPHAN_AGE_MS)) {
          await rm(path, { recursive: true, force: true });
          orphans += 1;
        }
      }
    }
    if (orphans > 0) this.ctx.log.info({ orphans }, 'Removed orphaned media files');
    return { purged: purgedRows.length, orphans };
  }

  /** True when the variant file exists and is readable. */
  async exists(mediaId: string, file: string): Promise<boolean> {
    const path = this.filePath(mediaId, file);
    if (!path) return false;
    try {
      await access(path, fsConstants.R_OK);
      return true;
    } catch {
      return false;
    }
  }

  async row(mediaId: string): Promise<MediaRow | null> {
    if (!MEDIA_ID_PATTERN.test(mediaId)) return null;
    const [row] = await this.ctx.db.select().from(mediaObjects).where(eq(mediaObjects.id, mediaId)).limit(1);
    return row ?? null;
  }

  async stats(): Promise<{ objects: number; bytes: number; available: boolean; freeBytes: number | null }> {
    const [row] = await this.ctx.db
      .select({ objects: count(), bytes: sql<string>`coalesce(sum(${mediaObjects.totalBytes}), 0)` })
      .from(mediaObjects)
      .where(isNull(mediaObjects.deletedAt));
    let freeBytes: number | null;
    try {
      const fs = await statfs(this.root);
      freeBytes = Number(fs.bavail) * Number(fs.bsize);
    } catch {
      freeBytes = null;
    }
    return { objects: Number(row?.objects ?? 0), bytes: Number(row?.bytes ?? 0), available: this.availableFlag, freeBytes };
  }
}

async function safeReaddir(path: string): Promise<string[]> {
  try {
    return await readdir(path);
  } catch {
    return [];
  }
}

async function olderThan(path: string, cutoffMs: number): Promise<boolean> {
  try {
    return (await stat(path)).mtimeMs < cutoffMs;
  } catch {
    return false;
  }
}
