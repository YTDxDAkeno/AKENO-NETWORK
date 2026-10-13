import type { FastifyBaseLogger } from 'fastify';
import type { DbExecutor } from '../database/client.js';
import { auditEvents } from '../database/schema/index.js';

export interface AuditEntry {
  actorId: string | null;
  action: string;
  targetType?: string;
  targetId?: string;
  metadata?: Record<string, unknown>;
  ipAddress?: string | null;
}

/**
 * Writes security-relevant events to the audit_events table.
 * Never pass passwords, session tokens, invitation codes or message contents in metadata.
 */
export class AuditService {
  constructor(
    private readonly db: DbExecutor,
    private readonly log: FastifyBaseLogger,
  ) {}

  /** Records an event inside the given transaction (or the default connection). */
  async record(entry: AuditEntry, executor: DbExecutor = this.db): Promise<void> {
    await executor.insert(auditEvents).values({
      actorId: entry.actorId,
      action: entry.action,
      targetType: entry.targetType ?? null,
      targetId: entry.targetId ?? null,
      metadata: entry.metadata ?? {},
      ipAddress: entry.ipAddress ?? null,
    });
  }

  /** Fire-and-forget variant for events that must not fail the surrounding request. */
  recordInBackground(entry: AuditEntry): void {
    this.record(entry).catch((error: unknown) => {
      this.log.error({ err: error, action: entry.action }, 'Failed to write audit event');
    });
  }
}
