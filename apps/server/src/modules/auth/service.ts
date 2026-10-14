import { and, desc, eq, lt, ne, notInArray, or, sql } from 'drizzle-orm';
import {
  type AuthSessionDto,
  type ChangePasswordInput,
  type DeviceScope,
  type LoginInput,
  RESERVED_ONLINE_IDS,
  checkPasswordQuality,
  type registerSchema,
} from '@akeno/shared';
import type { z } from 'zod';
import type { AppContext } from '../../app/context.js';
import type { DbExecutor } from '../../database/client.js';
import { authSessions, profiles, registrationInvitations, users } from '../../database/schema/index.js';
import { REALTIME_CLOSE_CODES } from '@akeno/shared';
import { hmacSha256, randomToken, safeEqual, sha256Hex } from '../../utils/crypto.js';
import { isUniqueViolation } from '../../utils/db-errors.js';
import { AppError, Errors } from '../../utils/errors.js';
import { LoginThrottle } from './login-throttle.js';

export interface RequestMeta {
  ip: string;
  userAgent: string | null;
}

/** Identity attached to an authenticated request or WebSocket connection. */
export interface AuthContextData {
  /** "session": browser cookie session. "device": paired-device bearer token. */
  credential: 'session' | 'device';
  userId: string;
  onlineId: string;
  role: 'user' | 'admin';
  /** Web session ID, or the device ID for paired devices. */
  sessionId: string;
  sessionExpiresAt: Date;
  /** Empty for devices: bearer tokens are not ambient credentials and need no CSRF token. */
  csrfToken: string;
  /** Granted device scopes; null for web sessions, which are not scope-limited. */
  scopes: ReadonlySet<DeviceScope> | null;
}

export interface IssuedSession {
  userId: string;
  sessionId: string;
  rawToken: string;
  expiresAt: Date;
}

type RegisterData = z.output<typeof registerSchema>;

const TOKEN_PATTERN = /^[A-Za-z0-9_-]{32,128}$/;
const CSRF_CONTEXT = 'akeno-csrf-v1';

export class AuthService {
  readonly throttle = new LoginThrottle();

  constructor(private readonly ctx: AppContext) {}

  /**
   * The CSRF token is an HMAC of the raw session token. Only someone who can read the HttpOnly
   * session cookie (i.e. the server) can derive it, so it cannot be forged by other origins,
   * and it needs no extra storage.
   */
  csrfTokenFor(rawToken: string): string {
    return hmacSha256(rawToken, CSRF_CONTEXT);
  }

  verifyCsrf(rawToken: string, provided: string | undefined): boolean {
    if (!provided) return false;
    return safeEqual(this.csrfTokenFor(rawToken), provided);
  }

  async register(input: RegisterData, meta: RequestMeta): Promise<IssuedSession> {
    const { config } = this.ctx;
    const normalized = input.onlineId.toLowerCase();
    if ((RESERVED_ONLINE_IDS as readonly string[]).includes(normalized)) {
      throw Errors.conflict('This Online ID is not available. Please choose another one.', 'ONLINE_ID_TAKEN');
    }
    const quality = checkPasswordQuality(input.password, input.onlineId);
    if (quality) throw Errors.validation(quality, { password: quality });

    const invitationCode = input.invitationCode && input.invitationCode.length > 0 ? input.invitationCode : null;
    if (config.registrationMode === 'invite' && !invitationCode) {
      throw new AppError(400, 'INVITATION_REQUIRED', 'An invitation code is required to join this network.', {
        invitationCode: 'An invitation code is required.',
      });
    }

    const passwordHash = await this.ctx.passwords.hash(input.password);
    try {
      return await this.ctx.db.transaction(async (tx) => {
        let invitationId: string | null = null;
        if (invitationCode) {
          invitationId = await this.redeemInvitation(tx, invitationCode);
        }
        const [user] = await tx
          .insert(users)
          .values({
            onlineId: input.onlineId,
            onlineIdNormalized: normalized,
            passwordHash,
            invitedByInvitationId: invitationId,
            passwordChangedAt: new Date(),
          })
          .returning({ id: users.id });
        if (!user) throw new Error('User insert returned no row');
        await tx.insert(profiles).values({
          userId: user.id,
          displayName: input.displayName && input.displayName.length > 0 ? input.displayName : input.onlineId,
        });
        const session = await this.createSession(tx, user.id, meta);
        await this.ctx.audit.record(
          {
            actorId: user.id,
            action: 'auth.registered',
            targetType: 'user',
            targetId: user.id,
            metadata: { viaInvitation: invitationId !== null, invitationId },
            ipAddress: meta.ip,
          },
          tx,
        );
        return session;
      });
    } catch (error) {
      if (isUniqueViolation(error, 'users_online_id_normalized_key')) {
        throw Errors.conflict('This Online ID is already taken. Please choose another one.', 'ONLINE_ID_TAKEN');
      }
      throw error;
    }
  }

