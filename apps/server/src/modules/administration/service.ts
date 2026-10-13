import { and, desc, eq, ilike, lt, or, sql } from 'drizzle-orm';
import {
  REALTIME_CLOSE_CODES,
  type AdminStatsDto,
  type AdminUpdateUserInput,
  type AdminUserDto,
  type AuditEventDto,
  type CreatedRegistrationInvitation,
  type RegistrationInvitationDto,
  type RegistrationInvitationStatus,
  type createRegistrationInvitationSchema,
} from '@akeno/shared';
import type { z } from 'zod';
import type { AppContext } from '../../app/context.js';
import { auditEvents, authSessions, profiles, registrationInvitations, users } from '../../database/schema/index.js';
import { generateInvitationCode, sha256Hex } from '../../utils/crypto.js';
import { Errors } from '../../utils/errors.js';
import { toIso } from '../../utils/time.js';
import { toUserSummary } from '../users/service.js';

type InvitationRow = typeof registrationInvitations.$inferSelect;

function invitationStatus(row: InvitationRow, now = new Date()): RegistrationInvitationStatus {
  if (row.revokedAt) return 'revoked';
  if (row.useCount >= row.maxUses) return 'exhausted';
  if (row.expiresAt && row.expiresAt <= now) return 'expired';
  return 'active';
}

export class AdminService {
  constructor(private readonly ctx: AppContext) {}

  private async invitationDtos(rows: InvitationRow[]): Promise<RegistrationInvitationDto[]> {
    const creators = await this.ctx.users.getSummaries(rows.flatMap((row) => (row.createdById ? [row.createdById] : [])));
    return rows.map((row) => ({
      id: row.id,
      codeHint: row.codeHint,
      note: row.note,
      maxUses: row.maxUses,
      useCount: row.useCount,
      expiresAt: toIso(row.expiresAt),
      revokedAt: toIso(row.revokedAt),
      createdAt: row.createdAt.toISOString(),
      createdBy: row.createdById ? (creators.get(row.createdById) ?? null) : null,
      status: invitationStatus(row),
    }));
  }

  async listInvitations(): Promise<RegistrationInvitationDto[]> {
    const rows = await this.ctx.db
      .select()
      .from(registrationInvitations)
      .orderBy(desc(registrationInvitations.createdAt))
      .limit(200);
    return this.invitationDtos(rows);
  }

  /** Creates a registration invitation. The plain code is returned once and never stored. */
  async createInvitation(
    actorId: string | null,
    input: z.output<typeof createRegistrationInvitationSchema>,
    ipAddress: string | null,
  ): Promise<CreatedRegistrationInvitation> {
    const { display, normalized } = generateInvitationCode();
    const [row] = await this.ctx.db
      .insert(registrationInvitations)
      .values({
        codeHash: sha256Hex(normalized),
        codeHint: normalized.slice(0, 4),
        note: input.note,
        maxUses: input.maxUses,
        expiresAt: input.expiresInHours ? new Date(Date.now() + input.expiresInHours * 3_600_000) : null,
        createdById: actorId,
      })
      .returning();
    if (!row) throw new Error('Invitation insert returned no row');
    await this.ctx.audit.record({
      actorId,
      action: 'admin.invitation_created',
      targetType: 'registration_invitation',
      targetId: row.id,
      metadata: { maxUses: row.maxUses, expiresAt: toIso(row.expiresAt) },
      ipAddress,
    });
    const [invitation] = await this.invitationDtos([row]);
    return { invitation: invitation!, code: display };
  }

  async revokeInvitation(actorId: string, invitationId: string, ipAddress: string): Promise<RegistrationInvitationDto> {
    const [row] = await this.ctx.db
      .update(registrationInvitations)
      .set({ revokedAt: new Date() })
      .where(eq(registrationInvitations.id, invitationId))
      .returning();
    if (!row) throw Errors.notFound('The invitation');
    await this.ctx.audit.record({
      actorId,
      action: 'admin.invitation_revoked',
      targetType: 'registration_invitation',
      targetId: invitationId,
      ipAddress,
    });
    const [dto] = await this.invitationDtos([row]);
    return dto!;
  }

