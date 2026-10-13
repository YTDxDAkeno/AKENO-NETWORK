# API reference

These are AKENO NETWORK's own endpoints. They are not compatible with, and make no claims
about, Sony, PlayStation Network, BlueSphere or Omega APIs.

Request and response types are defined in `packages/shared/src/schemas` (Zod input schemas)
and `packages/shared/src/contracts` (response DTOs). The tables below summarize them.

## Conventions

- **Base path:** `/api/v1`. Health endpoints are at `/health/*`, the WebSocket at `/ws`.
- **Format:** JSON request and response bodies (`Content-Type: application/json`, max 64 KiB).
  Timestamps are ISO 8601 UTC strings. IDs are UUIDs except message and notification IDs,
  which are increasing integers used as cursors.
- **Authentication:** an opaque session token in an HttpOnly cookie (`__Host-akeno_session`
  over HTTPS, `akeno_session` over plain HTTP). Obtain it by registering or signing in.
- **CSRF:** every `POST`, `PUT`, `PATCH` and `DELETE` must
  1. come from an allowed origin (`Origin` header, or `Referer` as a fallback), and
  2. for authenticated requests, carry `X-CSRF-Token` with the `csrfToken` returned by
     `/auth/login`, `/auth/register` or `/auth/session`.
- **Errors:** `{"error": {"code": "...", "message": "English text", "fields": {...}, "requestId": "..."}}`.
  `fields` maps input paths to messages for validation errors. Codes are listed in
  `packages/shared/src/contracts/errors.ts`.

| HTTP | Typical codes |
| --- | --- |
| 400 | `VALIDATION_FAILED`, `BAD_REQUEST`, `INVITATION_REQUIRED`, `INVITATION_INVALID`, `INVALID_CREDENTIALS` (password change) |
| 401 | `UNAUTHENTICATED`, `INVALID_CREDENTIALS` (login) |
| 403 | `FORBIDDEN`, `CSRF_FAILED`, `ORIGIN_REJECTED`, `BLOCKED`, `PRIVACY_RESTRICTED`, `ACCOUNT_SUSPENDED` |
| 404 | `NOT_FOUND`, `NOT_MEMBER` — also returned for private resources the caller may not see |
| 409 | `CONFLICT`, `ONLINE_ID_TAKEN`, `ALREADY_FRIENDS`, `REQUEST_PENDING`, `ALREADY_MEMBER`, `CAPACITY_REACHED`, `SESSION_CLOSED` |
| 413 / 415 | `PAYLOAD_TOO_LARGE`, `UNSUPPORTED_MEDIA_TYPE` |
| 429 | `RATE_LIMITED` |

- **Rate limits** (per user when signed in, otherwise per IP): 300 requests/minute globally
  (configurable), login 20/min, registration 10 per 15 min, password change and account
  deletion 10/5 per 15 min, friend requests 30/min (plus 30/hour and 50 pending per user),
  sending messages 60/min, WebSocket handshakes 60/min.

## Authentication — `/auth`

| Method & path | Body | Response |
| --- | --- | --- |
| `GET /auth/config` | — | `{ registrationMode: "invite" \| "open", passwordMinLength }` |
| `POST /auth/register` | `{ onlineId, password, displayName?, invitationCode? }` | `201 AuthSessionResponse` + cookie |
| `POST /auth/login` | `{ onlineId, password }` | `AuthSessionResponse` + cookie |
| `POST /auth/logout` | — | `204`, session deleted, sockets closed |
| `GET /auth/session` | — | `AuthSessionResponse` or `401` |
| `GET /auth/sessions` | — | `{ sessions: AuthSessionDto[] }` |
| `DELETE /auth/sessions/:id` | — | `204` |
| `POST /auth/sessions/revoke-others` | — | `{ revoked: number }` |
| `POST /auth/password` | `{ currentPassword, newPassword }` | `204`, other sessions revoked |

`AuthSessionResponse = { user: CurrentUser, csrfToken, session: { id, expiresAt } }`.
Online IDs: 3–16 characters, letters/digits/`_`/`-`, starting with a letter, unique
case-insensitively. Passwords: 12–128 characters, not a common password, not containing the
Online ID.

## Users, profiles, presence

| Method & path | Body / query | Response |
| --- | --- | --- |
| `GET /users/me` | — | `CurrentUser` (profile, privacy, presence preference) |
| `DELETE /users/me` | `{ password, confirmation: "DELETE" }` | `204` (see account deletion in SECURITY.md) |
| `GET /users/search?q=&limit=` | `q` 1–32 chars | `{ users: UserSearchResult[] }` (excludes self, blocked, inactive) |
| `GET /users/:handle` | handle = Online ID or UUID | `PublicProfile` (privacy-filtered; 404 if the user blocked the caller) |
| `PATCH /profile` | any of `displayName, avatar, bio, statusMessage, favoriteGames[], activity` | `CurrentUser` |
| `PATCH /profile/privacy` | any of `profileVisibility, presenceVisibility, lastSeenVisibility, messagePrivacy, friendRequestPrivacy` | `CurrentUser` |
| `PUT /presence` | `{ status: "online" \| "away" \| "dnd" \| "invisible" }` | `{ preference, status }` |

