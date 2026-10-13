import { and, asc, eq, ilike, inArray, ne, or, sql } from 'drizzle-orm';
import type {
  CurrentUser,
  PresencePreference,
  PresenceVisibility,
  PublicProfile,
  Relationship,
  RelationshipState,
  UpdatePrivacyInput,
  UpdateProfileInput,
  UserSearchResult,
  UserSummary,
  UserWithPresence,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import type { DbExecutor } from '../../database/client.js';
import { friendRequests, profiles, users } from '../../database/schema/index.js';
import { computePresenceView, effectiveSelfStatus } from '../../realtime/presence-rules.js';
import { Errors } from '../../utils/errors.js';
import { toIso } from '../../utils/time.js';

/** Columns needed to render a user card with presence. */
export const userCardColumns = {
  id: users.id,
  onlineId: users.onlineId,
  status: users.status,
  role: users.role,
  createdAt: users.createdAt,
  lastSeenAt: users.lastSeenAt,
  displayName: profiles.displayName,
  avatar: profiles.avatar,
  statusMessage: profiles.statusMessage,
  activity: profiles.activity,
  presencePreference: profiles.presencePreference,
  presenceVisibility: profiles.presenceVisibility,
  lastSeenVisibility: profiles.lastSeenVisibility,
};

export interface UserCardRow {
  id: string;
  onlineId: string;
  status: 'active' | 'suspended' | 'deleted';
  role: 'user' | 'admin';
  createdAt: Date;
  lastSeenAt: Date | null;
  displayName: string;
  avatar: string;
  statusMessage: string;
  activity: string | null;
  presencePreference: PresencePreference;
  presenceVisibility: PresenceVisibility;
  lastSeenVisibility: PresenceVisibility;
}

export function toUserSummary(row: Pick<UserCardRow, 'id' | 'onlineId' | 'displayName' | 'avatar' | 'status'>): UserSummary {
  if (row.status === 'deleted') {
    return { id: row.id, onlineId: 'deleted-user', displayName: 'Deleted user', avatar: 'bot', deleted: true };
  }
  return { id: row.id, onlineId: row.onlineId, displayName: row.displayName, avatar: row.avatar, deleted: false };
}

function escapeLike(value: string): string {
  return value.replace(/[\\%_]/g, (match) => `\\${match}`);
}

export class UsersService {
  constructor(private readonly ctx: AppContext) {}

  async loadCards(ids: string[], executor: DbExecutor = this.ctx.db): Promise<Map<string, UserCardRow>> {
    const unique = [...new Set(ids)];
    if (unique.length === 0) return new Map();
    const rows = await executor
      .select(userCardColumns)
      .from(users)
      .innerJoin(profiles, eq(profiles.userId, users.id))
      .where(inArray(users.id, unique));
    return new Map(rows.map((row) => [row.id, row]));
  }

  async getSummaries(ids: string[], executor?: DbExecutor): Promise<Map<string, UserSummary>> {
    const cards = await this.loadCards(ids, executor);
    return new Map([...cards].map(([id, row]) => [id, toUserSummary(row)]));
  }

  async getSummary(id: string, executor?: DbExecutor): Promise<UserSummary> {
    const summary = (await this.getSummaries([id], executor)).get(id);
    if (!summary) throw Errors.notFound('The user');
    return summary;
  }

  /** User cards with presence computed for the given viewer, respecting privacy and blocks. */
  async getWithPresence(viewerId: string, ids: string[]): Promise<Map<string, UserWithPresence>> {
    const cards = await this.loadCards(ids);
    const targetIds = [...cards.keys()];
    const [friends, blocked] = await Promise.all([
      this.ctx.relations.friendSet(viewerId, targetIds),
      this.ctx.relations.blockedSet(viewerId, targetIds),
    ]);
    const result = new Map<string, UserWithPresence>();
    for (const [id, row] of cards) {
      result.set(id, this.withPresence(row, viewerId, friends.has(id), blocked.has(id)));
    }
    return result;
  }

  withPresence(row: UserCardRow, viewerId: string, isFriend: boolean, blocked: boolean): UserWithPresence {
    const presence = computePresenceView(this.ctx.presence.subjectFor(row), {
      isSelf: row.id === viewerId,
      isFriend,
      blocked,
    });
    return { ...toUserSummary(row), presence };
  }

  /** Loads an active user by ID or throws NOT_FOUND. */
  async requireActiveUser(id: string, executor?: DbExecutor): Promise<UserCardRow> {
    const row = (await this.loadCards([id], executor)).get(id);
    if (!row || row.status !== 'active') throw Errors.notFound('The user');
    return row;
  }

  async getCurrentUser(userId: string): Promise<CurrentUser> {
    const [row] = await this.ctx.db
      .select({
        id: users.id,
        onlineId: users.onlineId,
        role: users.role,
        status: users.status,
        createdAt: users.createdAt,
        profile: profiles,
      })
      .from(users)
      .innerJoin(profiles, eq(profiles.userId, users.id))
      .where(eq(users.id, userId))
      .limit(1);
    if (!row) throw Errors.unauthenticated();
    const p = row.profile;
    return {
      id: row.id,
      onlineId: row.onlineId,
      role: row.role,
      status: row.status,
      createdAt: row.createdAt.toISOString(),
      profile: {
        displayName: p.displayName,
        avatar: p.avatar,
        bio: p.bio,
        statusMessage: p.statusMessage,
        favoriteGames: p.favoriteGames,
        activity: p.activity,
        activityUpdatedAt: toIso(p.activityUpdatedAt),
      },
      privacy: {
        profileVisibility: p.profileVisibility,
        presenceVisibility: p.presenceVisibility,
        lastSeenVisibility: p.lastSeenVisibility,
        messagePrivacy: p.messagePrivacy,
        friendRequestPrivacy: p.friendRequestPrivacy,
      },
      presencePreference: p.presencePreference,
      // The request itself proves the user is active, so they count as connected here.
      presence: effectiveSelfStatus({
        connected: true,
        allIdle: this.ctx.presence.allIdle(row.id),
        preference: p.presencePreference,
      }),
    };
  }

  async updateProfile(userId: string, input: UpdateProfileInput & Record<string, unknown>): Promise<CurrentUser> {
    const now = new Date();
    const patch: Partial<typeof profiles.$inferInsert> = { updatedAt: now };
    if (input.displayName !== undefined) patch.displayName = input.displayName;
    if (input.avatar !== undefined) patch.avatar = input.avatar;
    if (input.bio !== undefined) patch.bio = input.bio;
    if (input.statusMessage !== undefined) patch.statusMessage = input.statusMessage;
    if (input.favoriteGames !== undefined) {
      // Remove case-insensitive duplicates while keeping the user's order.
      const seen = new Set<string>();
      patch.favoriteGames = input.favoriteGames.filter((game) => {
        const key = game.toLowerCase();
        if (seen.has(key)) return false;
        seen.add(key);
        return true;
      });
    }
    if (input.activity !== undefined) {
      patch.activity = input.activity ? input.activity : null;
      patch.activityUpdatedAt = input.activity ? now : null;
    }
    await this.ctx.db.update(profiles).set(patch).where(eq(profiles.userId, userId));
    await this.afterProfileChange(userId);
    return this.getCurrentUser(userId);
  }

  async updatePrivacy(userId: string, input: UpdatePrivacyInput): Promise<CurrentUser> {
    await this.ctx.db
      .update(profiles)
      .set({ ...input, updatedAt: new Date() })
      .where(eq(profiles.userId, userId));
    this.ctx.presence.invalidate(userId);
    await this.afterProfileChange(userId);
    return this.getCurrentUser(userId);
  }

  private async afterProfileChange(userId: string): Promise<void> {
    await this.ctx.presence.publish(userId);
    const friendIds = await this.ctx.relations.friendIds(userId);
    this.ctx.hub.sendToUsers([userId, ...friendIds], { type: 'profile.changed', data: { userId } });
  }

  async search(viewerId: string, query: string, limit: number): Promise<UserSearchResult[]> {
    const q = query.trim().toLowerCase();
    const pattern = `%${escapeLike(q)}%`;
    const prefix = `${escapeLike(q)}%`;
    const excluded = await this.ctx.relations.blockedByOrBlocking(viewerId);
    const rows = await this.ctx.db
      .select(userCardColumns)
      .from(users)
      .innerJoin(profiles, eq(profiles.userId, users.id))
      .where(
        and(
          eq(users.status, 'active'),
          ne(users.id, viewerId),
          or(ilike(users.onlineIdNormalized, pattern), ilike(profiles.displayName, pattern)),
        ),
      )
      .orderBy(
        sql`CASE WHEN ${users.onlineIdNormalized} = ${q} THEN 0 WHEN ${users.onlineIdNormalized} LIKE ${prefix} THEN 1 ELSE 2 END`,
        asc(users.onlineIdNormalized),
      )
      .limit(limit + excluded.size);
    const visible = rows.filter((row) => !excluded.has(row.id)).slice(0, limit);
    const states = await this.relationshipStates(
      viewerId,
      visible.map((row) => row.id),
    );
    return visible.map((row) => ({ ...toUserSummary(row), relationship: states.get(row.id) ?? 'none' }));
  }

  /** Accepts either a UUID or an Online ID (online IDs can never look like a UUID). */
  async resolveHandle(handle: string): Promise<string | null> {
    const isUuid = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(handle);
    const [row] = await this.ctx.db
      .select({ id: users.id })
      .from(users)
      .where(isUuid ? eq(users.id, handle.toLowerCase()) : eq(users.onlineIdNormalized, handle.toLowerCase()))
      .limit(1);
    return row?.id ?? null;
  }

  async getPublicProfile(viewerId: string, handle: string): Promise<PublicProfile> {
    const targetId = await this.resolveHandle(handle);
    if (!targetId) throw Errors.notFound('The user');
    const [row] = await this.ctx.db
      .select({ card: userCardColumns, profile: profiles })
      .from(users)
      .innerJoin(profiles, eq(profiles.userId, users.id))
      .where(eq(users.id, targetId))
      .limit(1);
    if (!row || row.card.status !== 'active') throw Errors.notFound('The user');

    const isSelf = targetId === viewerId;
    const [iBlocked, theyBlocked] = isSelf
      ? [false, false]
      : await Promise.all([
          this.ctx.relations.hasBlocked(viewerId, targetId),
          this.ctx.relations.hasBlocked(targetId, viewerId),
        ]);
    // Users who blocked the viewer are indistinguishable from users that do not exist.
    if (theyBlocked) throw Errors.notFound('The user');

    const isFriend = !isSelf && (await this.ctx.relations.areFriends(viewerId, targetId));
    const relationship = await this.relationshipFor(viewerId, targetId, {
      isFriend,
      iBlocked,
      messagePrivacy: row.profile.messagePrivacy,
      friendRequestPrivacy: row.profile.friendRequestPrivacy,
    });
    const restricted = !isSelf && (iBlocked || (row.profile.profileVisibility === 'friends' && !isFriend));
    const mutualFriendCount = isSelf || iBlocked ? 0 : await this.mutualFriendCount(viewerId, targetId);

    return {
      user: toUserSummary(row.card),
      presence: computePresenceView(this.ctx.presence.subjectFor(row.card), {
        isSelf,
        isFriend,
        blocked: iBlocked,
      }),
      joinedAt: row.card.createdAt.toISOString(),
      restricted,
      bio: restricted ? null : row.profile.bio,
      statusMessage: restricted ? null : row.profile.statusMessage,
      favoriteGames: restricted ? [] : row.profile.favoriteGames,
      relationship,
      mutualFriendCount,
    };
  }

  private async relationshipFor(
    viewerId: string,
    targetId: string,
    context: {
      isFriend: boolean;
      iBlocked: boolean;
      messagePrivacy: 'everyone' | 'friends';
      friendRequestPrivacy: 'everyone' | 'nobody';
    },
  ): Promise<Relationship> {
    if (viewerId === targetId) {
      return { state: 'self', friendRequestId: null, canMessage: false, canSendFriendRequest: false };
    }
    if (context.iBlocked) {
      return { state: 'blocked', friendRequestId: null, canMessage: false, canSendFriendRequest: false };
    }
    const canMessage = context.messagePrivacy === 'everyone' || context.isFriend;
    if (context.isFriend) {
      return { state: 'friend', friendRequestId: null, canMessage, canSendFriendRequest: false };
    }
    const pending = await this.ctx.relations.pendingRequestBetween(viewerId, targetId);
    if (pending) {
      return {
        state: pending.senderId === viewerId ? 'request_sent' : 'request_received',
        friendRequestId: pending.id,
        canMessage,
        canSendFriendRequest: false,
      };
    }
    return {
      state: 'none',
      friendRequestId: null,
      canMessage,
      canSendFriendRequest: context.friendRequestPrivacy === 'everyone',
    };
  }

  private async relationshipStates(viewerId: string, targetIds: string[]): Promise<Map<string, RelationshipState>> {
    const result = new Map<string, RelationshipState>();
    if (targetIds.length === 0) return result;
    const friends = await this.ctx.relations.friendSet(viewerId, targetIds);
    for (const id of targetIds) result.set(id, friends.has(id) ? 'friend' : 'none');
    const pending = await this.ctx.db
      .select({ senderId: friendRequests.senderId, recipientId: friendRequests.recipientId })
      .from(friendRequests)
      .where(
        and(
          eq(friendRequests.status, 'pending'),
          or(
            and(eq(friendRequests.senderId, viewerId), inArray(friendRequests.recipientId, targetIds)),
            and(eq(friendRequests.recipientId, viewerId), inArray(friendRequests.senderId, targetIds)),
          ),
        ),
      );
    for (const row of pending) {
      if (row.senderId === viewerId) result.set(row.recipientId, 'request_sent');
      else result.set(row.senderId, 'request_received');
    }
    return result;
  }

  private async mutualFriendCount(a: string, b: string): Promise<number> {
    const [mine, theirs] = await Promise.all([this.ctx.relations.friendIds(a), this.ctx.relations.friendIds(b)]);
    const set = new Set(mine);
    return theirs.filter((id) => set.has(id)).length;
  }
}
