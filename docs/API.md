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
| Device pairing | `400 AUTHORIZATION_PENDING`, `SLOW_DOWN`, `ACCESS_DENIED`, `EXPIRED_TOKEN`, `PAIRING_INVALID`; `403 DEVICE_SCOPE_DENIED` |

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

## Paired devices — `/devices`

Consoles and other non-browser clients authenticate with a **device token** obtained through
code-based pairing; see [DEVICE_PAIRING.md](DEVICE_PAIRING.md) for the flow, scopes and security
properties.

- Send `Authorization: Bearer akd_…`. Cookies are ignored on such requests, and no `Origin` or
  `X-CSRF-Token` header is needed.
- Only routes mapped to a granted scope are allowed; everything else returns
  `403 DEVICE_SCOPE_DENIED`.
- The WebSocket accepts the same header (scope `realtime`).

| Method & path | Caller | Response |
| --- | --- | --- |
| `POST /devices/pairings` | device | `201 { deviceCode, userCode, verificationUri, verificationUriComplete, expiresIn, interval }` |
| `POST /devices/pairings/token` | device | `DeviceTokenResponse` once; else `400 AUTHORIZATION_PENDING \| SLOW_DOWN \| ACCESS_DENIED \| EXPIRED_TOKEN \| PAIRING_INVALID` |
| `POST /devices/pairings/lookup` \| `approve` \| `deny` | website | `{ userCode, scopes? }` |
| `GET /devices`, `DELETE /devices/:id` | website | List / unpair |
| `PATCH /devices/:id` | website (owner) | `{ scopes: DeviceScope[] }` → `{ device }`. Reauthorization, e.g. allow `activity:write` for a console paired before it existed. Devices can never call it; removing a permission ends the device's running game session and closes its realtime connection |
| `GET /devices/current`, `DELETE /devices/current` | device | Self-description / sign-out |

## Users, profiles, presence

| Method & path | Body / query | Response |
| --- | --- | --- |
| `GET /users/me` | — | `CurrentUser` (profile, privacy, presence preference) |
| `DELETE /users/me` | `{ password, confirmation: "DELETE" }` | `204` (see account deletion in SECURITY.md) |
| `GET /users/search?q=&limit=` | `q` 1–32 chars | `{ users: UserSearchResult[] }` (excludes self, blocked, inactive) |
| `GET /users/:handle` | handle = Online ID or UUID | `PublicProfile` (privacy-filtered; 404 if the user blocked the caller). Alpha 0.3 adds `appearance` (banner `null` when details are restricted), `badges`, `featuredBadges`, `favoriteGameDetails` (artwork for exact name matches), `mutualFriends` (≤ 6) and `party` (only while the user appears online to the viewer and the viewer may see the party) |
| `PATCH /profile` | any of `displayName, avatar, bio, statusMessage, favoriteGames[], activity` | `CurrentUser` (`avatar` is the preset; an uploaded picture takes precedence) |
| `PUT /profile/avatar` | raw image body (`image/jpeg`, `image/png`, `image/webp`, `image/gif` or `application/octet-stream`; ≤ 8 MB; website session + CSRF only) | `CurrentUser` with `profile.avatarImage`; `422 INVALID_IMAGE`, `413`, `415`, `429 MEDIA_QUOTA_EXCEEDED`, `507 MEDIA_STORAGE_FULL`, `503 MEDIA_UNAVAILABLE` |
| `DELETE /profile/avatar` | — | `CurrentUser` (preset shown again; files deleted) |
| `PUT /profile/banner`, `DELETE /profile/banner` | as above (3:1) | `CurrentUser` with `profile.appearance.bannerImage` |
| `PATCH /profile/appearance` | any of `theme` (`default, purple, blue, crimson, midnight, sakura, emerald, custom`), `accentColor` (`#rrggbb`, custom theme), `bannerOverlay`, `featuredBadges[]` (≤ 3, earned only) | `CurrentUser` |
| `PATCH /profile/privacy` | any of `profileVisibility, presenceVisibility, lastSeenVisibility, messagePrivacy, friendRequestPrivacy, gameActivityTracking, gameActivityVisibility, gameHistoryVisibility, gamePlaytimeVisible, gameExcludedTitles[]` (title IDs, max 50) | `CurrentUser` |
| `PUT /presence` | `{ status: "online" \| "away" \| "dnd" \| "invisible" }` | `{ preference, status }` |

## Game activity

Details, lifecycle and privacy rules: [GAME_ACTIVITY.md](GAME_ACTIVITY.md).

| Method & path | Caller | Body / response |
| --- | --- | --- |
| `POST /activity/heartbeat` | device with `activity:write` | `{ titleId, titleName?, platform: "PS5" }` → `{ session: { id, titleId, startedAt, created }, heartbeatIntervalSeconds, staleAfterSeconds }`; `403 DEVICE_ONLY` for web sessions, `403 DEVICE_SCOPE_DENIED`, `409 ACTIVITY_TRACKING_DISABLED`, `409 ACTIVITY_TITLE_EXCLUDED` |
| `POST /activity/stop` | device with `activity:write` | `{ reason?: "closed" \| "shutdown" }` → `{ ended }` |
| `GET /users/:handle/activity` | signed in (devices: `profile:read`) | `GameActivityResponse`: `current`, `historyVisible`, `playtimeVisible`, `recentGames` (durations `null` when playtime is hidden), `recentSessions`, `trackedSince`; owner only: `trackingEnabled`, `excluded`/`private` flags |
| `DELETE /activity/history` | owner, website | `{ deleted }` |
| `GET/PUT/DELETE /admin/game-titles[/:titleId]` | administrator | Curated game names (Alpha 0.2 API; maps the title ID to a canonical game) |