  /** Locks and consumes one use of an invitation inside the registration transaction. */
  private async redeemInvitation(tx: DbExecutor, code: string): Promise<string> {
    const now = new Date();
    const [invitation] = await tx
      .select()
      .from(registrationInvitations)
      .where(eq(registrationInvitations.codeHash, sha256Hex(code)))
      .for('update')
      .limit(1);
    const invalid =
      !invitation ||
      invitation.revokedAt !== null ||
      (invitation.expiresAt !== null && invitation.expiresAt <= now) ||
      invitation.useCount >= invitation.maxUses;
    if (invalid) {
      throw new AppError(400, 'INVITATION_INVALID', 'This invitation code is invalid, used up or expired.', {
        invitationCode: 'This invitation code is invalid, used up or expired.',
      });
    }
    await tx
      .update(registrationInvitations)
      .set({ useCount: sql`${registrationInvitations.useCount} + 1` })
      .where(eq(registrationInvitations.id, invitation.id));
    return invitation.id;
  }

  async login(input: LoginInput, meta: RequestMeta): Promise<IssuedSession> {
    const normalized = input.onlineId.trim().toLowerCase();
    const retryAfter = this.throttle.retryAfterMs(normalized, meta.ip);
    if (retryAfter > 0) {
      const minutes = Math.max(1, Math.ceil(retryAfter / 60_000));
      throw Errors.rateLimited(
        `Too many failed sign-in attempts. Try again in ${minutes} minute${minutes === 1 ? '' : 's'}.`,
      );
    }

    const [user] = await this.ctx.db
      .select({ id: users.id, passwordHash: users.passwordHash, status: users.status })
      .from(users)
      .where(eq(users.onlineIdNormalized, normalized))
      .limit(1);

    const invalid = new AppError(401, 'INVALID_CREDENTIALS', 'The Online ID or password is incorrect.');
    if (!user || !user.passwordHash || user.status === 'deleted') {
      await this.ctx.passwords.verifyDummy(input.password);
      this.throttle.recordFailure(normalized, meta.ip);
      this.ctx.audit.recordInBackground({
        actorId: null,
        action: 'auth.login_failed',
        metadata: { reason: 'unknown_account' },
        ipAddress: meta.ip,
      });
      throw invalid;
    }

    const valid = await this.ctx.passwords.verify(user.passwordHash, input.password);
    if (!valid) {
      this.throttle.recordFailure(normalized, meta.ip);
      this.ctx.audit.recordInBackground({
        actorId: null,
        action: 'auth.login_failed',
        targetType: 'user',
        targetId: user.id,
        metadata: { reason: 'wrong_password' },
        ipAddress: meta.ip,
      });
      throw invalid;
    }
    // Only reveal suspension after the correct password, so it cannot be used for enumeration.
    if (user.status === 'suspended') {
      throw new AppError(403, 'ACCOUNT_SUSPENDED', 'This account has been suspended by an administrator.');
    }
    this.throttle.recordSuccess(normalized);

    if (this.ctx.passwords.needsRehash(user.passwordHash)) {
      const upgraded = await this.ctx.passwords.hash(input.password);
      await this.ctx.db.update(users).set({ passwordHash: upgraded }).where(eq(users.id, user.id));
    }

    const session = await this.ctx.db.transaction((tx) => this.createSession(tx, user.id, meta));
    this.ctx.audit.recordInBackground({
      actorId: user.id,
      action: 'auth.login',
      targetType: 'session',
      targetId: session.sessionId,
      ipAddress: meta.ip,
    });
    return session;
  }

