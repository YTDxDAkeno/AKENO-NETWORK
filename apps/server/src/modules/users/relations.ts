import { and, eq, inArray, or } from 'drizzle-orm';
import type { AppContext } from '../../app/context.js';
import type { DbExecutor } from '../../database/client.js';
import { friendRequests, friendships, userBlocks } from '../../database/schema/index.js';
import { orderedPair } from '../../utils/pairs.js';

/** Read-only helpers for friendship and block relationships between users. */
export class RelationsService {
  constructor(private readonly ctx: AppContext) {}

  private exec(executor?: DbExecutor): DbExecutor {
    return executor ?? this.ctx.db;
  }

  async areFriends(a: string, b: string, executor?: DbExecutor): Promise<boolean> {
    if (a === b) return false;
    const [low, high] = orderedPair(a, b);
    const rows = await this.exec(executor)
      .select({ low: friendships.userLowId })
      .from(friendships)
      .where(and(eq(friendships.userLowId, low), eq(friendships.userHighId, high)))
      .limit(1);
    return rows.length > 0;
  }

  async hasBlocked(blockerId: string, blockedId: string, executor?: DbExecutor): Promise<boolean> {
    const rows = await this.exec(executor)
      .select({ blocker: userBlocks.blockerId })
      .from(userBlocks)
      .where(and(eq(userBlocks.blockerId, blockerId), eq(userBlocks.blockedId, blockedId)))
      .limit(1);
    return rows.length > 0;
  }

  async isBlockedEitherWay(a: string, b: string, executor?: DbExecutor): Promise<boolean> {
    const rows = await this.exec(executor)
      .select({ blocker: userBlocks.blockerId })
      .from(userBlocks)
      .where(
        or(
          and(eq(userBlocks.blockerId, a), eq(userBlocks.blockedId, b)),
          and(eq(userBlocks.blockerId, b), eq(userBlocks.blockedId, a)),
        ),
      )
      .limit(1);
    return rows.length > 0;
  }

  async friendIds(userId: string, executor?: DbExecutor): Promise<string[]> {
    const rows = await this.exec(executor)
      .select({ low: friendships.userLowId, high: friendships.userHighId })
      .from(friendships)
      .where(or(eq(friendships.userLowId, userId), eq(friendships.userHighId, userId)));
    return rows.map((row) => (row.low === userId ? row.high : row.low));
  }

  /** Which of the target users are friends of the viewer. */
  async friendSet(viewerId: string, targetIds: string[], executor?: DbExecutor): Promise<Set<string>> {
    if (targetIds.length === 0) return new Set();
    const rows = await this.exec(executor)
      .select({ low: friendships.userLowId, high: friendships.userHighId })
      .from(friendships)
      .where(
        or(
          and(eq(friendships.userLowId, viewerId), inArray(friendships.userHighId, targetIds)),
          and(eq(friendships.userHighId, viewerId), inArray(friendships.userLowId, targetIds)),
        ),
      );
    return new Set(rows.map((row) => (row.low === viewerId ? row.high : row.low)));
  }

  /** Which of the target users have a block relationship (in either direction) with the viewer. */
  async blockedSet(viewerId: string, targetIds: string[], executor?: DbExecutor): Promise<Set<string>> {
    if (targetIds.length === 0) return new Set();
    const rows = await this.exec(executor)
      .select({ blocker: userBlocks.blockerId, blocked: userBlocks.blockedId })
      .from(userBlocks)
      .where(
        or(
          and(eq(userBlocks.blockerId, viewerId), inArray(userBlocks.blockedId, targetIds)),
          and(eq(userBlocks.blockedId, viewerId), inArray(userBlocks.blockerId, targetIds)),
        ),
      );
    return new Set(rows.map((row) => (row.blocker === viewerId ? row.blocked : row.blocker)));
  }

  /** Users who blocked the viewer (used to hide the viewer from their searches and vice versa). */
  async blockedByOrBlocking(viewerId: string, executor?: DbExecutor): Promise<Set<string>> {
    const rows = await this.exec(executor)
      .select({ blocker: userBlocks.blockerId, blocked: userBlocks.blockedId })
      .from(userBlocks)
      .where(or(eq(userBlocks.blockerId, viewerId), eq(userBlocks.blockedId, viewerId)));
    return new Set(rows.map((row) => (row.blocker === viewerId ? row.blocked : row.blocker)));
  }

  async pendingRequestBetween(
    a: string,
    b: string,
    executor?: DbExecutor,
  ): Promise<{ id: string; senderId: string; recipientId: string } | null> {
    const [row] = await this.exec(executor)
      .select({ id: friendRequests.id, senderId: friendRequests.senderId, recipientId: friendRequests.recipientId })
      .from(friendRequests)
      .where(
        and(
          eq(friendRequests.status, 'pending'),
          or(
            and(eq(friendRequests.senderId, a), eq(friendRequests.recipientId, b)),
            and(eq(friendRequests.senderId, b), eq(friendRequests.recipientId, a)),
          ),
        ),
      )
      .limit(1);
    return row ?? null;
  }
}
