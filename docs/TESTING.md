# Testing

AKENO NETWORK has five layers of automated tests. None of the backend or end-to-end tests mock
the database or the API: they run against real PostgreSQL and real HTTP/WebSocket servers.

| Layer | Location | Runner | Count (last run) |
| --- | --- | --- | --- |
| Shared schema unit tests | `packages/shared/src/schemas/*.test.ts` | Vitest | 8 |
| Server integration tests | `apps/server/test/*.test.ts` | Vitest + real PostgreSQL + real WebSockets | 101 |
| Web unit/component tests | `apps/web/tests/unit/*.test.ts(x)` | Vitest + jsdom + Testing Library | 26 |
| Browser end-to-end | `apps/web/tests/e2e/*.spec.ts` | Playwright (Chromium) against production builds | 3 scenarios (core acceptance with 22+ steps; mobile + parties; device pairing) |
| Device client and PS5 console app (C) | `clients/ps5/tests` | C unit tests under ASan/UBSan (OpenSSL and mbedTLS builds) + Node runner against the production server build, incl. Chromium for the console page | 383 checks per build + 24 CLI and 14 console-app end-to-end scenarios |

## Running the tests

```bash
pnpm lint
pnpm typecheck
pnpm test        # shared + server + web
pnpm test:e2e    # builds everything, then runs Playwright
pnpm test:client # builds the server, the C client and the console app (sanitizers), runs unit and E2E tests
```

### Database for tests

The server tests create a migrated template database (`akeno_test_template`) once, and clone
it into a disposable database per test file (`akeno_test_<random>`), so test files run in
parallel without sharing state. Leftover test databases are dropped at the end.

They need a PostgreSQL role that may `CREATE DATABASE`. By default they connect to
`postgres://akeno:akeno_dev_password@127.0.0.1:5432/postgres`; override it with:

```bash
export TEST_DATABASE_ADMIN_URL=postgres://user:password@host:5432/postgres
```

With `compose.dev.yaml` the `akeno` user is a superuser:
`TEST_DATABASE_ADMIN_URL=postgres://akeno:change-me-local-dev-password@127.0.0.1:5432/postgres`.

### End-to-end environment

`apps/web/playwright.config.ts` starts `infrastructure/scripts/e2e-server.mjs`, which:

1. drops and recreates the database `akeno_e2e` (it refuses names not starting with
   `akeno_e2e`),
2. applies migrations and creates the administrator `e2e_admin` through the real CLI,
3. runs the **production** server bundle (`NODE_ENV=production`, rate limiting and
   production Argon2 parameters enabled) serving the **production** web build on port 3200.

Configure with `E2E_DATABASE_ADMIN_URL`, `E2E_PORT` and `E2E_ADMIN_PASSWORD`. Playwright needs
Chromium: `pnpm --filter @akeno/web exec playwright install chromium` (CI also installs the
system dependencies with `--with-deps`). The Playwright version is pinned to 1.56.1.

## What is covered

### Server integration tests (`apps/server/test`)

- **auth.test.ts** — registration with invitations; unknown, exhausted, expired and revoked
  invitations; invitation not consumed by a failed registration; single-use invitation
  redeemed once under 5 concurrent registrations; Online ID format, reserved names and
  password quality; open registration mode; case-insensitive duplicate Online IDs; login from
  several devices; identical errors for wrong password and unknown account; account lockout;
  logout revokes the session server-side; expired and idle sessions; hashed tokens and
  passwords at rest; password change signs out other sessions; session revocation only by
  the owner; suspended accounts; authentication required for private endpoints; CSRF token
  required; foreign or missing Origin rejected; admin route protection; invitation codes not
  stored or listed; profile validation; account deletion with ownership transfer, tombstoned
  messages and Online ID reuse.
- **friends.test.ts** — request/accept flow and notifications; self, duplicate and
  already-friends requests; reverse request turns into acceptance; simultaneous mutual
  requests create exactly one friendship; only the recipient can accept/decline and only the
  sender can cancel; removal; "nobody" friend-request privacy; blocking removes friendships,
  prevents requests and messages, hides profiles and search results, cancels pending
  requests; profile visibility; message privacy.
- **realtime-presence.test.ts** — presence rule unit tests (audiences, invisible, idle, last
  seen, self view); WebSocket rejected without session or with a foreign/missing Origin;
  hello/ping/pong; protocol violations close the socket; logout closes sockets with 4401;
  friends see online/offline; a second device keeps the user online; away only when all
  devices are idle; invisible users appear offline and do not update last seen; non-friends
  do not see presence by default; realtime friend events and presence after acceptance.
