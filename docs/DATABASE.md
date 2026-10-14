# Database

PostgreSQL 16 is the single source of truth for durable data. The schema is defined in
TypeScript with Drizzle ORM (`apps/server/src/database/schema/`) and compiled into plain SQL
migrations (`apps/server/src/database/migrations/`). All timestamps are `timestamptz` (UTC).

## Tables

| Table | Purpose | Key constraints |
| --- | --- | --- |
| `users` | Account identity: Online ID (display + normalized), Argon2id hash, role, status (`active`/`suspended`/`deleted`), last seen | Unique `online_id_normalized` (case-insensitive uniqueness) |
| `profiles` | Display name, avatar preset, bio, status message, favorite games (JSONB), self-reported activity, presence preference, privacy settings | PK = `user_id`, cascades with the user |
| `registration_invitations` | Invitation codes for private registration | Unique `code_hash` (SHA-256; the code itself is never stored); `CHECK use_count <= max_uses`, `max_uses 1–100` |
| `auth_sessions` | Opaque sessions | Unique `token_hash` (SHA-256 of a 256-bit token); absolute expiry + idle timeout |
| `friend_requests` | Requests and their outcome | `CHECK sender <> recipient`; **partial unique index on the unordered pair while `pending`** (no duplicate or crossing requests) |
| `friendships` | One row per friendship | PK `(user_low_id, user_high_id)` with `CHECK low < high` (no duplicates) |
| `user_blocks` | Blocks | PK `(blocker_id, blocked_id)`, `CHECK` not self |
| `conversations` | Message containers: `direct`, `group`, `party` | Direct pairs ordered and **unique** (partial unique index); CHECK ties pair columns to the kind |
| `messages` | All messages (identity `bigint` ID = cursor) | Index `(conversation_id, id)`; **unique `(sender_id, client_message_id)`** for idempotent retries; body length 1–4000 |
| `conversation_read_states` | Per-user read cursor per conversation | PK `(conversation_id, user_id)`; updated with `GREATEST` so it never moves backwards |
| `groups` | Group metadata and its conversation | Unique `conversation_id`; deleting the conversation cascades to the group |
| `group_members` | Membership and role | PK `(group_id, user_id)`; **partial unique index: one `owner` per group** |
| `parties` | Active parties | `CHECK member_count BETWEEN 0 AND max_members`, `max_members 2–16` |
| `party_members` | Party membership | **Unique `user_id`: one party per user** |
| `game_sessions` | Lobby directory entries | **`CHECK participant_count <= max_participants`**, `max 2–64`; index `(status, visibility, created_at)` |
| `game_session_members` | Participants (`host`/`player`) | PK `(session_id, user_id)` |
| `game_session_join_requests` | Approval-mode requests | Partial unique `(session_id, user_id)` while `pending` |
| `invitations` | Group / party / game-session invitations | CHECK: exactly one target column matching `kind`; partial unique pending invitation per target and invitee; `expires_at` |
| `notifications` | Durable notifications (JSONB payload) | Index `(user_id, id)`; pruned after 90 days |
| `audit_events` | Security audit log | Never contains passwords, tokens, invitation codes or message bodies |
| `device_pairings`, `device_credentials` | Code-based pairing and device tokens (migration `0001`) | Only SHA-256 hashes of codes and tokens; scopes per device |
| `game_activity_sessions` | Tracked game sessions reported by paired devices (`0002`); `private` flag for sessions reported while invisible (`0003`) | Partial unique `(user_id, title_id)` while running (no double counting); server timestamps; CHECKs on heartbeat/end order and end reason |
| `game_titles` | Curated game names of Alpha 0.2 (kept in step for rollback; copied into `games` by `0004`) | PK `title_id` |
| `media_objects` | Uploaded images (`0004`): kind, owner, source dimensions, variants (JSONB), byte size, SHA-256 of the upload, soft-delete time | PK = 32-hex random ID (also the storage key); `CHECK` on ID format and kind; purged 48 h after soft delete |
| `games` | Canonical games (`0004`): name, aliases, icon/cover/banner media, artwork source, attribution and licence notes | Media FKs `ON DELETE SET NULL` |
| `game_title_mappings` | Verified title ID → game (`0004`) | PK `title_id` (`CHECK [A-Z]{4}[0-9]{5}`); cascades with the game |
| `user_badges` | Badges granted by administrators or the operator CLI (`0004`) | PK `(user_id, badge_id)`; the derived Administrator badge is never stored |

