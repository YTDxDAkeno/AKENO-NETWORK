import { eq, or } from 'drizzle-orm';
import { REALTIME_CLOSE_CODES, type DeleteAccountInput } from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import type { DbExecutor } from '../../database/client.js';
import {
  authSessions,
  conversationReadStates,
  friendRequests,
  friendships,
  notifications,
  profiles,
  userBlocks,
  users,
} from '../../database/schema/index.js';
import { isUniqueViolation } from '../../utils/db-errors.js';
import { Effects } from '../../utils/effects.js';
import { AppError, Errors } from '../../utils/errors.js';
import type { AuthContextData, RequestMeta } from '../auth/service.js';

export class AccountService {
  constructor(private readonly ctx: AppContext) {}

  /**
   * Creates an account directly (CLI bootstrap and development seeding). Public registration
   * goes through AuthService.register, which additionally enforces invitations and reserved IDs.
   */
  async createAccount(input: {
    onlineId: string;
    password: string;
    role: 'user' | 'admin';
    displayName?: string;
  }): Promise<{ id: string }> {
    const passwordHash = await this.ctx.passwords.hash(input.password);
    try {
      return await this.ctx.db.transaction(async (tx) => {
        const [user] = await tx
          .insert(users)
          .values({
            onlineId: input.onlineId,
            onlineIdNormalized: input.onlineId.toLowerCase(),
            passwordHash,
            role: input.role,
            passwordChangedAt: new Date(),
          })
          .returning({ id: users.id });
        if (!user) throw new Error('User insert returned no row');
        await tx.insert(profiles).values({ userId: user.id, displayName: input.displayName ?? input.onlineId });
        await this.ctx.audit.record(
          { actorId: null, action: 'account.created_by_operator', targetType: 'user', targetId: user.id, metadata: { role: input.role } },
          tx,
        );
        return user;
      });
    } catch (error) {
      if (isUniqueViolation(error, 'users_online_id_normalized_key')) {
        throw Errors.conflict('This Online ID is already taken.', 'ONLINE_ID_TAKEN');
      }
      throw error;
    }
  }

  /**
   * Deletes an account with safe data handling:
   * - Profile, relationships, blocks, notifications, read states and sessions are removed.
   * - Memberships are removed; owned groups and led parties are handed to another member.
   * - The user row is kept as an anonymous tombstone so that messages already delivered to
   *   other people keep a valid author ("Deleted user"). The Online ID is released.
   */
  async deleteAccount(auth: AuthContextData, input: DeleteAccountInput, meta: RequestMeta): Promise<void> {
    const [user] = await this.ctx.db
      .select({ passwordHash: users.passwordHash })
      .from(users)
      .where(eq(users.id, auth.userId))
      .limit(1);
    if (!user?.passwordHash) throw Errors.unauthenticated();
    if (!(await this.ctx.passwords.verify(user.passwordHash, input.password))) {
      throw new AppError(400, 'INVALID_CREDENTIALS', 'The password is incorrect.', { password: 'The password is incorrect.' });
    }

    const userId = auth.userId;
    const formerFriends = await this.ctx.relations.friendIds(userId);
    const effects = new Effects();
    await this.ctx.db.transaction(async (tx) => {
      await this.removeFootprint(tx, userId, effects);
      const now = new Date();
      await tx
        .update(users)
        .set({
          status: 'deleted',
          role: 'user',
          onlineId: `deleted-${userId.slice(0, 8)}`,
          onlineIdNormalized: `~deleted~${userId}`,
          passwordHash: null,
          lastSeenAt: null,
          deletedAt: now,
          updatedAt: now,
        })
        .where(eq(users.id, userId));
      await tx
        .update(profiles)
        .set({
          displayName: 'Deleted user',
          avatar: 'bot',
          bio: '',
          statusMessage: '',
          favoriteGames: [],
          activity: null,
          activityUpdatedAt: null,
          presencePreference: 'invisible',
          updatedAt: now,
        })
        .where(eq(profiles.userId, userId));
      await this.ctx.audit.record(
        { actorId: userId, action: 'account.deleted', targetType: 'user', targetId: userId, ipAddress: meta.ip },
        tx,
      );
    });
    await effects.run((error) => this.ctx.log.error({ err: error }, 'Account deletion side effect failed'));
    this.ctx.hub.disconnectUser(userId, 'This account was deleted.', REALTIME_CLOSE_CODES.sessionRevoked);
    this.ctx.hub.sendToUsers(formerFriends, { type: 'friends.changed', data: { reason: 'friend_removed', userId } });
  }

  private async removeFootprint(tx: DbExecutor, userId: string, effects: Effects): Promise<void> {
    await this.ctx.groups.removeUserEverywhere(tx, userId, effects);
    await this.ctx.parties.leaveCurrentInTx(tx, userId, effects);
    await this.ctx.gameSessions.removeUserEverywhere(tx, userId, effects);
    await this.ctx.invitations.cancelAllForUser(tx, userId);
    await tx.delete(friendships).where(or(eq(friendships.userLowId, userId), eq(friendships.userHighId, userId)));
    await tx.delete(friendRequests).where(or(eq(friendRequests.senderId, userId), eq(friendRequests.recipientId, userId)));
    await tx.delete(userBlocks).where(or(eq(userBlocks.blockerId, userId), eq(userBlocks.blockedId, userId)));
    await tx.delete(notifications).where(eq(notifications.userId, userId));
    await tx.delete(conversationReadStates).where(eq(conversationReadStates.userId, userId));
    await tx.delete(authSessions).where(eq(authSessions.userId, userId));
  }
}
