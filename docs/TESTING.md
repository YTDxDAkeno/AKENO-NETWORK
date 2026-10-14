# Testing

AKENO NETWORK has five layers of automated tests. None of the backend or end-to-end tests mock
the database or the API: they run against real PostgreSQL and real HTTP/WebSocket servers.

| Layer | Location | Runner | Count (last run) |
| --- | --- | --- | --- |
| Shared schema unit tests | `packages/shared/src/schemas/*.test.ts` | Vitest | 8 |
| Server integration tests | `apps/server/test/*.test.ts` | Vitest + real PostgreSQL + real WebSockets | 151 (Alpha 0.3) |
| Web unit/component tests | `apps/web/tests/unit/*.test.ts(x)` | Vitest + jsdom + Testing Library | 27 |
| Browser end-to-end | `apps/web/tests/e2e/*.spec.ts` | Playwright (Chromium) against production builds | 6 scenarios (core acceptance; device pairing; 2 × game activity; mobile + parties; profiles, uploads, themes, badges and game artwork) |
| Device client and PS5 console app (C) | `clients/ps5/tests` | C unit tests under ASan/UBSan (OpenSSL and mbedTLS builds) + Node runner against the production server build, incl. Chromium for the console page | 496 checks per build + CLI and console-app end-to-end scenarios (74 end-to-end steps across the OpenSSL CLI run and the mbedTLS CLI + console-app run) |

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

### Alpha 0.4 additions (party voice)

- **voice.test.ts** (15, server) — against a fake LiveKit room API (Twirp/JSON) that records
  calls: tokens are room-bound, short-lived, microphone-only, identity = user ID, rooms created
  with the party's capacity; non-members, anonymous requests, missing CSRF and paired devices
  are refused; only correctly signed webhooks are accepted (forged secret, altered body);
  member voice state is shown to members only; intruders reported by LiveKit are removed;
  kick, leave and close disconnect / delete the room; blocks keep users apart; suspension;
  signing out the session that requested the token (and joins with such a token are refused);
  reconciliation; a LiveKit outage only affects voice; voice reported unavailable when not
  configured; configuration validation; CSP `connect-src`.
- **voice.test.ts** (7, web unit) — stored preference sanitizing, save/notify/reset,
  capture constraints, key labels, gate threshold and level-meter scale, microphone errors.
- **voice.spec.ts** (E2E) — needs a real LiveKit server (skipped otherwise; CI starts one).
  Two desktop Chromium users with Chromium's fake microphone join a party's voice, **receive
  each other's audio** (WebRTC inbound RTP with audio energy), see "Speaking" and mute state;
  "mute for me" stops receiving that person's audio; voice survives in-app navigation (voice
  bar); a reload leaves the room; the microphone test shows a level; a second tab takes the
  connection over; the leader removes a member, whose connection and microphone end and who is
  refused a new token; a phone-sized user joins from a party invitation ("Join voice"), the
  voice bar sits above the bottom navigation without horizontal overflow; leaving releases the
  microphone; no console or CSP errors. Test-only instrumentation records peer connections
  and microphone tracks in the page.

Running the voice E2E locally (LiveKit in Docker with host networking; the webhook points at
the E2E server port):

```bash
docker run -d --name livekit-e2e --network host -e LIVEKIT_CONFIG="$(cat <<'YAML'
port: 7880
bind_addresses: ["127.0.0.1"]
rtc: { tcp_port: 7881, udp_port: 7882, use_external_ip: false, node_ip: 127.0.0.1 }
keys: { akeno-dev: akeno-dev-secret-that-is-long-enough-0123456789 }
room: { auto_create: false }
webhook: { api_key: akeno-dev, urls: ["http://127.0.0.1:3200/api/v1/voice/webhook"] }
YAML
)" livekit/livekit-server:v1.12.0
E2E_LIVEKIT_URL=ws://127.0.0.1:7880 E2E_LIVEKIT_API_KEY=akeno-dev \
  E2E_LIVEKIT_API_SECRET=akeno-dev-secret-that-is-long-enough-0123456789 pnpm test:e2e
```

Manual checks done for the release candidate (scripts not part of the suite): the production
image with the Compose `voice` profile (two browsers exchanging audio over UDP 7882, LiveKit
as non-root with a read-only filesystem) and load measurements with 6 and 8 participants
(results in VOICE.md).

### Alpha 0.3 additions