- **messages.test.ts** — one conversation per pair (also under concurrency); persistence and
  ordering; idempotent retries (sequential and parallel) with no duplicate realtime event;
  before/after pagination; unread counts and monotonic read cursors; non-participants get 404
  for read, send and mark-read; validation; realtime delivery to every device of both users;
  typing indicators only to participants; missed-message recovery with the `after` cursor;
  messages, friendships and sessions survive a server restart.
- **groups-parties.test.ts** — invite-only group visibility; invitations and acceptance;
  group chat realtime and access control; role permissions (members, moderators, owner);
  removal notifications; ownership transfer before leaving; last member deletes the group;
  owner-only deletion; open-group discovery; parties: invitation-only access, friends-only
  invitations, party chat access, leader handoff, deletion when empty, capacity under
  concurrent joins, one party per user, kick/transfer/close permissions, joinable list.
- **game-sessions.test.ts** — creation and validation; join/leave; connection details hidden
  from non-participants; **10 concurrent joiners for 3 free slots → exactly 3 joined**;
  approval flow with accept/reject and notifications; public/friends/private visibility in
  discovery and direct access; private invitations; search and filters; host-only status
  updates; host handoff; closing prevents joins; host-only kicks.
- **health-security.test.ts** — liveness/readiness; CSP and other headers; `no-store` on API
  responses; HttpOnly/SameSite cookies and no token in bodies; `__Host-` Secure cookies and
  HSTS for HTTPS origins; JSON 404, 413 and malformed-JSON errors; empty JSON bodies accepted
  on action endpoints while `__proto__` payloads are rejected; rate limiting.

### Web unit tests (`apps/web/tests/unit`)

API client (CSRF header only on unsafe methods, error normalization, network errors,
unauthorized handling, one-time CSRF refresh), realtime client (status transitions,
exponential backoff, no reconnect after revocation, heartbeat timeout), message cache merging
and pending-message reconciliation, English relative time/presence/notification text, UUID
fallback for non-secure origins, and the login form (client validation and server errors).

### Core acceptance scenario (`apps/web/tests/e2e/core-acceptance.spec.ts`)

Separate browser contexts for an administrator, Alice, Bob and Mallory:

0. The administrator creates invitation codes in the Administration UI.
1. Alice registers with a valid invitation. 2. Bob registers with another one (a reused code
   is rejected; Mallory registers with a third).
3. Both sign out and sign in again (Bob with different letter case).
4. Both edit their profiles; Alice's profile page shows the changes.
5–6. Alice searches for Bob and sends a friend request.
7. Bob receives it **without reloading** and accepts.
8–9. Both appear as friends and see each other **Online**.
10–11. Alice messages Bob; Bob receives it without reloading.
12–13. Bob replies; Alice receives the reply in real time.
14. Both reload and still see the full history.
15. Alice creates a group. 16. Bob accepts the group invitation from his Home page.
17. Group chat works in both directions.
18. Alice creates a friends-only game session with connection details.
19. Bob discovers it in the directory and joins (connection details appear only after joining).
20. Both see the same two-person participant list.
21. Mallory is denied the group, conversation and session in the UI and through the API, is
    refused admin access, and a state-changing request without a CSRF token is rejected.
22. Bob signs out → Alice sees him go offline in real time (with last seen); a message sent
    while he is away is waiting after he signs in; Alice sees him online again; a reload
    reconnects the WebSocket.

The test also fails if any browser page logs a JavaScript error or a Content-Security-Policy
violation.

### Mobile and parties scenario (`apps/web/tests/e2e/mobile-parties.spec.ts`)

Two users on Pixel 7–sized phones (412 × 915): registration, the bottom tab bar and the
navigation drawer on every main page with **no horizontal overflow**, starting a named party
(shown as "Text chat only" with the voice-unavailable notice), inviting a friend, accepting
from Home, realtime member updates, party chat, leadership handoff when the leader leaves,
and the **Invisible** status hiding a user from friends until they switch to Do Not Disturb.

### Device pairing (`apps/server/test/devices.test.ts`, `apps/web/tests/e2e/device-pairing.spec.ts`)

Full pairing without a password; only hashes stored; one-time redemption; `SLOW_DOWN`,
`ACCESS_DENIED`, `EXPIRED_TOKEN`, unknown and malformed codes; granted scopes must be a subset;
approval requires a browser session with CSRF; suspended accounts get no token; device tokens
work without cookies/Origin/CSRF; default-deny for authentication, password, account deletion,
profile editing, privacy, blocks, device management and admin routes (also for admins);
scope limits incl. the WebSocket; cookies ignored when a bearer header is present (Basic auth
unaffected); revocation by unpairing, device sign-out, password change and expiry (live socket
closed with 4401); cross-user revocation refused; device presence and realtime events; the
route table only lists existing, non-sensitive routes. The browser scenario approves a code via
`/pair`, limits permissions, and unpairs from Settings.

### Device client and console app (`clients/ps5/tests`)

