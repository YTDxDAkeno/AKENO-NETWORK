import { randomInt } from 'node:crypto';
import { and, desc, eq, lt, notInArray, or, sql } from 'drizzle-orm';
import {
  CONSOLE_ACCOUNT_STATES,
  DEVICE_CODE_PREFIX,
  DEVICE_LIMITS,
  DEVICE_SCOPES,
  DEVICE_TOKEN_PREFIX,
  REALTIME_CLOSE_CODES,
  REALTIME_PROTOCOL_VERSION,
  type ApproveDevicePairingInput,
  type ConsoleAccountState,
  type DeviceDto,
  type DevicePairingPreviewDto,
  type DevicePairingStartResponse,
  type DeviceScope,
  type DeviceSelfResponse,
  type DeviceTokenResponse,
  type LinkConsoleUserInput,
  type StartDevicePairingInput,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import type { DbExecutor } from '../../database/client.js';
import { deviceCredentials, devicePairings, profiles, users } from '../../database/schema/index.js';
import { randomToken, sha256Hex } from '../../utils/crypto.js';
import { isUniqueViolation } from '../../utils/db-errors.js';
import { AppError, Errors } from '../../utils/errors.js';
import type { AuthContextData, RequestMeta } from '../auth/service.js';

const DEVICE_TOKEN_PATTERN = new RegExp(`^${DEVICE_TOKEN_PREFIX}[A-Za-z0-9_-]{43}$`);
/** Same unambiguous alphabet as invitation codes (no 0/O, 1/I/L). */
const USER_CODE_ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';
const USER_CODE_LENGTH = 8;
/** Upper bound for unredeemed, unexpired pairing requests across the whole server. */
const MAX_OUTSTANDING_PAIRINGS = 500;
/** Polls may arrive slightly early because of network jitter. */
const POLL_TOLERANCE_MS = 1000;
/** Finished or expired pairing rows are kept this long for auditing, then deleted. */
const PAIRING_RETENTION_MS = 24 * 60 * 60 * 1000;
const TOUCH_INTERVAL_MS = 5 * 60 * 1000;

const CONSOLE_ACCOUNT_STATE_SET = new Set<string>(CONSOLE_ACCOUNT_STATES);

const SCOPE_ORDER = new Map<string, number>(DEVICE_SCOPES.map((scope, index) => [scope, index]));

function normalizeScopes(scopes: readonly string[]): DeviceScope[] {
  return [...new Set(scopes)]
    .filter((scope): scope is DeviceScope => SCOPE_ORDER.has(scope))
    .sort((a, b) => SCOPE_ORDER.get(a)! - SCOPE_ORDER.get(b)!);
}

function generateUserCode(): { display: string; normalized: string } {
  let normalized = '';
  for (let i = 0; i < USER_CODE_LENGTH; i += 1) normalized += USER_CODE_ALPHABET[randomInt(USER_CODE_ALPHABET.length)];
  return { display: `${normalized.slice(0, 4)}-${normalized.slice(4)}`, normalized };
}

const pairingInvalid = () =>
  new AppError(400, 'PAIRING_INVALID', 'This pairing request is invalid, already used or was cancelled. Start pairing again.');
const codeNotFound = () =>
  new AppError(404, 'PAIRING_INVALID', 'This code is invalid or has expired. Check the code on your device or start pairing again.');

type CredentialRow = typeof deviceCredentials.$inferSelect;

/**
 * Device pairing and device credentials.
 *
 * A device (for example a console homebrew client) cannot safely handle the user's password,
 * so it is paired instead: it requests a short user code, the user approves that code on the
 * website while signed in, and the device then redeems a one-time device code for an opaque,
 * scoped and revocable bearer token. Only SHA-256 hashes of all codes and tokens are stored.
 */
export class DevicesService {
  constructor(private readonly ctx: AppContext) {}

  async startPairing(input: StartDevicePairingInput, meta: RequestMeta): Promise<DevicePairingStartResponse> {
    const now = new Date();
    const [outstanding] = await this.ctx.db
      .select({ count: sql<number>`count(*)::int` })
      .from(devicePairings)
      .where(and(sql`${devicePairings.status} IN ('pending', 'approved')`, sql`${devicePairings.expiresAt} > ${now}`));
    if ((outstanding?.count ?? 0) >= MAX_OUTSTANDING_PAIRINGS) {
      throw Errors.rateLimited('Too many devices are waiting to be paired right now. Please try again in a few minutes.');
    }

    const requestedScopes = normalizeScopes(input.scopes ?? DEVICE_SCOPES);
    const expiresAt = new Date(now.getTime() + DEVICE_LIMITS.pairingTtlSeconds * 1000);
    for (let attempt = 0; ; attempt += 1) {
      const deviceCode = `${DEVICE_CODE_PREFIX}${randomToken(32)}`;
      const userCode = generateUserCode();
      try {
        await this.ctx.db.insert(devicePairings).values({
          deviceCodeHash: sha256Hex(deviceCode),
          userCodeHash: sha256Hex(userCode.normalized),
          deviceName: input.deviceName,
          platform: input.platform ?? null,
          clientVersion: input.clientVersion ?? null,
          requestedScopes,
          requestIp: meta.ip.slice(0, 64),
          expiresAt,
        });
        const verificationUri = `${this.ctx.config.publicOrigin}/pair`;
        return {
          deviceCode,
          userCode: userCode.display,
          verificationUri,
          verificationUriComplete: `${verificationUri}?code=${userCode.display}`,
          expiresIn: DEVICE_LIMITS.pairingTtlSeconds,
          interval: DEVICE_LIMITS.pairingPollIntervalSeconds,
        };
      } catch (error) {
        // A user-code collision is astronomically unlikely but harmless: draw a new code.
        if (attempt < 3 && isUniqueViolation(error)) continue;
        throw error;
      }
    }
  }

  private async findPendingByUserCode(executor: DbExecutor, normalizedCode: string, lock: boolean) {
    const query = executor
      .select()
      .from(devicePairings)
      .where(and(eq(devicePairings.userCodeHash, sha256Hex(normalizedCode)), eq(devicePairings.status, 'pending')))
      .limit(1);
    const [row] = lock ? await query.for('update') : await query;
    if (!row || row.expiresAt.getTime() <= Date.now()) throw codeNotFound();
    return row;
  }

  async previewPairing(normalizedCode: string): Promise<DevicePairingPreviewDto> {
    const row = await this.findPendingByUserCode(this.ctx.db, normalizedCode, false);
    return {
      deviceName: row.deviceName,
      platform: row.platform,
      clientVersion: row.clientVersion,
      requestedScopes: normalizeScopes(row.requestedScopes),
      createdAt: row.createdAt.toISOString(),
      expiresAt: row.expiresAt.toISOString(),
      requestIp: row.requestIp,
    };
  }

  async approvePairing(auth: AuthContextData, input: ApproveDevicePairingInput, meta: RequestMeta): Promise<void> {
    await this.ctx.db.transaction(async (tx) => {
      const row = await this.findPendingByUserCode(tx, input.userCode, true);
      const requested = new Set(row.requestedScopes);
      const granted = normalizeScopes(input.scopes ?? row.requestedScopes);
      if (granted.some((scope) => !requested.has(scope))) {
        throw Errors.validation('You can only grant permissions the device asked for.', {
          scopes: 'You can only grant permissions the device asked for.',
        });
      }
      await tx
        .update(devicePairings)
        .set({ status: 'approved', userId: auth.userId, grantedScopes: granted, decidedAt: new Date() })
        .where(eq(devicePairings.id, row.id));
      await this.ctx.audit.record(
        {
          actorId: auth.userId,
          action: 'device.pairing_approved',
          targetType: 'device_pairing',
          targetId: row.id,
          metadata: { deviceName: row.deviceName, platform: row.platform, scopes: granted },
          ipAddress: meta.ip,
        },
        tx,
      );
    });
  }

  async denyPairing(auth: AuthContextData, normalizedCode: string, meta: RequestMeta): Promise<void> {
    await this.ctx.db.transaction(async (tx) => {
      const row = await this.findPendingByUserCode(tx, normalizedCode, true);
      await tx
        .update(devicePairings)
        .set({ status: 'denied', userId: auth.userId, decidedAt: new Date() })
        .where(eq(devicePairings.id, row.id));
      await this.ctx.audit.record(
        {
          actorId: auth.userId,
          action: 'device.pairing_denied',
          targetType: 'device_pairing',
          targetId: row.id,
          metadata: { deviceName: row.deviceName },
          ipAddress: meta.ip,
        },
        tx,
      );
    });
  }

  /**
   * Called repeatedly by the device. Returns the bearer token exactly once, after the user has
   * approved the request; otherwise throws a typed error the device can act on.
   */
  async redeemPairing(deviceCode: string, meta: RequestMeta): Promise<DeviceTokenResponse> {
    // The transaction returns status errors instead of throwing them, so the poll timestamp
    // is committed and SLOW_DOWN works; only a lost redemption race rolls back.
    const outcome = await this.ctx.db.transaction(async (tx) => {
      const [row] = await tx
        .select()
        .from(devicePairings)
        .where(eq(devicePairings.deviceCodeHash, sha256Hex(deviceCode)))
        .for('update')
        .limit(1);
      if (!row || row.status === 'redeemed') return { error: pairingInvalid() } as const;

      const now = new Date();
      if (row.expiresAt.getTime() <= now.getTime()) {
        return {
          error: new AppError(400, 'EXPIRED_TOKEN', 'The pairing code expired before it was approved. Start pairing again.'),
        } as const;
      }
      const tooSoon =
        row.lastPolledAt !== null &&
        now.getTime() - row.lastPolledAt.getTime() < DEVICE_LIMITS.pairingPollIntervalSeconds * 1000 - POLL_TOLERANCE_MS;
      await tx.update(devicePairings).set({ lastPolledAt: now }).where(eq(devicePairings.id, row.id));
      if (row.status === 'denied') {
        return { error: new AppError(400, 'ACCESS_DENIED', 'The pairing request was declined on the website.') } as const;
      }
      if (tooSoon) {
        return {
          error: new AppError(400, 'SLOW_DOWN', `Poll at most every ${DEVICE_LIMITS.pairingPollIntervalSeconds} seconds.`),
        } as const;
      }
      if (row.status === 'pending' || !row.userId) {
        return {
          error: new AppError(400, 'AUTHORIZATION_PENDING', 'Waiting for the code to be approved on the website.'),
        } as const;
      }

      const [owner] = await tx.select({ status: users.status }).from(users).where(eq(users.id, row.userId)).limit(1);
      if (owner?.status !== 'active') return { error: pairingInvalid() } as const;

      const rawToken = `${DEVICE_TOKEN_PREFIX}${randomToken(32)}`;
      const expiresAt = new Date(now.getTime() + this.ctx.config.devices.ttlMs);
      const [credential] = await tx
        .insert(deviceCredentials)
        .values({
          userId: row.userId,
          tokenHash: sha256Hex(rawToken),
          name: row.deviceName,
          platform: row.platform,
          clientVersion: row.clientVersion,
          scopes: normalizeScopes(row.grantedScopes ?? []),
          expiresAt,
          ipAddress: meta.ip.slice(0, 64),
        })
        .returning();
      if (!credential) throw new Error('Device credential insert returned no row');

      // One-time redemption: the conditional update fails if another request redeemed it first.
      const redeemed = await tx
        .update(devicePairings)
        .set({ status: 'redeemed', redeemedAt: now, deviceId: credential.id })
        .where(and(eq(devicePairings.id, row.id), eq(devicePairings.status, 'approved')))
        .returning({ id: devicePairings.id });
      if (redeemed.length === 0) throw pairingInvalid();

      const unpaired = await this.enforceDeviceLimit(tx, row.userId);
      await this.ctx.audit.record(
        {
          actorId: row.userId,
          action: 'device.paired',
          targetType: 'device',
          targetId: credential.id,
          metadata: { deviceName: credential.name, platform: credential.platform, scopes: credential.scopes },
          ipAddress: meta.ip,
        },
        tx,
      );
      return { credential, rawToken, unpaired } as const;
    });

    if ('error' in outcome) throw outcome.error;
    for (const id of outcome.unpaired) {
      this.ctx.hub.disconnectSession(id, 'This device was unpaired.', REALTIME_CLOSE_CODES.sessionRevoked);
    }
    return {
      accessToken: outcome.rawToken,
      tokenType: 'Bearer',
      expiresAt: outcome.credential.expiresAt.toISOString(),
      device: this.toDto(outcome.credential, outcome.credential.id),
      user: await this.userSummary(outcome.credential.userId),
    };
  }

  /** Keeps at most DEVICE_LIMITS.maxPerUser devices; the least recently used ones are unpaired. */
  private async enforceDeviceLimit(tx: DbExecutor, userId: string): Promise<string[]> {
    const keep = tx
      .select({ id: deviceCredentials.id })
      .from(deviceCredentials)
      .where(eq(deviceCredentials.userId, userId))
      .orderBy(desc(deviceCredentials.lastUsedAt))
      .limit(DEVICE_LIMITS.maxPerUser);
    const removed = await tx
      .delete(deviceCredentials)
      .where(and(eq(deviceCredentials.userId, userId), notInArray(deviceCredentials.id, keep)))
      .returning({ id: deviceCredentials.id });
    return removed.map(({ id }) => id);
  }

  /** Resolves a bearer token. Returns null for unknown, malformed, expired or idle tokens. */
  async authenticate(rawToken: string): Promise<AuthContextData | null> {
    if (!DEVICE_TOKEN_PATTERN.test(rawToken)) return null;
    const [row] = await this.ctx.db
      .select({
        id: deviceCredentials.id,
        userId: deviceCredentials.userId,
        scopes: deviceCredentials.scopes,
        expiresAt: deviceCredentials.expiresAt,
        lastUsedAt: deviceCredentials.lastUsedAt,
        onlineId: users.onlineId,
        role: users.role,
        status: users.status,
      })
      .from(deviceCredentials)
      .innerJoin(users, eq(users.id, deviceCredentials.userId))
      .where(eq(deviceCredentials.tokenHash, sha256Hex(rawToken)))
      .limit(1);
    if (!row) return null;

    const now = Date.now();
    const idleExpired = row.lastUsedAt.getTime() + this.ctx.config.devices.idleTimeoutMs <= now;
    if (row.expiresAt.getTime() <= now || idleExpired) {
      await this.ctx.db.delete(deviceCredentials).where(eq(deviceCredentials.id, row.id));
      return null;
    }
    if (row.status !== 'active') return null;
    if (now - row.lastUsedAt.getTime() > TOUCH_INTERVAL_MS) await this.touch(row.id);

    return {
      credential: 'device',
      userId: row.userId,
      onlineId: row.onlineId,
      // Device tokens never carry administrator privileges.
      role: 'user',
      sessionId: row.id,
      sessionExpiresAt: row.expiresAt,
      csrfToken: '',
      scopes: new Set(normalizeScopes(row.scopes)),
    };
  }

  async touch(deviceId: string, ip?: string): Promise<void> {
    await this.ctx.db
      .update(deviceCredentials)
      .set({ lastUsedAt: new Date(), ...(ip ? { ipAddress: ip.slice(0, 64) } : {}) })
      .where(eq(deviceCredentials.id, deviceId));
  }

  /** True while the device credential exists, has not expired and its owner is active. */
  async isValid(deviceId: string): Promise<boolean> {
    const now = new Date();
    const [row] = await this.ctx.db
      .select({ id: deviceCredentials.id })
      .from(deviceCredentials)
      .innerJoin(users, eq(users.id, deviceCredentials.userId))
      .where(and(eq(deviceCredentials.id, deviceId), sql`${deviceCredentials.expiresAt} > ${now}`, eq(users.status, 'active')))
      .limit(1);
    return Boolean(row);
  }

  async describeSelf(auth: AuthContextData): Promise<DeviceSelfResponse> {
    const [row] = await this.ctx.db
      .select()
      .from(deviceCredentials)
      .where(eq(deviceCredentials.id, auth.sessionId))
      .limit(1);
    if (!row) throw Errors.unauthenticated();
    return {
      device: this.toDto(row, auth.sessionId),
      user: await this.userSummary(auth.userId),
      apiVersion: 1,
      realtimeProtocolVersion: REALTIME_PROTOCOL_VERSION,
      serverTime: new Date().toISOString(),
    };
  }

  async list(userId: string, currentDeviceId: string | null): Promise<DeviceDto[]> {
    const rows = await this.ctx.db
      .select()
      .from(deviceCredentials)
      .where(eq(deviceCredentials.userId, userId))
      .orderBy(desc(deviceCredentials.lastUsedAt));
    return rows.map((row) => this.toDto(row, currentDeviceId));
  }

  /**
   * Replaces the permissions of a paired device. Only its owner can do this, from the website;
   * a device can never change its own permissions. Removing a permission takes effect at once.
   */
  async updateScopes(userId: string, deviceId: string, scopes: readonly DeviceScope[], meta: RequestMeta): Promise<DeviceDto> {
    const next = normalizeScopes([...scopes]);
    const [before] = await this.ctx.db
      .select({ scopes: deviceCredentials.scopes })
      .from(deviceCredentials)
      .where(and(eq(deviceCredentials.id, deviceId), eq(deviceCredentials.userId, userId)))
      .limit(1);
    if (!before) throw Errors.notFound('The device');
    const [row] = await this.ctx.db
      .update(deviceCredentials)
      .set({ scopes: next })
      .where(and(eq(deviceCredentials.id, deviceId), eq(deviceCredentials.userId, userId)))
      .returning();
    if (!row) throw Errors.notFound('The device');
    const removed = normalizeScopes(before.scopes).filter((scope) => !next.includes(scope));
    if (removed.includes('activity:write')) await this.ctx.activity.endForDevice(userId, deviceId);
    // Live updates were opened with the old permissions; the device reconnects with the new ones.
    if (removed.length > 0) this.ctx.hub.disconnectSession(deviceId, 'The permissions of this device changed.', REALTIME_CLOSE_CODES.sessionRevoked);
    this.ctx.audit.recordInBackground({
      actorId: userId,
      action: 'device.scopes_changed',
      targetType: 'device',
      targetId: deviceId,
      metadata: { deviceName: row.name, scopes: next, removed },
      ipAddress: meta.ip,
    });
    return this.toDto(row, null);
  }

  /**
   * AKENO Account Bridge: a paired console records which local user is selected on it. Nothing
   * here is verified by anyone but the device token; the label is shown as local and unverified.
   * A different local user replaces the link; the same user keeps its link time and, when the
   * console sends no new check result, its previous one.
   */
  async linkConsoleUser(auth: AuthContextData, input: LinkConsoleUserInput, meta: RequestMeta): Promise<DeviceDto> {
    const now = new Date();
    const row = await this.ctx.db.transaction(async (tx) => {
      const [before] = await tx
        .select()
        .from(deviceCredentials)
        .where(and(eq(deviceCredentials.id, auth.sessionId), eq(deviceCredentials.userId, auth.userId)))
        .for('update')
        .limit(1);
      if (!before) throw Errors.unauthenticated();
      const sameUser = before.consoleUserId === input.localUserId;
      const accountState = input.accountState ?? (sameUser ? (before.consoleAccountState ?? 'not_checked') : 'not_checked');
      const accountCheckedAt =
        input.accountState === undefined
          ? sameUser
            ? before.consoleAccountCheckedAt
            : null
          : input.accountState === 'not_checked'
            ? null
            : now;
      const [updated] = await tx
        .update(deviceCredentials)
        .set({
          consoleUserId: input.localUserId,
          consoleUserName: input.userName,
          consoleUserLinkedAt: sameUser && before.consoleUserLinkedAt ? before.consoleUserLinkedAt : now,
          consoleAccountState: accountState,
          consoleAccountCheckedAt: accountCheckedAt,
        })
        .where(eq(deviceCredentials.id, before.id))
        .returning();
      if (!updated) throw Errors.unauthenticated();
      if (!sameUser) {
        await this.ctx.audit.record(
          {
            actorId: auth.userId,
            action: 'device.console_user_linked',
            targetType: 'device',
            targetId: before.id,
            metadata: { deviceName: before.name, consoleUserName: input.userName, replaced: before.consoleUserId !== null },
            ipAddress: meta.ip,
          },
          tx,
        );
      }
      return updated;
    });
    return this.toDto(row, auth.sessionId);
  }

  /** Removes the console-user link of one of the user's devices (from the website or the device itself). */
  async unlinkConsoleUser(userId: string, deviceId: string, meta: RequestMeta): Promise<DeviceDto> {
    const [row] = await this.ctx.db
      .update(deviceCredentials)
      .set({
        consoleUserId: null,
        consoleUserName: null,
        consoleUserLinkedAt: null,
        consoleAccountState: null,
        consoleAccountCheckedAt: null,
      })
      .where(and(eq(deviceCredentials.id, deviceId), eq(deviceCredentials.userId, userId)))
      .returning();
    if (!row) throw Errors.notFound('The device');
    this.ctx.audit.recordInBackground({
      actorId: userId,
      action: 'device.console_user_unlinked',
      targetType: 'device',
      targetId: deviceId,
      metadata: { deviceName: row.name },
      ipAddress: meta.ip,
    });
    return this.toDto(row, null);
  }

  async revoke(userId: string, deviceId: string, meta: RequestMeta, reason = 'This device was unpaired.'): Promise<void> {
    // A game reported by this device stops counting at its last heartbeat.
    await this.ctx.activity.endForDevice(userId, deviceId);
    const deleted = await this.ctx.db
      .delete(deviceCredentials)
      .where(and(eq(deviceCredentials.id, deviceId), eq(deviceCredentials.userId, userId)))
      .returning({ id: deviceCredentials.id, name: deviceCredentials.name });
    const [device] = deleted;
    if (!device) throw Errors.notFound('The device');
    this.ctx.hub.disconnectSession(deviceId, reason, REALTIME_CLOSE_CODES.sessionRevoked);
    this.ctx.audit.recordInBackground({
      actorId: userId,
      action: 'device.revoked',
      targetType: 'device',
      targetId: deviceId,
      metadata: { deviceName: device.name },
      ipAddress: meta.ip,
    });
  }

  /**
   * Unpairs every device of a user inside an existing transaction (password change, password
   * reset, suspension, account deletion). Callers disconnect live sockets after commit.
   */
  async revokeAllForUser(tx: DbExecutor, userId: string): Promise<number> {
    const deleted = await tx
      .delete(deviceCredentials)
      .where(eq(deviceCredentials.userId, userId))
      .returning({ id: deviceCredentials.id });
    return deleted.length;
  }

  async deleteExpired(): Promise<number> {
    const now = new Date();
    const idleCutoff = new Date(now.getTime() - this.ctx.config.devices.idleTimeoutMs);
    const credentials = await this.ctx.db
      .delete(deviceCredentials)
      .where(or(lt(deviceCredentials.expiresAt, now), lt(deviceCredentials.lastUsedAt, idleCutoff)))
      .returning({ id: deviceCredentials.id });
    await this.ctx.db
      .delete(devicePairings)
      .where(lt(devicePairings.expiresAt, new Date(now.getTime() - PAIRING_RETENTION_MS)));
    return credentials.length;
  }

  private toDto(row: CredentialRow, currentDeviceId: string | null): DeviceDto {
    return {
      id: row.id,
      name: row.name,
      platform: row.platform,
      clientVersion: row.clientVersion,
      scopes: normalizeScopes(row.scopes),
      createdAt: row.createdAt.toISOString(),
      lastUsedAt: row.lastUsedAt.toISOString(),
      expiresAt: row.expiresAt.toISOString(),
      ipAddress: row.ipAddress,
      current: row.id === currentDeviceId,
      consoleUser:
        row.consoleUserId !== null && row.consoleUserName !== null && row.consoleUserLinkedAt !== null
          ? {
              localUserId: row.consoleUserId,
              userName: row.consoleUserName,
              linkedAt: row.consoleUserLinkedAt.toISOString(),
              accountState: CONSOLE_ACCOUNT_STATE_SET.has(row.consoleAccountState ?? '')
                ? (row.consoleAccountState as ConsoleAccountState)
                : 'not_checked',
              accountCheckedAt: row.consoleAccountCheckedAt?.toISOString() ?? null,
            }
          : null,
    };
  }

  private async userSummary(userId: string): Promise<{ id: string; onlineId: string; displayName: string }> {
    const [row] = await this.ctx.db
      .select({ id: users.id, onlineId: users.onlineId, displayName: profiles.displayName })
      .from(users)
      .innerJoin(profiles, eq(profiles.userId, users.id))
      .where(eq(users.id, userId))
      .limit(1);
    if (!row) throw Errors.unauthenticated();
    return row;
  }
}
