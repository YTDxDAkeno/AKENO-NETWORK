# Testing

AKENO NETWORK has four layers of automated tests. None of the backend or end-to-end tests mock
the database or the API: they run against real PostgreSQL and real HTTP/WebSocket servers.

| Layer | Location | Runner | Count (last run) |
| --- | --- | --- | --- |
| Shared schema unit tests | `packages/shared/src/schemas/*.test.ts` | Vitest | 8 |
| Server integration tests | `apps/server/test/*.test.ts` | Vitest + real PostgreSQL + real WebSockets | 83 |
| Web unit/component tests | `apps/web/tests/unit/*.test.ts(x)` | Vitest + jsdom + Testing Library | 20 |
| Browser end-to-end acceptance | `apps/web/tests/e2e/*.spec.ts` | Playwright (Chromium) against production builds | 1 scenario, 22+ steps |

## Running the tests

```bash
pnpm lint
pnpm typecheck
pnpm test        # shared + server + web
pnpm test:e2e    # builds everything, then runs Playwright
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
  HSTS for HTTPS origins; JSON 404, 413 and malformed-JSON errors; rate limiting.

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

## Last verified results

Executed on 2026-10-13 in the development container (Linux, Node.js 22.22.0,
PostgreSQL 16.15, Chromium 141 via Playwright 1.56.1):

| Command | Result |
| --- | --- |
| `pnpm lint` | passed (0 problems) |
| `pnpm typecheck` | passed |
| `pnpm --filter @akeno/shared test` | 8 / 8 passed |
| `pnpm --filter @akeno/server test` | 83 / 83 passed (stable across repeated runs) |
| `pnpm --filter @akeno/web test` | 20 / 20 passed |
| `pnpm test:e2e` | 1 / 1 passed (~17 s; passed 3 consecutive runs) |
| `docker build` + `docker compose up` | image built; stack healthy; registration, backup/restore and HTTPS via Caddy verified manually |

The GitHub Actions workflow (`.github/workflows/ci.yml`) runs the same checks; its first run on
GitHub has not been observed yet.

## Not covered by automated tests

- Real mobile devices and the PlayStation 5 browser (layouts were checked at 390 px width in
  Chromium only).
- Accessibility audits with screen readers (semantic markup and keyboard support were
  implemented but not audited with assistive technology).
- Multi-instance deployments (not supported in V1).
- Voice chat (not implemented).