- **Unit (383 checks, ASan + UBSan, also Valgrind; built once per TLS transport):** strict JSON
  (escapes, surrogates, depth limit, int64) and JSON output round trips, bounded buffers, HTTP
  heads and chunked bodies (incl. conflicting lengths and unsupported encodings), WebSocket
  framing (RFC 6455 vectors, masking, RSV bits, control-frame rules, non-minimal lengths),
  SHA-1/Base64 vectors, URL parsing, backoff bounds, capability policy (firmware never changes a
  decision), log redaction, HTTP method/path validation, transport init and CSPRNG output, and
  the console app's local HTTP parser against hostile input (missing/duplicate Host, header
  injection, folding, oversized fields, chunked bodies), Host/Origin checks and secret comparison.
- **End-to-end, CLI (24 scenarios, run for the OpenSSL and the mbedTLS transport):** real server
  build on a fresh database behind a TLS terminator with a throwaway CA. Untrusted certificate,
  wrong host name and IP-vs-DNS mismatches are refused; a DNS-named certificate is accepted;
  pairing approve and deny; 0600 credential bound to the server; profile, friends, status,
  messages (quotes, backslashes, Unicode) and history; presence; party invitation accept; game
  sessions; realtime events, then a **server restart** with automatic reconnection; unpairing on
  the website stops the listener (exit 4) and deletes the credential; scope limits; logout;
  world-readable credential files refused; no tokens in logs; malformed HTTP, invalid JSON,
  oversized, truncated and stalled responses, 5xx and refused connections end cleanly; reconnect
  attempts are bounded.
- **End-to-end, console app (14 scenarios, `--app`):** the host build of the PS5 app against the
  same server: page secret, DNS-rebinding Host and cross-site Origin refused; proxy allow-list;
  pairing through the page and the website (code also as notification); friends and messages
  through the proxy; realtime events become page events and system notifications (and respect
  the notification switches); party invitation by notification and accepted through the page;
  diagnostics and a TLS connection test; a second launch reopens the page; revocation on the
  website deletes the credential; clean exit; untrusted server certificate refused; no tokens in
  logs; and two **Chromium** runs of the real page (first-run setup and pairing code; keyboard-only
  navigation, chat, live reply, every tab) with no script or CSP errors. Set
  `AKENO_UI_SCREENSHOTS=<dir>` to save screenshots (CI uploads them as `console-ui-screenshots`).

## Last verified results

Executed on 2026-10-13 in a cloud development container (Linux, Node.js 22.22.0,
PostgreSQL 16, Chromium 141 via Playwright 1.56.1, GCC 13, OpenSSL 3.0.13):

| Command | Result |
| --- | --- |
| `pnpm lint` | passed (0 problems) |
| `pnpm typecheck` | passed |
| `pnpm --filter @akeno/shared test` | 8 / 8 passed |
| `pnpm --filter @akeno/server test` | 101 / 101 passed |
| `pnpm --filter @akeno/web test` | 26 / 26 passed |
| `pnpm test:e2e` | 3 / 3 passed (~27 s; 3 consecutive runs) |
| `clients/ps5/build-asan/akeno_unit_tests`, `akeno_unit_tests_mbedtls` | 383 checks each, 0 failures (ASan/UBSan; Valgrind clean on the release builds) |
| `node clients/ps5/tests/integration/run.mjs …/akeno-cli` | 24 / 24 passed |
| `node clients/ps5/tests/integration/run.mjs …/akeno-cli-mbedtls --app …/akeno-app` | 38 / 38 passed (incl. Chromium) |
| `make -C clients/ps5/console` (PS5 SDK `f5d40f0`) + `check-imports.sh` | builds with `-Werror`; import gate passes |
| Migration upgrade check | database created with the previous revision's migrations + data, upgraded with `0001_devices`: data kept, new tables present |
| `docker build` + `docker compose up` | image built; stack healthy; registration, backup/restore and HTTPS via Caddy verified manually |

The GitHub Actions workflow (`.github/workflows/ci.yml`) runs the same checks on every push and
pull request. Its first run on GitHub (run 38455492193, commit `854d97d`) passed: lint,
typecheck, unit/integration tests against a PostgreSQL service, build, both Playwright
scenarios, the Docker image build and Compose validation.

## Not covered by automated tests

- **Any PlayStation 5 hardware.** The console app is built for the PS5 in CI but tested on Linux
  only; only the toolchain diagnostic ELF has run on a PS5 (see FIRMWARE_COMPATIBILITY.md).
- Real mobile devices and the PlayStation 5 browser (phone layouts are tested with Chromium's
  device emulation only).
- Accessibility audits with screen readers (semantic markup and keyboard support were
  implemented but not audited with assistive technology).
- Multi-instance deployments (not supported in V1).
- Voice chat (not implemented).