## Friends and blocks

| Method & path | Body | Response |
| --- | --- | --- |
| `GET /friends` | — | `{ friends: FriendDto[] }` with presence |
| `DELETE /friends/:userId` | — | `204` |
| `GET /friends/requests` | — | `{ incoming: FriendRequestDto[], outgoing: FriendRequestDto[] }` |
| `POST /friends/requests` | `{ userId }` | `201 { status: "pending", request }` or `200 { status: "accepted" }` when a reverse request existed |
| `POST /friends/requests/:id/accept` | — | recipient only |
| `POST /friends/requests/:id/decline` | — | recipient only |
| `POST /friends/requests/:id/cancel` | — | sender only |
| `GET /blocks` | — | `{ blocked: BlockedUserDto[] }` |
| `POST /blocks` | `{ userId }` | `204`; removes friendship, cancels requests and invitations |
| `DELETE /blocks/:userId` | — | `204` |

## Conversations and messages

Direct conversations, groups and parties share one message model. Any conversation the caller
can access (direct participant, group member, party member) works with these endpoints.

| Method & path | Body / query | Response |
| --- | --- | --- |
| `GET /conversations` | — | `{ conversations: DirectConversationDto[] }` (direct only, with unread counts) |
| `POST /conversations` | `{ userId }` | `DirectConversationDto` (find or create; respects blocks and message privacy) |
| `GET /conversations/:id` | — | `DirectConversationDto` |
| `GET /conversations/:id/messages` | `before` \| `after` (message ID), `limit` ≤ 100 | `MessagePage { messages (chronological), hasMore }` |
| `POST /conversations/:id/messages` | `{ body (1–2000 chars), clientMessageId (UUID) }` | `201 { message, created: true }`, or `200 { created: false }` for an idempotent retry |
| `POST /conversations/:id/read` | `{ lastReadMessageId }` | `{ lastReadMessageId }` (never moves backwards) |

The send endpoint stores the message in PostgreSQL **before** responding and before the
`message.created` event is pushed. Retrying with the same `clientMessageId` never creates a
duplicate.

## Groups

| Method & path | Notes |
| --- | --- |
| `GET /groups` | Groups the caller belongs to, with role and unread count |
| `GET /groups/discover` | Open groups the caller is not in |
| `POST /groups` | `{ name (3–48), description?, joinPolicy: "invite_only" \| "open" }` → creator is owner |
| `GET /groups/:id` | `GroupDetailDto`; invite-only groups are 404 for non-members without an invitation |
| `PATCH /groups/:id` | Owner or moderator; only the owner may change `joinPolicy` |
| `DELETE /groups/:id` | Owner only; deletes members, invitations and messages |
| `GET /groups/:id/members` | Members with presence |
| `POST /groups/:id/join` | Open groups, or accepts a pending invitation |
| `POST /groups/:id/leave` | Owner must transfer first unless they are the last member (then the group is deleted) |
| `DELETE /groups/:id/members/:userId` | Owner: anyone; moderator: members only |
| `PATCH /groups/:id/members/:userId` | `{ role: "moderator" \| "member" }`, owner only |
| `POST /groups/:id/transfer-ownership` | `{ userId }`, owner only; previous owner becomes moderator |
| `GET/POST /groups/:id/invitations` | List pending (owner/moderator) / invite `{ userId }` |
| `GET/POST /groups/:id/messages` | Same semantics as conversation messages |

## Parties

A user is in at most one party. Parties are deleted when the last member leaves or the leader
closes them. **Voice is not available**; `PartyDto.voice.available` is always `false`.

| Method & path | Notes |
| --- | --- |
| `GET /parties/current` | `{ party: PartyDto \| null }` |
| `GET /parties` | Joinable parties: friends' parties open to friends, and parties the caller is invited to |
| `POST /parties` | `{ name?, privacy: "friends" \| "invite_only", maxMembers 2–16 }`; leaves the current party |
| `GET /parties/:id` | Members, friends of the leader (friends privacy) and invitees |
| `PATCH /parties/:id` | Leader: `{ name?, privacy? }` |
| `POST /parties/:id/join` | Friends of the leader (friends privacy) or invitees; enforces capacity |
| `POST /parties/:id/leave` | Leadership passes to the longest-standing member |
| `POST /parties/:id/close` | Leader only |
| `POST /parties/:id/leader` | `{ userId }`, leader only |
| `DELETE /parties/:id/members/:userId` | Leader only |
| `POST /parties/:id/invitations` | `{ userId }`, members may invite their friends |
| `GET/POST /parties/:id/messages` | Party text chat |

## Game sessions

Application-level lobby directory. Joining records participation inside AKENO NETWORK only.

