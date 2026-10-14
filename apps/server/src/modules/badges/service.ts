import { and, asc, eq, inArray } from 'drizzle-orm';
import { BADGES, FEATURED_BADGES_MAX, badgeDefinition, type BadgeDto, type BadgeId, type UserBadgeAdminDto } from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import type { DbExecutor } from '../../database/client.js';
import { profiles, userBadges, users } from '../../database/schema/index.js';
import { AppError, Errors } from '../../utils/errors.js';

const BADGE_ORDER = new Map(BADGES.map((badge, index) => [badge.id, index]));

/**
 * Badges are recognitions granted by trusted server-side logic only:
 * - "administrator" is derived from the account role and never stored;
 * - every other badge is granted by an administrator (audited) or by the operator's CLI.
 * There is no endpoint through which users can give themselves a badge; they can only choose
 * which earned badges to feature.
 */
export class BadgesService {
  constructor(private readonly ctx: AppContext) {}

  /** Earned badges of many users, in catalogue order. */
  async earnedFor(userIds: string[], executor: DbExecutor = this.ctx.db): Promise<Map<string, BadgeDto[]>> {
    const unique = [...new Set(userIds)];
    const result = new Map<string, BadgeDto[]>(unique.map((id) => [id, []]));
    if (unique.length === 0) return result;
    const [granted, roles] = await Promise.all([
      executor.select().from(userBadges).where(inArray(userBadges.userId, unique)),
      executor.select({ id: users.id, role: users.role }).from(users).where(inArray(users.id, unique)),
    ]);
    for (const row of roles) if (row.role === 'admin') result.get(row.id)?.push({ id: 'administrator', grantedAt: null });
    for (const row of granted) {
      if (!badgeDefinition(row.badgeId)) continue;
      result.get(row.userId)?.push({ id: row.badgeId, grantedAt: row.grantedAt.toISOString() });
    }
    for (const list of result.values()) list.sort((a, b) => (BADGE_ORDER.get(a.id) ?? 99) - (BADGE_ORDER.get(b.id) ?? 99));
    return result;
  }

  async earned(userId: string, executor?: DbExecutor): Promise<BadgeDto[]> {
    return (await this.earnedFor([userId], executor)).get(userId) ?? [];
  }

  /** Featured badges that are still earned (a revoked badge silently drops out). */
  static featuredOf(featured: readonly string[], earned: readonly BadgeDto[]): BadgeId[] {
    const earnedIds = new Set(earned.map((badge) => badge.id));
    return featured.filter((id): id is BadgeId => earnedIds.has(id as BadgeId)).slice(0, FEATURED_BADGES_MAX);
  }

  /** Validates a featured-badge selection against what the user has earned. */
  async validateFeatured(userId: string, featured: readonly BadgeId[]): Promise<BadgeId[]> {
    const earned = new Set((await this.earned(userId)).map((badge) => badge.id));
    const missing = featured.filter((id) => !earned.has(id));
    if (missing.length > 0) {
      throw Errors.validation('You can only feature badges you have earned.', { featuredBadges: 'You can only feature badges you have earned.' });
    }
    return [...featured];
  }

  async listForUser(userId: string): Promise<UserBadgeAdminDto[]> {
    const rows = await this.ctx.db
      .select()
      .from(userBadges)
      .where(eq(userBadges.userId, userId))
      .orderBy(asc(userBadges.grantedAt));
    const grantors = await this.ctx.users.getSummaries(rows.flatMap((row) => (row.grantedById ? [row.grantedById] : [])));
    return rows.map((row) => ({
      badgeId: row.badgeId,
      note: row.note,
      grantedAt: row.grantedAt.toISOString(),
      grantedBy: row.grantedById ? (grantors.get(row.grantedById) ?? null) : null,
    }));
  }

  /**
   * Grants a badge. `actorId` null means the server operator (command line). Administrators may
   * not grant authority badges to their own account.
   */
  async grant(actorId: string | null, userId: string, badgeId: BadgeId, note: string, ipAddress: string | null): Promise<void> {
    const definition = badgeDefinition(badgeId);
    if (!definition || definition.derived) throw Errors.badRequest('This badge cannot be granted manually.');
    if (actorId && actorId === userId && definition.authority) {
      throw new AppError(403, 'FORBIDDEN', 'Authority badges must be granted by another administrator or the server operator.');
    }
    await this.ctx.users.requireActiveUser(userId);
    const inserted = await this.ctx.db
      .insert(userBadges)
      .values({ userId, badgeId, grantedById: actorId, note })
      .onConflictDoNothing()
      .returning({ userId: userBadges.userId });
    if (inserted.length === 0) throw Errors.conflict('This member already has that badge.', 'ALREADY_MEMBER');
    await this.ctx.audit.record({
      actorId,
      action: 'admin.badge_granted',
      targetType: 'user',
      targetId: userId,
      metadata: { badgeId, note, viaCli: actorId === null },
      ipAddress,
    });
    await this.changed(userId);
  }

  async revoke(actorId: string | null, userId: string, badgeId: BadgeId, ipAddress: string | null): Promise<void> {
    const deleted = await this.ctx.db
      .delete(userBadges)
      .where(and(eq(userBadges.userId, userId), eq(userBadges.badgeId, badgeId)))
      .returning({ userId: userBadges.userId });
    if (deleted.length === 0) throw Errors.notFound('The badge');
    // Drop it from the featured selection as well.
    const [profile] = await this.ctx.db.select({ featured: profiles.featuredBadges }).from(profiles).where(eq(profiles.userId, userId));
    if (profile?.featured.includes(badgeId)) {
      await this.ctx.db
        .update(profiles)
        .set({ featuredBadges: profile.featured.filter((id) => id !== badgeId) })
        .where(eq(profiles.userId, userId));
    }
    await this.ctx.audit.record({
      actorId,
      action: 'admin.badge_revoked',
      targetType: 'user',
      targetId: userId,
      metadata: { badgeId, viaCli: actorId === null },
      ipAddress,
    });
    await this.changed(userId);
  }

  private async changed(userId: string): Promise<void> {
    const friendIds = await this.ctx.relations.friendIds(userId);
    this.ctx.hub.sendToUsers([userId, ...friendIds], { type: 'profile.changed', data: { userId } });
  }
}