  async createSession(tx: DbExecutor, userId: string, meta: RequestMeta): Promise<IssuedSession> {
    const rawToken = randomToken(32);
    const expiresAt = new Date(Date.now() + this.ctx.config.session.ttlMs);
    const [session] = await tx
      .insert(authSessions)
      .values({
        userId,
        tokenHash: sha256Hex(rawToken),
        expiresAt,
        userAgent: meta.userAgent ? meta.userAgent.slice(0, 256) : null,
        ipAddress: meta.ip.slice(0, 64),
      })
      .returning({ id: authSessions.id });
    if (!session) throw new Error('Session insert returned no row');

    // Keep at most N sessions per user; the least recently used ones are revoked.
    const keep = tx
      .select({ id: authSessions.id })
      .from(authSessions)
      .where(eq(authSessions.userId, userId))
      .orderBy(desc(authSessions.lastUsedAt))
      .limit(this.ctx.config.session.maxPerUser);
    await tx.delete(authSessions).where(and(eq(authSessions.userId, userId), notInArray(authSessions.id, keep)));

    return { userId, sessionId: session.id, rawToken, expiresAt };
  }

  /** Resolves a raw session token from the cookie. Returns null for unknown, expired or idle sessions. */
  async authenticate(rawToken: string | undefined): Promise<AuthContextData | null> {
    if (!rawToken || !TOKEN_PATTERN.test(rawToken)) return null;
    const tokenHash = sha256Hex(rawToken);
    const [row] = await this.ctx.db
      .select({
        sessionId: authSessions.id,
        userId: authSessions.userId,
        expiresAt: authSessions.expiresAt,
        lastUsedAt: authSessions.lastUsedAt,
        onlineId: users.onlineId,
        role: users.role,
        status: users.status,
      })
      .from(authSessions)
      .innerJoin(users, eq(users.id, authSessions.userId))
      .where(eq(authSessions.tokenHash, tokenHash))
      .limit(1);
    if (!row) return null;

    const now = Date.now();
    const idleExpired = row.lastUsedAt.getTime() + this.ctx.config.session.idleTimeoutMs <= now;
    if (row.expiresAt.getTime() <= now || idleExpired) {
      await this.ctx.db.delete(authSessions).where(eq(authSessions.id, row.sessionId));
      return null;
    }
    if (row.status !== 'active') return null;

    if (now - row.lastUsedAt.getTime() > this.ctx.config.session.touchIntervalMs) {
      await this.touchSession(row.sessionId);
    }
    return {
      credential: 'session',
      userId: row.userId,
      onlineId: row.onlineId,
      role: row.role,
      sessionId: row.sessionId,
      sessionExpiresAt: row.expiresAt,
      csrfToken: this.csrfTokenFor(rawToken),
      scopes: null,
    };
  }

  async touchSession(sessionId: string): Promise<void> {
    await this.ctx.db.update(authSessions).set({ lastUsedAt: new Date() }).where(eq(authSessions.id, sessionId));
  }

  /** True while the session still exists and has not expired (used by the realtime gateway). */
  async isSessionValid(sessionId: string): Promise<boolean> {
    const now = new Date();
    const [row] = await this.ctx.db
      .select({ id: authSessions.id })
      .from(authSessions)
      .innerJoin(users, eq(users.id, authSessions.userId))
      .where(and(eq(authSessions.id, sessionId), sql`${authSessions.expiresAt} > ${now}`, eq(users.status, 'active')))
      .limit(1);
    return Boolean(row);
  }

  async logout(auth: AuthContextData, meta: RequestMeta): Promise<void> {
    await this.ctx.db.delete(authSessions).where(eq(authSessions.id, auth.sessionId));
    this.ctx.hub.disconnectSession(auth.sessionId, 'You signed out.', REALTIME_CLOSE_CODES.sessionRevoked);
    this.ctx.voice.onSessionsRevoked(auth.userId, (id) => id === auth.sessionId);
    this.ctx.audit.recordInBackground({
      actorId: auth.userId,
      action: 'auth.logout',
      targetType: 'session',
      targetId: auth.sessionId,
      ipAddress: meta.ip,
    });
  }