`profiles` also holds the game-activity privacy settings: tracking on/off, audiences for the
current game and the history (`0002`), playtime visibility and excluded title IDs (`0003`), and
since `0004` the profile customization: `avatar_media_id`, `banner_media_id` (FKs to
`media_objects`, `SET NULL`), `banner_overlay`, `theme`, `accent_color`, `featured_badges`.
Migration `0004` is additive: existing users keep their preset avatar and get the default theme.

The suggested `direct_conversations`, `group_messages`, `party_messages`,
`direct_conversation_members`, `message_read_state`, `group_invitations`,
`party_invitations` and `game_session_invitations` tables are intentionally merged into
`conversations`/`messages`/`conversation_read_states` and one `invitations` table. This keeps
one implementation of sending, pagination, idempotency, unread counts and realtime delivery
for all chat types, and one invitation inbox.

## Concurrency guarantees

| Race | Protection |
| --- | --- |
| Two users send each other friend requests at the same moment | Partial unique index on the unordered pending pair; the losing insert is retried and turns into an acceptance. Tested. |
| Duplicate friendships | Ordered-pair primary key + `ON CONFLICT DO NOTHING` |
| Many users join a full game session simultaneously | `SELECT … FOR UPDATE` on the session row, conditional `UPDATE … WHERE participant_count < max_participants`, and a CHECK constraint as a backstop. Tested with 10 concurrent joiners for 3 slots. |
| Party capacity | Same pattern on `parties.member_count` |
| Single-use invitation code redeemed twice | Row lock on the invitation inside the registration transaction. Tested with 5 concurrent registrations. |
| Duplicate messages from client retries | Unique `(sender_id, client_message_id)` + `ON CONFLICT DO NOTHING`, then the existing row is returned |
| Duplicate direct conversations | Partial unique index on the ordered pair |
| Two owners in one group | Partial unique index; ownership transfer demotes before promoting |

Multi-step changes (accepting invitations, leaving parties with leader handoff, account
deletion, group deletion) each run in one transaction. Deadlocks or serialization failures
are reported to the client as `409 CONFLICT — please try again`.

## Deletion behavior

| Action | Effect |
| --- | --- |
| Delete group (owner) | Deletes its conversation, which cascades to the group, members, invitations and messages |
| Last member leaves a group | Group is deleted as above |
| Last member leaves / leader closes a party | Party, chat and invitations are deleted |
| Game session closed | Status `finished`; members kept for history; pending requests and invitations cancelled |
| Account deletion | Tombstone: profile reset, relationships, blocks, sessions, notifications and read states deleted; memberships removed with ownership/leadership handoff; Online ID released; authored messages remain attributed to "Deleted user" |

## Migrations

- Generate after editing the schema: `pnpm db:generate` (drizzle-kit writes a new SQL file).
  Review the SQL before committing it.
- Apply: `pnpm db:migrate`, or automatically at startup when `MIGRATE_ON_START=true` (the
  Docker image default). Migrations run in transactions and are serialized with a PostgreSQL
  advisory lock, and already-applied migrations are skipped (journal table
  `drizzle.__drizzle_migrations`).
- Never edit a migration that has been applied in production; add a new one.
- `GET /health/ready` returns 503 until migrations have been applied.
- Every migration so far is additive (new tables, or new columns with defaults), so the previous
  server revision keeps working on an upgraded database. Check an upgrade on a disposable copy
  first: `node infrastructure/scripts/upgrade-check.mjs --old <deployed checkout>` or
  `--from-dump <backup.dump>` (a restored production backup); it never touches production.

| Migration | Content |
| --- | --- |
| `0000_initial` | V1 schema |
| `0001_devices` | Device pairing and credentials |
| `0002_game_activity` | Game sessions, curated names, game-activity privacy columns |
| `0003_activity_privacy` | `profiles.game_playtime_visible`, `profiles.game_excluded_titles`, `game_activity_sessions.private` |

## Development data

`pnpm db:seed` creates `alice`, `bob` and `carol` (password `akeno-dev-password`, or
`SEED_PASSWORD`), a friendship, an open group and an invitation code. It refuses to run when
`NODE_ENV=production`. Never run it against a production database.
