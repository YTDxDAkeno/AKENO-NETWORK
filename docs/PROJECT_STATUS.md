# Project status

_Last updated: 2026-10-13._ This file is the source of truth for continuing work across
sessions. Update it after every meaningful milestone. Do not mark anything complete that has
not actually been run.

## Current phase

**V1 core complete — Phases 0–8 done; Phase 9 (voice) not started (optional).**
Branch: `claude/epic-bell-s1psav`.

## Implemented and tested

| Feature | Evidence |
| --- | --- |
| Monorepo (pnpm workspaces), strict TypeScript, ESLint | `pnpm lint`, `pnpm typecheck` pass |
| PostgreSQL schema (20 tables), SQL migrations, migration runner with advisory lock | Applied to fresh databases in tests, E2E, Docker |
| Invitation-based registration, open mode for development, reserved IDs, password policy | `auth.test.ts`, E2E steps 0–2 |
| Argon2id hashing (OWASP parameters), rehash on parameter change | `auth.test.ts` (hash format), Docker on Alpine/musl |
| Opaque hashed sessions, HttpOnly/SameSite cookies, `__Host-` + Secure over HTTPS, idle + absolute expiry, revocation, password change | `auth.test.ts`, `health-security.test.ts`, Caddy HTTPS check |
| CSRF token + Origin allowlist, WebSocket origin/session checks | `auth.test.ts`, `realtime-presence.test.ts`, E2E step 21 |
| Login lockout and HTTP rate limits | `auth.test.ts`, `health-security.test.ts` |
| Admin bootstrap via CLI, invitation management, member suspension/roles, audit log, stats | `auth.test.ts`, E2E (admin UI), Docker container check |
| Profiles: display name, preset avatars, bio, status, favorite games, self-reported activity | `auth.test.ts`, E2E step 4 |
| Privacy settings (profile, presence, last seen, messages, friend requests) | `friends.test.ts`, `realtime-presence.test.ts` |
| Friends: search, requests, accept/decline/cancel, removal, race-safe | `friends.test.ts`, E2E steps 5–9 |
| Blocking (removes friendship, cancels requests/invitations, prevents interaction) | `friends.test.ts` |
| Presence: online/away/DND/invisible/offline, multi-device, idle, last seen, privacy filtering | `realtime-presence.test.ts`, E2E steps 9 and 22 |
| Direct messages: persistence, realtime delivery, idempotency, pagination, unread, typing, reconnect recovery, restart persistence | `messages.test.ts`, E2E steps 10–14 and 22 |
| Groups: roles, invitations, open groups, chat, moderation, ownership transfer, deletion | `groups-parties.test.ts`, E2E steps 15–17 |
| Text parties: leader, invitations, capacity, one party per user, chat, close | `groups-parties.test.ts`, `mobile-parties.spec.ts` |
| Game sessions: directory, filters, visibility, open/approval joins, invitations, capacity under concurrency, host handoff, close | `game-sessions.test.ts`, E2E steps 18–20 |
| Notifications and unified invitation inbox | Server tests, E2E (group invitation accepted from Home) |
| Account deletion with safe data handling | `auth.test.ts` |
| Web UI: all pages, responsive (desktop + phone), dark default + light theme, loading/empty/error states, confirmations, reconnect banner | Unit tests, both E2E specs (phone viewport without horizontal overflow), manual screenshots |
| Docker image (non-root, read-only FS), Compose stack, Caddy HTTPS + WSS | Built and run in the dev container; HTTPS verified with a local certificate |
| Backup and restore scripts | Round trip verified (data deleted, restored, user signed in) |
| Load test script and measurements | `docs/PERFORMANCE.md`, `docs/perf/*.json` |
| GitHub Actions CI (lint, typecheck, tests with PostgreSQL, build, Playwright E2E, Docker build) | Run 38455492193 on commit `854d97d` passed both jobs |

## Implemented but not yet tested in the target environment

