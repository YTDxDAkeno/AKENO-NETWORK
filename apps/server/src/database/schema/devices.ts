import { sql } from 'drizzle-orm';
import { check, index, integer, pgTable, text, timestamp, uniqueIndex, uuid, varchar } from 'drizzle-orm/pg-core';
import { users } from './users.js';

/**
 * Short-lived pairing requests started by a device (RFC 8628-style device authorization).
 * Only SHA-256 hashes of the device code and the user code are stored.
 */
export const devicePairings = pgTable(
  'device_pairings',
  {
    id: uuid('id').primaryKey().defaultRandom(),
    deviceCodeHash: varchar('device_code_hash', { length: 64 }).notNull(),
    userCodeHash: varchar('user_code_hash', { length: 64 }).notNull(),
    deviceName: varchar('device_name', { length: 48 }).notNull(),
    platform: varchar('platform', { length: 48 }),
    clientVersion: varchar('client_version', { length: 32 }),
    requestedScopes: text('requested_scopes').array().notNull(),
    grantedScopes: text('granted_scopes').array(),
    /** pending → approved → redeemed, or pending → denied. */
    status: varchar('status', { length: 16 }).notNull().default('pending'),
    /** The account that approved or denied the request. */
    userId: uuid('user_id').references(() => users.id, { onDelete: 'cascade' }),
    deviceId: uuid('device_id'),
    requestIp: varchar('request_ip', { length: 64 }),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
    expiresAt: timestamp('expires_at', { withTimezone: true }).notNull(),
    decidedAt: timestamp('decided_at', { withTimezone: true }),
    redeemedAt: timestamp('redeemed_at', { withTimezone: true }),
    lastPolledAt: timestamp('last_polled_at', { withTimezone: true }),
  },
  (t) => [
    uniqueIndex('device_pairings_device_code_hash_key').on(t.deviceCodeHash),
    uniqueIndex('device_pairings_user_code_hash_key').on(t.userCodeHash),
    index('device_pairings_expires_at_idx').on(t.expiresAt),
    check('device_pairings_status_check', sql`${t.status} IN ('pending', 'approved', 'denied', 'redeemed')`),
    check(
      'device_pairings_decision_check',
      sql`(${t.status} = 'pending') = (${t.userId} IS NULL AND ${t.decidedAt} IS NULL)`,
    ),
    check('device_pairings_redeemed_check', sql`(${t.status} = 'redeemed') = (${t.redeemedAt} IS NOT NULL)`),
  ],
);

/**
 * Credentials of paired devices. A device authenticates with an opaque bearer token whose
 * SHA-256 is stored here; it is limited to the granted scopes and can be revoked at any time.
 */
export const deviceCredentials = pgTable(
  'device_credentials',
  {
    id: uuid('id').primaryKey().defaultRandom(),
    userId: uuid('user_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    tokenHash: varchar('token_hash', { length: 64 }).notNull(),
    name: varchar('name', { length: 48 }).notNull(),
    platform: varchar('platform', { length: 48 }),
    clientVersion: varchar('client_version', { length: 32 }),
    scopes: text('scopes').array().notNull(),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
    lastUsedAt: timestamp('last_used_at', { withTimezone: true }).notNull().defaultNow(),
    expiresAt: timestamp('expires_at', { withTimezone: true }).notNull(),
    ipAddress: varchar('ip_address', { length: 64 }),
    /**
     * AKENO Account Bridge: the local console user this device linked to the account, as the
     * device reported it. A local, unverified label; deleted with the credential when unpaired.
     */
    consoleUserId: integer('console_user_id'),
    consoleUserName: varchar('console_user_name', { length: 32 }),
    consoleUserLinkedAt: timestamp('console_user_linked_at', { withTimezone: true }),
    /** Outcome of the console's optional read-only account check (CONSOLE_ACCOUNT_STATES). */
    consoleAccountState: varchar('console_account_state', { length: 24 }),
    consoleAccountCheckedAt: timestamp('console_account_checked_at', { withTimezone: true }),
  },
  (t) => [
    uniqueIndex('device_credentials_token_hash_key').on(t.tokenHash),
    index('device_credentials_user_id_idx').on(t.userId),
    index('device_credentials_expires_at_idx').on(t.expiresAt),
    check(
      'device_credentials_console_user_check',
      sql`(${t.consoleUserId} IS NULL) = (${t.consoleUserName} IS NULL) AND (${t.consoleUserId} IS NULL) = (${t.consoleUserLinkedAt} IS NULL) AND (${t.consoleUserId} IS NULL) = (${t.consoleAccountState} IS NULL)`,
    ),
    check(
      'device_credentials_console_account_state_check',
      sql`${t.consoleAccountState} IS NULL OR ${t.consoleAccountState} IN ('not_checked', 'no_account_id', 'account_id_present', 'check_failed')`,
    ),
  ],
);