- **media-profile.test.ts** (21) — valid uploads become square WebP/JPEG variants served only to
  signed-in users with immutable caching and `nosniff`; pictures appear in friends lists and
  profiles; non-square sources are center-cropped and never enlarged; EXIF/GPS/XMP/ICC are
  removed and EXIF orientation is applied; animated GIFs become still; text, SVG, HTML, PDF,
  TIFF and ELF files are refused whatever their declared type; truncated, corrupted and
  mislabelled images are refused; a 30000×30000 PNG header (decompression bomb) is refused from
  the header; too-small images, oversized bodies (413) and wrong content types (415) are refused;
  uploads need a session, an allowed Origin and the CSRF token; replacing and removing delete the
  old files; media needs authentication and rejects traversal and malformed paths; media
  survives a server restart; the daily quota and the global storage limit apply; an unwritable
  media directory disables only uploads (messaging keeps working); orphans and old soft-deleted
  rows are cleaned up; account deletion removes images; appearance validation; banners hidden
  from viewers of friends-only profiles; preset avatars keep working.
- **games-badges.test.ts** (14) — catalogue with verified title IDs and artwork (covers cropped
  to 2:3); artwork appears automatically in presence and history for a mapped title; **an
  unmapped title with an identical reported name gets no artwork**; detected-titles report;
  mapping updates live presence; favorite games and sessions match only exact names or mapped
  IDs; the Alpha 0.2 curated-name API still works and keeps `game_titles` in step; artwork files
  are deleted with the artwork or game; catalogue changes are audited; admin-only access; badges:
  derived Administrator badge, grant/revoke with audit, no self-granted authority badges, users
  cannot grant or feature unearned badges, revoked badges leave the featured selection.
- **profiles-artwork.spec.ts** (browser) — a non-image file is refused in the crop dialog; a
  picture is cropped, zoomed, moved with the keyboard and saved; a banner is uploaded; a theme is
  chosen with live preview; the administrator creates a game, maps a title ID and uploads icon and
  cover through the UI and grants a badge; the user features it; a simulated console reports the
  game and the friend sees the artwork in the friend list and on the themed profile (cover keeps
  2:3); the profile and home page have no horizontal overflow on a phone; removing the picture
  restores the preset; no browser errors or CSP violations.
- **Console app** — new C unit test for the read-only icon lookup (traversal, non-PNG, missing);
  console E2E: profile pictures reach the page through the bounded media fetch as data: images,
  console icons are read from `user/appmeta`, non-PNG files and traversal are refused, nothing is
  served without the page secret.
- **Upgrade check** — from a real Alpha 0.2 checkout (`9dcc192`): row counts kept, profile
  defaults, curated names migrated into verified title mappings, upload works on the upgraded
  database, Alpha 0.2 still works on it (rollback).
- **Backup drill** with the production Docker image (see MEDIA_STORAGE.md).

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

- **Unit (488 checks, ASan + UBSan, also Valgrind; built once per TLS transport):** strict JSON
  (escapes, surrogates, depth limit, int64) and JSON output round trips, bounded buffers, HTTP
  heads and chunked bodies (incl. conflicting lengths and unsupported encodings), WebSocket
  framing (RFC 6455 vectors, masking, RSV bits, control-frame rules, non-minimal lengths),
  SHA-1/Base64 vectors, URL parsing, backoff bounds, capability policy (firmware never changes a
  decision), log redaction, HTTP method/path validation, transport init and CSPRNG output, and
  the console app's local HTTP parser against hostile input (missing/duplicate Host, header
  injection, folding, oversized fields, chunked bodies), Host/Origin checks and secret comparison;
  game detection: title IDs, raw application-info parsing at both SDK offsets, layout votes
  (agreeing, alternative, conflicting, none), classification, debouncing, names from
  `param.json`/`param.sfo` and hostile metadata files.
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
- **End-to-end, console app (15 Alpha 0.1 scenarios, `--app`):** the host build of the PS5 app against the
  same server: page secret, DNS-rebinding Host and cross-site Origin refused; proxy allow-list;
  pairing through the page and the website (code also as notification); friends and messages
  through the proxy; realtime events become page events and system notifications (and respect
  the notification switches); party invitation by notification and accepted through the page;
  diagnostics and a TLS connection test; a second launch reopens the page; revocation on the
  website deletes the credential; clean exit; untrusted server certificate refused; pairing by
  notification alone when the browser cannot be opened; no tokens in logs; and two **Chromium** runs of the real page (first-run setup and pairing code; keyboard-only
  navigation, chat, live reply, every tab) with no script or CSP errors; the **Change Server**
  form stays open with its typed values over more than two refresh cycles and Cancel/Escape
  change nothing.