  async listSessions(userId: string, currentSessionId: string): Promise<AuthSessionDto[]> {
    const rows = await this.ctx.db
      .select()
      .from(authSessions)
      .where(eq(authSessions.userId, userId))
      .orderBy(desc(authSessions.lastUsedAt));
    return rows.map((row) => ({
      id: row.id,
      createdAt: row.createdAt.toISOString(),
      lastUsedAt: row.lastUsedAt.toISOString(),
      expiresAt: row.expiresAt.toISOString(),
      userAgent: row.userAgent,
      ipAddress: row.ipAddress,
      current: row.id === currentSessionId,
    }));
  }

  async revokeSession(userId: string, sessionId: string, meta: RequestMeta): Promise<void> {
    const deleted = await this.ctx.db
      .delete(authSessions)
      .where(and(eq(authSessions.id, sessionId), eq(authSessions.userId, userId)))
      .returning({ id: authSessions.id });
    if (deleted.length === 0) throw Errors.notFound('The session');
    this.ctx.hub.disconnectSession(sessionId, 'This session was signed out.', REALTIME_CLOSE_CODES.sessionRevoked);
    this.ctx.voice.onSessionsRevoked(userId, (id) => id === sessionId);
    this.ctx.audit.recordInBackground({
      actorId: userId,
      action: 'auth.session_revoked',
      targetType: 'session',
      targetId: sessionId,
      ipAddress: meta.ip,
    });
  }

  async revokeOtherSessions(userId: string, currentSessionId: string, meta: RequestMeta): Promise<number> {
    const deleted = await this.ctx.db
      .delete(authSessions)
      .where(and(eq(authSessions.userId, userId), ne(authSessions.id, currentSessionId)))
      .returning({ id: authSessions.id });
    this.ctx.hub.disconnectUser(
      userId,
      'This session was signed out from another device.',
      REALTIME_CLOSE_CODES.sessionRevoked,
      currentSessionId,
    );
    this.ctx.voice.onSessionsRevoked(userId, (id) => id !== currentSessionId);
    this.ctx.audit.recordInBackground({
      actorId: userId,
      action: 'auth.other_sessions_revoked',
      metadata: { count: deleted.length },
      ipAddress: meta.ip,
    });
    return deleted.length;
  }

  async changePassword(auth: AuthContextData, input: ChangePasswordInput, meta: RequestMeta): Promise<void> {
    const [user] = await this.ctx.db
      .select({ passwordHash: users.passwordHash, onlineId: users.onlineId })
      .from(users)
      .where(eq(users.id, auth.userId))
      .limit(1);
    if (!user?.passwordHash) throw Errors.unauthenticated();
    const valid = await this.ctx.passwords.verify(user.passwordHash, input.currentPassword);
    if (!valid) {
      throw new AppError(400, 'INVALID_CREDENTIALS', 'The current password is incorrect.', {
        currentPassword: 'The current password is incorrect.',
      });
    }
    const quality = checkPasswordQuality(input.newPassword, user.onlineId);
    if (quality) throw Errors.validation(quality, { newPassword: quality });

    const passwordHash = await this.ctx.passwords.hash(input.newPassword);
    await this.ctx.db.transaction(async (tx) => {
      await tx
        .update(users)
        .set({ passwordHash, passwordChangedAt: new Date(), updatedAt: new Date() })
        .where(eq(users.id, auth.userId));
      // A password change signs out every other browser session and unpairs every device.
      await tx
        .delete(authSessions)
        .where(and(eq(authSessions.userId, auth.userId), ne(authSessions.id, auth.sessionId)));
      await this.ctx.devices.revokeAllForUser(tx, auth.userId);
      await this.ctx.audit.record(
        { actorId: auth.userId, action: 'auth.password_changed', targetType: 'user', targetId: auth.userId, ipAddress: meta.ip },
        tx,
      );
    });
    this.ctx.hub.disconnectUser(
      auth.userId,
      'Your password was changed. Please sign in again.',
      REALTIME_CLOSE_CODES.sessionRevoked,
      auth.sessionId,
    );
    this.ctx.voice.onSessionsRevoked(auth.userId, (id) => id !== auth.sessionId);
  }

  async deleteExpiredSessions(): Promise<number> {
    const now = new Date();
    const idleCutoff = new Date(now.getTime() - this.ctx.config.session.idleTimeoutMs);
    const deleted = await this.ctx.db
      .delete(authSessions)
      .where(or(lt(authSessions.expiresAt, now), lt(authSessions.lastUsedAt, idleCutoff)))
      .returning({ id: authSessions.id });
    return deleted.length;
  }
}