  async listUsers(query: string | undefined, limit: number): Promise<AdminUserDto[]> {
    const pattern = query ? `%${query.toLowerCase().replace(/[\\%_]/g, (match) => `\\${match}`)}%` : null;
    const rows = await this.ctx.db
      .select({
        id: users.id,
        onlineId: users.onlineId,
        status: users.status,
        role: users.role,
        createdAt: users.createdAt,
        lastSeenAt: users.lastSeenAt,
        displayName: profiles.displayName,
        avatar: profiles.avatar,
        sessions: sql<number>`(SELECT count(*)::int FROM auth_sessions s WHERE s.user_id = ${users.id})`,
      })
      .from(users)
      .innerJoin(profiles, eq(profiles.userId, users.id))
      .where(
        and(
          sql`${users.status} <> 'deleted'`,
          pattern ? or(ilike(users.onlineIdNormalized, pattern), ilike(profiles.displayName, pattern)) : undefined,
        ),
      )
      .orderBy(desc(users.createdAt))
      .limit(limit);
    return rows.map((row) => ({
      ...toUserSummary(row),
      role: row.role,
      status: row.status,
      createdAt: row.createdAt.toISOString(),
      lastSeenAt: toIso(row.lastSeenAt),
      activeSessionCount: row.sessions,
      connected: this.ctx.hub.isConnected(row.id),
    }));
  }

  async updateUser(actorId: string, userId: string, input: AdminUpdateUserInput, ipAddress: string): Promise<AdminUserDto> {
    if (actorId === userId) {
      throw Errors.badRequest('You cannot change your own role or status. Ask another administrator.');
    }
    const [existing] = await this.ctx.db.select().from(users).where(eq(users.id, userId)).limit(1);
    if (!existing || existing.status === 'deleted') throw Errors.notFound('The user');
    await this.ctx.db.transaction(async (tx) => {
      await tx
        .update(users)
        .set({ ...input, updatedAt: new Date() })
        .where(eq(users.id, userId));
      if (input.status === 'suspended') {
        await tx.delete(authSessions).where(eq(authSessions.userId, userId));
        await this.ctx.devices.revokeAllForUser(tx, userId);
      }
      await this.ctx.audit.record(
        { actorId, action: 'admin.user_updated', targetType: 'user', targetId: userId, metadata: { ...input }, ipAddress },
        tx,
      );
    });
    if (input.status === 'suspended') {
      this.ctx.hub.disconnectUser(userId, 'This account has been suspended.', REALTIME_CLOSE_CODES.sessionRevoked);
    }
    const [dto] = (await this.listUsers(undefined, 1000)).filter((user) => user.id === userId);
    if (!dto) throw Errors.notFound('The user');
    return dto;
  }

  async listAuditEvents(before: number | undefined, limit: number): Promise<{ events: AuditEventDto[]; hasMore: boolean }> {
    const rows = await this.ctx.db
      .select()
      .from(auditEvents)
      .where(before ? lt(auditEvents.id, before) : undefined)
      .orderBy(desc(auditEvents.id))
      .limit(limit + 1);
    const page = rows.slice(0, limit);
    const actors = await this.ctx.users.getSummaries(page.flatMap((row) => (row.actorId ? [row.actorId] : [])));
    return {
      events: page.map((row) => ({
        id: row.id,
        action: row.action,
        actor: row.actorId ? (actors.get(row.actorId) ?? null) : null,
        targetType: row.targetType,
        targetId: row.targetId,
        metadata: row.metadata,
        ipAddress: row.ipAddress,
        createdAt: row.createdAt.toISOString(),
      })),
      hasMore: rows.length > limit,
    };
  }

  async stats(): Promise<AdminStatsDto> {
    const [counts] = await this.ctx.db
      .select({
        total: sql<number>`count(*) FILTER (WHERE ${users.status} <> 'deleted')::int`,
        active: sql<number>`count(*) FILTER (WHERE ${users.status} = 'active')::int`,
        suspended: sql<number>`count(*) FILTER (WHERE ${users.status} = 'suspended')::int`,
      })
      .from(users);
    const memory = process.memoryUsage();
    const pool = this.ctx.db.$client;
    return {
      users: { total: counts?.total ?? 0, active: counts?.active ?? 0, suspended: counts?.suspended ?? 0 },
      realtime: { connectedUsers: this.ctx.hub.connectedUserIds().length, connections: this.ctx.hub.connectionCount() },
      process: {
        uptimeSeconds: Math.round(process.uptime()),
        rssBytes: memory.rss,
        heapUsedBytes: memory.heapUsed,
        nodeVersion: process.version,
      },
      database: { totalConnections: pool.totalCount, idleConnections: pool.idleCount, waitingRequests: pool.waitingCount },
    };
  }
}