- **End-to-end, Alpha 0.2 console scenarios (10 more, simulated process list via
  `AKENO_SIM_PROCESSES`):** game activity permission granted later on the website; start,
  switch, two games (ambiguous), suspended game, close; conflicting and alternative
  application-info layouts; process-list failure; heartbeats stop after a crash and resume after
  a restart; console switch, website tracking switch and excluded game; exit ends the session;
  recent messages and a second launch opening the newest conversation; one-time notification
  links (no page secret, single use, expiry, malformed tickets, DNS rebinding) and the plain
  fallback; in Chromium: quick-open link, drafts kept across incoming messages, newest-message
  banner, Games tab, a friend playing, the new Diagnostics sections. Set
  `AKENO_UI_SCREENSHOTS=<dir>` to save screenshots (CI uploads them as `console-ui-screenshots`).
- **End-to-end, audio lab (5 scenarios, `--audiolab`, Alpha 0.4):** the experimental PS5 voice
  research build with simulated audio (`AKENO_SIM_AUDIO`, `AKENO_SIM_AUDIO_LIBDIR`): the release
  build has no audio routes and no audio section; nothing runs until the user asks; the probe
  reports library files and exported names read-only, and that microphone enumeration is not
  attempted; the tone needs the explicit confirmation, plays once at a time and reports its
  result; without audio output it reports "unavailable" and the app keeps working; the
  diagnostics page section works in Chromium. The PS5 workflow also builds the audio lab ELF,
  runs the import gate on it and fails unless its dynamic imports equal the release ELF's
  (docs/PS5_AUDIO_RESEARCH.md).

## Last verified results

Executed on 2026-10-14 (session 4, Alpha 0.2 release candidate) in a cloud development container
(Linux, Node.js 22.22.0, pnpm 10.28.0, PostgreSQL 16.15, Chromium via Playwright, GCC 13.3,
clang 18.1.3, OpenSSL 3.0.13, ps5-payload-sdk `f5d40f0`):

| Command | Result |
| --- | --- |
| `pnpm lint` | passed (0 problems; console page limited to ES2017 syntax) |
| `pnpm typecheck` | passed |
| `pnpm --filter @akeno/shared test` | 8 / 8 passed |
| `pnpm --filter @akeno/server test` | 116 / 116 passed |
| `pnpm --filter @akeno/web test` | 27 / 27 passed |
| `pnpm test:e2e` (website, Chromium) | 5 / 5 passed, two consecutive full runs |
| `akeno_unit_tests`, `akeno_unit_tests_mbedtls` (ASan/UBSan) | 488 checks each, 0 failures; Valgrind clean on the release builds |
| `node clients/ps5/tests/integration/run.mjs …/akeno-cli` | 24 / 24 passed |
| `node clients/ps5/tests/integration/run.mjs …/akeno-cli-mbedtls --app …/akeno-app` | 49 / 49 passed (24 client + 25 console-app scenarios, 4 of them in Chromium) |
| `make -C clients/ps5/console` + `check-imports.sh` | builds with `-Werror`; gate passes; imports = Alpha 0.1's + weak `sceKernelGetAppInfo` |
| Alpha 0.1 rebuild from `592c23a` | SHA-256 `35f393c6…6b65`, identical to the hardware-tested CI artifact |
| `node infrastructure/scripts/upgrade-check.mjs --old <592c23a>` | data from the Alpha 0.1 server kept through migrations 0002–0003; new and old server both work on the upgraded database; `--from-dump` restore path also checked |

The GitHub Actions workflow (`.github/workflows/ci.yml`) runs the same checks on every push and
pull request. Its first run on GitHub (run 38455492193, commit `854d97d`) passed: lint,
typecheck, unit/integration tests against a PostgreSQL service, build, both Playwright
scenarios, the Docker image build and Compose validation.

## Not covered by automated tests

- **PS5 hardware is tested by hand only.** Alpha 0.1 ran on a PS5 with 12.20 (user report);
  Alpha 0.2 has not run on a console. Game detection on the console is simulated in the tests
  (`AKENO_SIM_PROCESSES`, both application-info layouts); the real `sceKernelGetAppInfo` layout,
  metadata paths and the button notification are hardware test items (see
  HARDWARE_TEST_PLAN_ALPHA_0.2.md).
- Real mobile devices and the PlayStation 5 browser (phone layouts are tested with Chromium's
  device emulation only).
- Accessibility audits with screen readers (semantic markup and keyboard support were
  implemented but not audited with assistive technology).
- Multi-instance deployments (not supported in V1).
- Voice on real phones, Firefox and Safari, TURN relays and the production network/firewall
  (voice is tested in Chromium against a local LiveKit server only).
