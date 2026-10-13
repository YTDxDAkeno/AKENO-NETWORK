# Game integration contract (future, not implemented)

**Status: design proposal.** Nothing in this document is implemented in V1 except the
application-level game-session directory it builds on. AKENO NETWORK V1 does not detect,
launch, join or network any game on any platform. No game has been tested for compatibility,
and no compatibility is claimed.

This contract describes how an **explicitly compatible** client — a PC game, a mod using a
documented public API, a launcher, or a future native client built under a separate
authorization decision — could integrate with AKENO NETWORK. It does not use, imitate or
redirect any Sony NP / PSN API, and nothing here requires modifying a console.

## Goals

- Let a compatible client authenticate as a specific AKENO user with limited permissions.
- Let it publish "playing X" activity instead of the user typing it manually.
- Let it create and manage game-session directory entries and exchange optional connection
  metadata (for example a lobby code or an address the game itself understands).
- Fail safely: integration problems must never affect chat, friends or the web client.

## 1. Client identity

Each integration is registered by an administrator as a **client application**:

| Field | Description |
| --- | --- |
| `clientId` | Random public identifier |
| `name` | Display name shown to users ("Example Racing (PC)") |
| `gameIdentifier` | Stable identifier matching `game_sessions.game_identifier` (pattern `[A-Za-z0-9._:-]{1,64}`) |
| `allowedCapabilities` | Upper bound of what the client may request (see 3) |
| `status` | `active` / `disabled` (disabling revokes all tokens immediately) |

Clients are public (they cannot keep secrets), so no client secret is issued.

## 2. Authentication (device authorization)

Proposed flow, modeled on OAuth 2.0 Device Authorization (RFC 8628) because game clients and
consoles often lack a convenient browser:

1. `POST /api/v1/integrations/device/start { clientId, requestedCapabilities[] }` →
   `{ deviceCode, userCode, verificationUri, expiresIn, interval }`.
2. The user opens `verificationUri` in the AKENO web app, signs in normally, sees the client
   name and requested capabilities, and approves with the `userCode`.
3. The client polls `POST /api/v1/integrations/device/token { deviceCode }` until it receives
   `{ accessToken, expiresIn, refreshToken, grantedCapabilities[] }`.
4. Requests use `Authorization: Bearer <accessToken>`. Access tokens are opaque, short-lived
   (≤ 1 hour), stored hashed like web sessions, and never accepted on cookie-based web routes.
   Refresh tokens rotate on every use.
5. Users can list and revoke integrations in Settings; administrators can disable a client.

Integration tokens are a separate credential type: they can never change passwords, privacy
settings, friendships or read private messages.

## 3. Capability negotiation

The client requests capabilities; the user grants a subset; the server enforces the result on
every call.

| Capability | Allows |
| --- | --- |
| `presence.activity` | Set/clear the user's activity text for `gameIdentifier` (still labeled self-reported unless verified by a future trusted mechanism) |
| `sessions.read` | List sessions for `gameIdentifier` visible to the user |
| `sessions.host` | Create, update, start, finish sessions for `gameIdentifier` hosted by the user |
| `sessions.join` | Join/leave sessions for `gameIdentifier` on the user's behalf |
| `friends.read` | Read the user's friends list with privacy-filtered presence |

`GET /api/v1/integrations/capabilities` returns `{ granted[], serverVersion,
protocolVersion }` so clients can degrade gracefully when the server is older or newer.

## 4. Session lifecycle

Integrations reuse the V1 game-session model and its rules (visibility, join mode, capacity,
host handoff):

```
waiting ──start──▶ active ──finish──▶ finished
   ▲                 │
   └────reopen───────┘
```

| Operation | Endpoint (proposed) |
| --- | --- |
| Create | `POST /api/v1/integrations/sessions { maxParticipants, visibility, joinMode, region?, connection? }` (`gameName`/`gameIdentifier` come from the client registration) |
| Heartbeat | `POST /api/v1/integrations/sessions/:id/heartbeat` every ≤ 60 s while hosting; sessions without a heartbeat for 3 minutes are marked `finished` |
| Update / start / finish | `PATCH /api/v1/integrations/sessions/:id` |
| Join / leave | `POST /api/v1/integrations/sessions/:id/join` \| `/leave` |
| Events | The client may subscribe to the existing `/ws` protocol with its bearer token and receive `game-session.changed` for its sessions only |

## 5. Connection metadata

`connection` is an opaque object the server stores but never interprets or connects to:

```json
{ "type": "lobby-code", "value": "4242", "expiresAt": "2026-10-13T20:00:00Z" }
```

- Maximum 1 KB, returned only to session participants (same rule as V1 `connectionInfo`).
- Allowed `type` values are declared per client registration (`lobby-code`, `game-address`,
  `invite-token`). The server never resolves addresses, never relays game traffic, and never
  performs NAT traversal for games.

## 6. Failure reporting

- Errors use the standard error envelope (`{ error: { code, message, requestId } }`).
- Additional codes: `CAPABILITY_NOT_GRANTED` (403), `CLIENT_DISABLED` (403),
  `TOKEN_EXPIRED` (401), `SESSION_HEARTBEAT_EXPIRED` (409).
- Clients may send `POST /api/v1/integrations/diagnostics { level, code, message,
  clientVersion }` (rate limited, stored for 7 days, visible to administrators) so operators
  can see integration problems without collecting personal data. Message bodies, tokens and
  connection metadata must never be included.

## Non-goals

- No emulation of console network services, no DNS redirection, no traffic interception.
- No automatic detection of games running on a console.
- No game networking, matchmaking servers, anti-cheat or voice inside games.
- No compatibility list until a specific client is implemented and tested end to end.