Since Alpha 0.3 `current`, `recentGames[]` and `recentSessions[]` carry `art: GameArtwork | null`
(`{ gameId, icon, cover, banner }` media IDs) for title IDs mapped to a verified game, and the
response has `totals: { seconds | null, games, sessions }` over all visible sessions. Artwork is
never attached by name similarity: see [GAME_ARTWORK.md](GAME_ARTWORK.md).

## Media

| Method & path | Caller | Response |
| --- | --- | --- |
| `GET /media/:id/:file` (outside `/api/v1`) | signed in, or device with `profile:read` | The image variant (`lg.webp`, `md.webp`, `sm.webp`, `fallback.jpg`) with a fixed `Content-Type`, `Cache-Control: private, max-age=31536000, immutable`, `ETag`; `404` for unknown or deleted IDs and anything that is not a valid ID and file name |

Clients build URLs with `mediaUrl(id, variant)` from `@akeno/shared`; media IDs are 128-bit
random values. Details: [MEDIA_STORAGE.md](MEDIA_STORAGE.md).

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
closes them. `PartyDto.voice` says whether voice chat can be used (`available`, `reason` in
English when not); `PartyMemberDto.voice` (`connected`, `muted`) is reported to party members
only (non-members always see `connected: false`). See [VOICE.md](VOICE.md).

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

### Party voice (Alpha 0.4)

| Method & path | Notes |
| --- | --- |
| `GET /voice/status` | Website session. `{ enabled, reason }`: whether voice is set up on this server |
| `POST /parties/:id/voice/token` | Website session + CSRF, members only, 20/min per user. `{ url, token, room, identity, expiresAt }`: a LiveKit JWT for this party's room only (identity = user ID, microphone publishing only, lifetime `VOICE_TOKEN_TTL_SECONDS`). Errors: `403 NOT_MEMBER`, `403 BLOCKED` (a blocked user is in the room), `503 VOICE_UNAVAILABLE` (not configured or LiveKit unreachable). Paired devices use the device route below |
| `POST /parties/:id/voice/device-token` | Paired device (`Authorization: Bearer`) with `parties:write`, members only, 20/min. `{ url, token, room, identity, expiresAt }`: `url` is the console voice gateway (`VOICE_GATEWAY_URL`, `wss://<domain>/voice-gateway`); `token` is a pass for this party's room only, marked for the gateway (`akeno.client=console-gateway`) and bound to the requesting device (sealed). Errors: `400` for website sessions, `401`, `403 DEVICE_SCOPE_DENIED`, `403 NOT_MEMBER`, `403 BLOCKED`, `503 VOICE_GATEWAY_UNAVAILABLE` (no gateway configured), `503 VOICE_UNAVAILABLE`. Unpairing the device or removing `parties:write` ends its voice connection. See [SERVER_VOICE_UPGRADE.md](SERVER_VOICE_UPGRADE.md) |
| `POST /voice/webhook` | Called by LiveKit only (server to server, no Origin check). Body verified against the `Authorization` JWT signed with the API secret (including the body's SHA-256); `401` otherwise. `204` on success |

Realtime: voice joins and leaves are delivered as the existing `party.changed` event (the party
is refetched), so member voice state stays current without polling.

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
| `GET /admin/stats` | User counts, live connections, process memory, database pool, media storage (`media.bytes`, `objects`, `limitBytes`, `freeBytes`, `available`) |
| `GET /admin/games?q=&missingArtwork=&limit=` | Canonical games with title IDs, artwork and provenance |
| `POST /admin/games` | `{ name, aliases[], titleIds[], artworkSource, artworkAttribution, artworkLicense }` → `GameDto` |
| `GET/PATCH/DELETE /admin/games/:id` | Read, edit (name, aliases, provenance), delete (artwork files are removed) |
| `PUT/DELETE /admin/games/:id/titles/:titleId` | Map / unmap a PlayStation title ID (verified mapping) |
| `PUT /admin/games/:id/artwork/:slot` | Raw image body; `slot` = `icon` (1:1), `cover` (2:3), `banner` (16:9); center-cropped, never enlarged |
| `DELETE /admin/games/:id/artwork/:slot` | Remove artwork |
| `GET /admin/games/detected-titles` | Title IDs reported by consoles: players, sessions, last played, reported name, mapping and artwork status (missing-artwork report) |
| `POST /admin/games/reload` | Rebuild the in-memory catalogue cache |
| `GET /admin/users/:id/badges` | Granted badges with grantor and note |
| `POST /admin/users/:id/badges` | `{ badgeId, note }`; derived `administrator` cannot be granted; authority badges (`founder`, `developer`, `verified_contributor`) cannot be granted to the caller's own account |
| `DELETE /admin/users/:id/badges/:badgeId` | Revoke (also removed from the featured selection) |

All catalogue and badge changes are written to the audit log (`admin.game_*`, `admin.badge_*`).

## Health

| Path | Meaning |
| --- | --- |
| `GET /health/live` | Process is up |
| `GET /health/ready` | Database reachable and migrations applied (`503` otherwise) |

## Realtime protocol (`/ws`, version 1)

Browsers connect with the session cookie from the same origin; paired devices send
`Authorization: Bearer <device token>` instead (no Origin required). Server messages are JSON
envelopes:

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
