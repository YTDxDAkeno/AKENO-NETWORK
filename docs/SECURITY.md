# Security

AKENO NETWORK is designed as an internet-facing application even though it serves a small
private group. This document describes the threat model, the controls that are implemented
(with file references), and known limitations.

## Threat model (summary)

| Threat | Main controls |
| --- | --- |
| Account takeover by password guessing | Argon2id, per-account and per-IP lockout, HTTP rate limits, generic error messages |
| Session theft | Opaque 256-bit tokens in HttpOnly Secure cookies, hashed at rest, idle + absolute expiry, revocation |
| Cross-site request forgery | Origin/Referer allowlist on all state-changing requests + per-session CSRF token header + SameSite=Lax |
| Cross-site WebSocket hijacking | Origin allowlist and session cookie checked before the upgrade |
| Unauthorized data access | Server-side authorization on every resource; private resources return 404 to non-members |
| Strangers contacting users | Invitation-only registration, blocking, message/friend-request privacy settings |
| Injection / XSS | Parameterized SQL (Drizzle), Zod validation, React output escaping, strict CSP, no HTML rendering of user text |
| Secrets leaking into logs | Pino redaction of cookies, CSRF tokens, passwords and invitation codes; message bodies never logged |
| Compromised container | Non-root user, read-only root filesystem, all capabilities dropped, database on an internal network |

## Authentication and sessions

- **Password hashing:** Argon2id via `@node-rs/argon2` with `m = 19456 KiB (19 MiB), t = 2,
  p = 1`, 32-byte output — the OWASP Password Storage Cheat Sheet minimum configuration for
  Argon2id. Parameters are configurable (`ARGON2_*`); hashes made with older parameters are
  upgraded automatically on the next successful login (`services/password.ts`). At most four
  hashes run concurrently (`ARGON2_MAX_CONCURRENCY`) to bound memory on small servers.
- **Password policy:** 12–128 characters, no composition rules, rejects a list of common
  passwords and passwords containing the Online ID (`packages/shared/src/schemas/auth.ts`).
  12 characters sits between the OWASP ASVS 5 minimum (8) and the NIST SP 800-63B rev. 4
  requirement for single-factor passwords (15). Raise it if your group prefers.
- **Unknown accounts:** login performs a dummy Argon2 verification so unknown Online IDs and
  wrong passwords take similar time and return the same message. Suspension is only revealed
  after a correct password.
- **Brute-force protection:** after 5 failures in 15 minutes an account is locked with
  exponential backoff (30 s doubling up to 15 min); an IP is locked for 15 minutes after 30
  failures (`modules/auth/login-throttle.ts`). The login route is additionally limited to 20
  requests per minute per IP. The throttle is in memory and resets on restart.
- **Sessions:** 32 random bytes (base64url) in the cookie; only the SHA-256 hash is stored
  (`auth_sessions.token_hash`). Absolute lifetime 30 days, idle timeout 7 days (configurable).
  At most 20 sessions per user. Cookies are `HttpOnly`, `SameSite=Lax`, `Path=/`, and over
  HTTPS `Secure` with the `__Host-` prefix. Tokens are never placed in `localStorage`.
- **Revocation:** logout, per-device sign-out, "sign out other sessions", password change
  (signs out all other sessions), admin suspension and account deletion delete session rows
  and immediately close affected WebSockets (close code 4401).
- **Paired devices:** consoles and other clients never receive the password. They get an
  opaque bearer token (`akd_` + 32 random bytes; SHA-256 stored) through code-based pairing
  approved in a signed-in browser ([DEVICE_PAIRING.md](DEVICE_PAIRING.md)). Tokens are limited to
  granted scopes by a default-deny route table, never carry the admin role, expire (180 days
  absolute, 30 days idle by default), and are revoked by unpairing, device sign-out, password
  change or reset, suspension and account deletion.
- **Invitations:** 16 characters from a 31-symbol alphabet (~79 bits), shown once, stored as
  SHA-256, limited uses and expiry, redeemed under a row lock inside the registration
  transaction, revocable, audited.
- **Administrator bootstrap:** no default credentials exist. The first admin is created with
  the CLI from the server shell (`create-admin`), which requires shell access to the host.

## CSRF and origin protection

1. Every `POST/PUT/PATCH/DELETE` under `/api/` must carry an `Origin` (or `Referer`) that
   exactly matches `PUBLIC_ORIGIN` or `ADDITIONAL_ORIGINS` (`middleware/origin.ts`). This also
   protects login and registration against login CSRF.
