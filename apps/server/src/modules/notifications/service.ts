import { and, desc, eq, inArray, isNull, lt, sql } from 'drizzle-orm';
import type { MarkNotificationsReadInput, NotificationData, NotificationDto, NotificationPage } from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import type { DbExecutor } from '../../database/client.js';
import { notifications } from '../../database/schema/index.js';
import type { Effects } from '../../utils/effects.js';
import { toIso } from '../../utils/time.js';

type NotificationRow = typeof notifications.$inferSelect;

function toDto(row: NotificationRow): NotificationDto {
  return { id: row.id, data: row.data, createdAt: row.createdAt.toISOString(), readAt: toIso(row.readAt) };
}

/** Durable notifications, pushed in realtime after the surrounding transaction commits. */
export class NotificationsService {
  constructor(private readonly ctx: AppContext) {}

  async create(userId: string, data: NotificationData, executor: DbExecutor, effects: Effects): Promise<void> {
    const [row] = await executor.insert(notifications).values({ userId, type: data.type, data }).returning();
    if (!row) return;
    const notification = toDto(row);
    effects.add(() => this.ctx.hub.sendToUser(userId, { type: 'notification.created', data: { notification } }));
  }

  async list(userId: string, before: number | undefined, limit: number): Promise<NotificationPage> {
    const rows = await this.ctx.db
      .select()
      .from(notifications)
      .where(and(eq(notifications.userId, userId), before ? lt(notifications.id, before) : undefined))
      .orderBy(desc(notifications.id))
      .limit(limit + 1);
    const [{ count } = { count: 0 }] = await this.ctx.db
      .select({ count: sql<number>`count(*)::int` })
      .from(notifications)
      .where(and(eq(notifications.userId, userId), isNull(notifications.readAt)));
    return {
      notifications: rows.slice(0, limit).map(toDto),
      unreadCount: count,
      hasMore: rows.length > limit,
    };
  }

  async markRead(userId: string, input: MarkNotificationsReadInput): Promise<void> {
    await this.ctx.db
      .update(notifications)
      .set({ readAt: new Date() })
      .where(
        and(
          eq(notifications.userId, userId),
          isNull(notifications.readAt),
          'ids' in input ? inArray(notifications.id, input.ids) : undefined,
        ),
      );
  }

  /** Removes notifications older than 90 days. */
  async prune(): Promise<void> {
    const cutoff = new Date(Date.now() - 90 * 24 * 60 * 60 * 1000);
    await this.ctx.db.delete(notifications).where(lt(notifications.createdAt, cutoff));
  }
}