| Method & path | Notes |
| --- | --- |
| `GET /game-sessions` | Discoverable sessions; query `q, status, platform, region, joinable=true, limit` |
| `GET /game-sessions/mine` | Sessions the caller hosts or joined (including finished) |
| `POST /game-sessions` | `{ gameName, gameIdentifier?, description?, visibility: public\|friends\|private, joinMode: open\|approval, maxParticipants 2–64, region?, platform?, connectionInfo? }` |
| `GET /game-sessions/:id` | `GameSessionDetailDto`; `connectionInfo` only for participants; join requests only for the host |
| `PATCH /game-sessions/:id` | Host only; `status: "waiting" \| "active"`, settings; max ≥ current participants |
| `POST /game-sessions/:id/join` | `{ message? }` → `{ outcome: "joined" \| "requested" \| "already_member", session }` |
| `DELETE /game-sessions/:id/request` | Cancel the caller's pending join request |
| `POST /game-sessions/:id/leave` | Host leaving hands hosting over; empty sessions finish |
| `POST /game-sessions/:id/close` | Host only; status becomes `finished` |
| `POST /game-sessions/:id/requests/:requestId/accept` \| `reject` | Host only |
| `DELETE /game-sessions/:id/members/:userId` | Host only |
| `POST /game-sessions/:id/invitations` | `{ userId }` (friends; only the host for private sessions) |

Capacity is enforced with a row lock, a conditional update and a database CHECK constraint, so
concurrent joins can never exceed `maxParticipants`.

## Invitations inbox and notifications

| Method & path | Notes |
| --- | --- |
| `GET /invitations` | Pending, unexpired invitations to the caller (groups, parties, sessions) |
| `POST /invitations/:id/accept` | Joins the target in one transaction → `{ kind, targetId }` |
| `POST /invitations/:id/decline` | Invitee only |
| `POST /invitations/:id/cancel` | Inviter (or a group owner/moderator) |
| `GET /notifications?before=&limit=` | `{ notifications, unreadCount, hasMore }` |
| `POST /notifications/read` | `{ all: true }` or `{ ids: number[] }` |

## Administration (admin role + CSRF)

| Method & path | Notes |
| --- | --- |
| `GET /admin/invitations` | Registration invitations with status (`active`, `expired`, `exhausted`, `revoked`) |
| `POST /admin/invitations` | `{ maxUses 1–100, expiresInHours \| null, note }` → `{ invitation, code }`; the code is shown once, only its SHA-256 is stored |
| `DELETE /admin/invitations/:id` | Revoke |
| `GET /admin/users?q=&limit=` | Members with session counts and connection state |
| `PATCH /admin/users/:id` | `{ status: "active" \| "suspended", role: "user" \| "admin" }`; not for the caller's own account; suspension signs the user out everywhere |
| `GET /admin/audit-events?before=&limit=` | Security audit log |
| `GET /admin/stats` | User counts, live connections, process memory, database pool |

## Health

| Path | Meaning |
| --- | --- |
| `GET /health/live` | Process is up |
| `GET /health/ready` | Database reachable and migrations applied (`503` otherwise) |

## Realtime protocol (`/ws`, version 1)

Connect with the session cookie from the same origin. Server messages are JSON envelopes:

```json
{ "v": 1, "type": "message.created", "ts": "2026-10-13T18:47:00.000Z", "data": { ... } }
```

| Server event | Data | Sent to |
| --- | --- | --- |
| `hello` | `userId, connectionId, serverTime, heartbeatIntervalMs, protocolVersion` | New connection |
| `pong` | `serverTime` | Reply to `ping` |
| `presence.updated` | `userId, presence` (privacy-filtered) | Connected friends |
| `self.presence` | `status` | The user's own devices |
| `friends.changed` | `reason, userId` | Both users |
| `message.created` | `conversationId, conversationKind, message` | All conversation members |
| `conversation.read` | `conversationId, lastReadMessageId` | The reader's devices |
| `typing` | `conversationId, userId` | Other conversation members |
| `notification.created` | `notification` | Recipient |
| `invitations.changed` | — | Invitee / inviter |
| `group.changed` / `party.changed` | `groupId`/`partyId`, `removed` | Members (and removed users) |
| `game-session.changed` | `sessionId` | Participants and, for listed sessions, the host's online friends |
| `profile.changed` | `userId` | The user and their friends |
| `session.revoked` | `reason` | Devices of a revoked session (followed by close 4401) |

Client messages (anything else closes the socket with 4400):

```json
{ "v": 1, "type": "ping" }
{ "v": 1, "type": "presence.idle", "data": { "idle": true } }
{ "v": 1, "type": "typing", "data": { "conversationId": "…" } }
```

Close codes: `1012` server restarting (reconnect), `4401` session revoked or expired (sign in
again), `4400` protocol violation, `4408` reserved for idle timeouts.

### Recovery after disconnects

Realtime events are not replayed. After reconnecting (`hello` received again), clients must:

1. For each conversation they display, call `GET /conversations/:id/messages?after=<newest id>`
   until `hasMore` is `false`, and merge by message ID (idempotent).
2. Refetch other state (friends, groups, parties, sessions, notifications) from the REST API.

The web client implements this in `apps/web/src/app/providers/RealtimeProvider.tsx`.