2. Authenticated state-changing requests must send `X-CSRF-Token`. The token is
   `HMAC-SHA256(session token, "akeno-csrf-v1")`: only the server (which receives the
   HttpOnly cookie) can derive it, it is bound to the session, and it needs no storage
   (`modules/auth/service.ts`, `middleware/auth.ts`). The web client keeps it in memory only.
3. Cookies use `SameSite=Lax`. There is no CORS configuration, so other origins cannot read
   API responses.
4. The WebSocket upgrade requires an allowed `Origin` and a valid session before a socket is
   created (`realtime/gateway.ts`); identity always comes from the server-side session, never
   from client-supplied data.
5. Requests with `Authorization: Bearer` (paired devices) ignore cookies completely and are
   therefore exempt from the Origin and CSRF checks; browsers cannot attach that header
   cross-origin without a CORS preflight, which is not allowed. The two unauthenticated pairing
   endpoints set and read no cookies.
6. `POST /api/v1/voice/webhook` is exempt from the Origin check because LiveKit calls it server
   to server; it reads no cookies and accepts only bodies whose signature (a JWT signed with
   the LiveKit API secret, containing the body's SHA-256) verifies.

## Authorization and privacy

- Every route except health, `/auth/config`, `/auth/register`, `/auth/login` and
  `/auth/session` requires a session. Administration routes also require the `admin` role.
- Resource-level checks are performed in services: conversation access is derived from
  direct-pair membership, group membership or party membership; group actions check roles;
  party actions check the leader; game-session actions check the host. Non-members receive
  **404** so that the existence of private groups, conversations and sessions is not revealed.
- Blocking removes friendships, cancels pending friend requests and invitations, hides both
  users from each other's search, and blocks messages, friend requests, invitations and
  joins. A user who blocked you is indistinguishable from a non-existent user.
- Privacy settings are enforced on the server: profile details, presence, last seen, direct
  messages and friend requests each have an audience. Presence events are only sent to
  friends and are filtered by these settings; invisible users never update `last_seen_at`.
- Game-session connection details are only returned to participants.

## Input, output and transport

- All input is validated with Zod schemas shared by client and server. Text is normalized
  (control characters removed, lengths enforced). Request bodies are limited to 64 KiB and
  WebSocket messages to 16 KiB; WebSocket clients are rate limited (token bucket) and must use
  the documented message types.
- SQL is generated by Drizzle with bound parameters; the few raw SQL statements use the `sql`
  template with parameters.
- The web client renders user text as text (React escaping); there is no Markdown/HTML
  rendering and no user-uploaded files. Avatars are a fixed set of presets.
- Security headers (`@fastify/helmet`): a strict Content-Security-Policy
  (`default-src 'self'`, no inline scripts or styles, `frame-ancestors 'none'`,
  `connect-src` limited to the app's own HTTP and WebSocket origins), `X-Content-Type-Options`,
  `Referrer-Policy: same-origin`, `Cross-Origin-Resource-Policy: same-origin`, and HSTS when
  served over HTTPS. API responses are `Cache-Control: no-store`.
- Production must use HTTPS; `COOKIE_SECURE=false` and open registration are refused in
  production unless explicitly overridden.

### Uploaded images (Alpha 0.3)

- Only signed-in website sessions with a CSRF token can upload; devices cannot. The raw-body
  parser exists only on upload routes (8 MB); unauthenticated uploads are refused before the
  body is read. Rate limit 12 per 10 minutes, quota 30 profile uploads per day, global size and
  free-space limits.
- File type is detected from the content (JPEG, PNG, WebP, GIF only); names and MIME types are
  ignored; SVG and every active format are refused. Decoding is bounded (dimensions, pixel count,
  timeout, one decode at a time). Images are re-encoded to WebP/JPEG with **all metadata
  removed** (EXIF/GPS, XMP, ICC); animations become still images.
- Storage paths come only from a random 128-bit ID and four fixed file names (no traversal);
  files are served with fixed content types, `nosniff`, the app CSP and same-origin resource
  policy, only to authenticated users and paired devices.
- Replaced and deleted images are removed immediately; orphans are swept; images are deleted
  with the account. Details: MEDIA_STORAGE.md.

### Badges

Badges are granted only by administrators (audited) or the operator CLI; the Administrator badge
is derived from the role; administrators cannot grant authority badges to themselves; users can
only feature badges they hold (validated on the server).

### Voice (Alpha 0.4)

Details in [VOICE.md](VOICE.md). In short:

- The AKENO server is the only authority: LiveKit creates no rooms by itself
  (`auto_create: false`), and joining needs a JWT issued by the API after checking party
  membership, blocks and the account state. Tokens are bound to one room and identity, expire
  after `VOICE_TOKEN_TTL_SECONDS` (default 120 s) and may publish microphone audio only.
- Membership, blocks, account state and the issuing browser session are checked again when
  LiveKit reports a join and every 30 s; leaving or removal from the party, closing, blocking,
  suspension, account deletion and signing out the issuing session disconnect the participant
  at once. Paired devices cannot obtain voice tokens.
- Voice traffic is encrypted by WebRTC (DTLS-SRTP) between each browser and the SFU; the SFU
  can technically access the audio it forwards (it is not end-to-end encrypted). Nothing is
  recorded: no egress service is deployed and no audio passes through the API.
- The LiveKit API key and secret live in `.env` (letters, digits, `-`, `_`; secret ≥ 32
  characters, validated at startup). The signalling/API port is not published publicly; only
  the media ports 7881/tcp and 7882/udp are, after the operator opens them.
- The Content-Security-Policy `connect-src` adds only the configured LiveKit origin; the
  microphone worklet is served from the app's own origin (`script-src 'self'`).
- Rate limiting: since Alpha 0.4 the built web client's files (the page and hashed assets) are
  not counted toward the per-user request limit, so reloading cannot break the app or a voice
  connection; API, WebSocket, media and health requests are limited as before.

## Logging and audit

- Structured JSON logs (pino). Cookies, authorization and CSRF headers, `Set-Cookie`,
  passwords and invitation codes are redacted. Request logs contain method, URL, status and
  timing — never request bodies. Client request IDs are not trusted; the server generates them.
- `audit_events` records registrations, logins and failures, logouts, session revocations,
  password changes, account deletions, invitation creation/revocation, admin user changes,
  group deletions and ownership transfers, and CLI operator actions. It never stores
  passwords, tokens, invitation codes or message contents.
- Error responses never include stack traces or SQL details.

## Data protection

- Messages are protected by TLS in transit and by database access control at rest.
  **Messages are not end-to-end encrypted**: anyone with access to the database or backups
  can read them. Treat backups as sensitive.
- Account deletion removes the profile, relationships, blocks, notifications, read state and
  sessions, removes memberships (handing over ownership/leadership), releases the Online ID
  and keeps the user row as an anonymous tombstone so messages already delivered to others
  remain readable as "Deleted user". Message bodies the user sent are not deleted.
- Notifications older than 90 days and expired sessions are removed automatically.

## Deployment hardening

- The container runs as the unprivileged `node` user with a read-only root filesystem,
  `cap_drop: ALL` and `no-new-privileges` (`compose.yaml`). The optional LiveKit container
  (`voice` profile) uses the same restrictions with a non-root UID and a 512 MB memory limit.
- PostgreSQL is on an internal Docker network without internet access and is never
  published. The app port is bound to `127.0.0.1`; only Caddy is public.
- Secrets live in `.env` (ignored by Git; `.env.example` contains placeholders only).
- `TRUST_PROXY` must list proxy addresses; hop counts are rejected because Fastify treats them
  as untrusted, which would silently disable client-IP detection.

## Known limitations

| Limitation | Impact / mitigation |
| --- | --- |
| No multi-factor authentication | Use long unique passwords; MFA (TOTP/WebAuthn) is a candidate for a later release |
| No email / password-reset by email | Operators reset passwords with `cli.js reset-password` |
| In-memory login throttle and presence | Reset on restart; acceptable for one instance (see ARCHITECTURE.md scaling path) |
| No end-to-end encryption | Stated above; do not claim E2EE (also for voice: the SFU can access the audio it forwards) |
| Voice participant cache and token-session map are in memory | Rebuilt from LiveKit every 30 s; after a restart the issuing-session check applies only to new tokens |
| No content moderation tools beyond blocking, group roles and admin suspension | Suitable for small trusted groups |
| No automated dependency vulnerability scanning in CI yet | Run `pnpm audit` periodically and keep images updated |
| Independent security review not performed | Recommended before exposing to people outside your circle of friends |

## Reporting

If you find a vulnerability, contact the repository owner privately rather than opening a
public issue.