- **VPS deployment** with a real domain and Let's Encrypt certificate — the same Compose
  stack was verified locally with Caddy's internal certificate for `localhost`.
- **Windows 11 / Docker Desktop** instructions — written from the Linux-verified commands;
  not executed on a Windows machine.
- **Controller navigation** (Gamepad API, opt-in) — implemented, not tested with a physical
  controller.
- **Light theme** — implemented via design tokens; only spot-checked.
- **Performance on a real 1 vCPU / 2 GB VPS** — approximated by pinning to one CPU core.

## Incomplete / known limitations

- No multi-factor authentication and no email-based password reset (operator CLI reset only).
- In-memory login throttle and presence reset on restart (single-instance design).
- No message editing/deletion, attachments or uploads (avatars are presets by design).
- Initial web bundle is ~187 KB gzipped (no route-level code splitting yet).
- Party member presence refreshes on events/refetch rather than a dedicated presence stream.
- `pnpm audit` / dependency scanning is not part of CI yet.

## Deferred

- **Phase 9 — voice chat** (WebRTC with authenticated signaling, STUN/TURN, optional LiveKit).
  Parties clearly show "Text chat only" / "Voice chat is not available". Design notes are in
  ARCHITECTURE.md and PERFORMANCE.md.
- Game integration API (design in GAME_INTEGRATION.md; nothing implemented).
- Multi-instance scaling (shared realtime bus).

## Unsupported (by design)

PSN sign-in, native console friends/parties/voice, Share Play, console game presence,
console game multiplayer, DNS redirection of vendor services, console payloads or
modifications. See PS5_FEASIBILITY.md.

## Last executed tests (2026-10-13)

| Command | Result |
| --- | --- |
| `pnpm lint` | passed |
| `pnpm typecheck` | passed |
| `pnpm --filter @akeno/shared test` | 8 / 8 passed |
| `pnpm --filter @akeno/server test` | 84 / 84 passed |
| `pnpm --filter @akeno/web test` | 20 / 20 passed |
| `pnpm --filter @akeno/web run test:e2e` | 2 / 2 passed: core acceptance + mobile/parties (3 consecutive runs) |
| `pnpm audit` | no known vulnerabilities (after overriding a dev-only esbuild advisory) |
| `docker build` / `docker compose up` / backup + restore / Caddy HTTPS | succeeded (manual verification) |
| Load test (20 users; 20 users on 1 core; 100 users) | completed without errors; results in PERFORMANCE.md |

Environment: Linux container, Node.js 22.22.0, pnpm 10.28.0, PostgreSQL 16.15,
Docker 29.8.2, Playwright 1.56.1 with Chromium 141.

## Important architectural decisions

1. Modular monolith: one Node.js process for API, WebSockets and static web; PostgreSQL only.
2. One conversation/message model for direct, group and party chat; one invitations table.
3. Database-enforced invariants for races (partial unique indexes, CHECK constraints, row
   locks) instead of application-only checks.
4. Realtime events only after commit; REST + cursors are authoritative for recovery.
5. Opaque server-side sessions; CSRF token derived by HMAC from the session token.
6. Presence computed from live connections + preferences, never stored as "online".
7. React Router 7 and TypeScript 5.9 chosen over newer majors for stability with tooling.
8. Playwright pinned to 1.56.1 to match the browser available in the development container.

## Next engineering actions

1. Deploy to a real VPS (1 vCPU / 2 GB) following VPS_DEPLOYMENT.md, run
   `load-test.mjs` there, and record the results in PERFORMANCE.md.
2. Execute the Windows 11 guide on a real Windows machine and correct any differences.
3. Add `pnpm audit` (or an equivalent scanner) to CI; it currently reports no known vulnerabilities.
4. Add route-level code splitting for rarely used pages (admin, settings).
5. Consider TOTP-based MFA for administrators.
6. Only then: start Phase 9 (voice) with a two-user WebRTC prototype behind a feature flag,
   using an operator-controlled TURN server with short-lived credentials.
