# Project status

_Last updated: 2026-10-13 (second session)._ This file is the source of truth for continuing
work across sessions. Do not mark anything complete that has not actually been run.

Branch: `claude/epic-bell-s1psav`. See `git log` for the latest commit; the last commit with a
fully green GitHub Actions run is noted below.

## Summary

| Area | Status |
| --- | --- |
| AKENO NETWORK web platform (V1) | Implemented and tested (unchanged scope, one realtime bug fixed) |
| Device pairing + scoped device tokens (server, web UI) | **Implemented and tested** |
| Device client core `clients/ps5` (C11, Linux) | **Implemented and tested on Linux** against the real server |
| Console (PS5) build of the client | **Not started** — needs SDK/loader review, a console TLS transport, a UI and your approval |
| Native Game Base / PSN integration | **Unsupported** — no documented interface; see PS5_GAMEBASE_RESEARCH.md |
| Any test on PS5 hardware (12.20 / 13.60) | **None performed** |

## Phase 0 — deployment continuity (this session)

| Check | Result |
| --- | --- |
| GitHub branch at session start | `d29a082` (same as the local clone) |
| VPS Git state (commit `2963980` reported earlier) | **Not verified.** This session ran in a cloud container whose network policy blocks `85.190.112.10.sslip.io` (HTTP 403 from the egress proxy) and port 22 of the VPS. |
| Production health, survival of the last reboot | **Not verified** (same reason). |
| `akeno-network-updates.bundle` | **Not accessible** from this session. Its commits are still only on the VPS. |
| Off-server encrypted backups, key-only SSH | **Not checked.** No changes were made to SSH. |
| CI on `d29a082` (run 38456220045) | **Failed**: core E2E step 17 lost a group message sent while the page was loading. Root cause found and fixed in the web client (see below). |
| CI on `78b544c` (run 38472359947) | Passed: checks + E2E, C client job, Docker build. |

### Reconciling the VPS-only commits (do this on your Windows PC)

The VPS commits and this session's commits both build on `d29a082`, so they must be merged.
No GitHub credentials are needed on the VPS.

```powershell
# 1. Copy the bundle from the VPS (find its path there first: ls ~ /opt/akeno)
scp administrator@vps-7971.onecom-cloud.one:/opt/akeno/akeno-network-updates.bundle .

# 2. In your clone
git fetch origin
git checkout claude/epic-bell-s1psav
git pull --ff-only
git bundle verify akeno-network-updates.bundle
git bundle list-heads akeno-network-updates.bundle
git fetch akeno-network-updates.bundle "refs/heads/*:refs/remotes/vps/*"
git log --oneline origin/claude/epic-bell-s1psav..vps/claude/epic-bell-s1psav   # VPS-only commits
git merge vps/claude/epic-bell-s1psav    # likely conflicts only in docs; keep both sides' facts

# 3. Verify, then push
pnpm install --frozen-lockfile
pnpm check && pnpm test:e2e
git push origin claude/epic-bell-s1psav
```

Then deploy on the VPS with the documented upgrade procedure (VPS_DEPLOYMENT.md → Upgrade):
back up first; the new migration only adds tables. Because the merge commit contains the VPS
commits, `git pull --ff-only` on the VPS fast-forwards without rewriting anything (check
`git status` is clean first). Pulling needs only read access (a public repository, or a
read-only deploy key) — never a write token on the VPS.

## Implemented and tested in this session

| Feature | Evidence |
| --- | --- |
| Device pairing (RFC 8628 semantics), one-time redemption, expiry, SLOW_DOWN, deny, hashes only | `apps/server/test/devices.test.ts` (17 tests) |
| Default-deny device scopes; sensitive routes unreachable; admin role never granted to devices | same file + route-table tests |
| Bearer auth on API and WebSocket; cookies ignored with bearer; CSRF/Origin unaffected for browsers | same file; all earlier server tests still pass |
| Revocation on unpair, device sign-out, password change, CLI reset, suspension, account deletion | server tests (unpair, sign-out, password change, expiry); code paths for the rest |
| `/pair` approval page with permission selection and phishing warning; Paired devices in Settings | `apps/web/tests/e2e/device-pairing.spec.ts` |
| Migration `0001_devices` (additive) upgrades a database from the previous revision without data loss | manual upgrade check (see TESTING.md) |
| C client: pairing, friends, presence, messages, invitations, parties, sessions, realtime, bounded reconnect, TLS verification, hostile-server handling | 243 unit checks + 23 end-to-end scenarios under ASan/UBSan; Valgrind; CI job |
| Web client fix: messages sent while a page was loading were lost until reload | `apps/web/tests/unit/message-sync.test.ts`; E2E 3 consecutive passes |

## Last executed tests (2026-10-13)

| Command | Result |
| --- | --- |
| `pnpm lint`, `pnpm typecheck` | passed |
| `pnpm test` | shared 8/8, server 101/101, web 26/26 |
| `pnpm test:e2e` | 3/3 (three consecutive runs) |
| C unit tests (ASan/UBSan, Valgrind) | 243 checks, 0 failures |
| C end-to-end runner | 23/23 |

Environment: Linux cloud container, Node.js 22.22.0, pnpm 10.28.0, PostgreSQL 16, GCC 13,
OpenSSL 3.0.13, Playwright 1.56.1 / Chromium 141.

## Earlier work (session 1, still valid)

Accounts, profiles, friends, blocking, presence, direct/group/party chat, groups, text parties,
game sessions, notifications, administration, Docker/Caddy deployment, backups, CI. Details are
in TESTING.md and the git history. Known limitations from that phase still apply (no MFA, no
email reset, in-memory login throttle, no message editing, no voice).

## Not done / open items

1. **VPS reconciliation and deployment** of this branch (above). Requires your PC or a session
   whose network policy allows the VPS.
2. **Backups off the server** and **key-only SSH**: not checked; changing SSH needs your
   explicit go-ahead.
3. **Console build** of the client: SDK and loader selection with a source-level review of
   their firmware support (not performed in this session), a TLS transport for the target, a
   controller UI, an artifact with checksum. Then a hardware test only with your approval
   ([SAFETY.md](SAFETY.md) checklist).
4. Native Game Base integration: recorded as unsupported. Revisit only if a documented,
   non-invasive interface is identified.
5. Optional: route-level code splitting, `pnpm audit` in CI, TOTP for admins (from session 1).

## Important decisions (this session)

1. Devices use opaque, revocable bearer tokens with an idle timeout instead of access/refresh
   token pairs: simpler and safer for clients with unreliable storage, same revocation model as
   web sessions.
2. Device permissions are a central default-deny route table, not per-route annotations, so a
   new route is unreachable for devices until it is deliberately listed.
3. The client is C11 with a single transport file for sockets/TLS, so the tested core can be
   reused for a console build without changes.
4. The client lives in this repository (`clients/ps5`) until you confirm creating a separate
   `AKENO-NETWORK-PS5` repository.
5. Console-side research was limited to what is needed to state, honestly, that native Game Base
   integration is unsupported; no exploit, kernel or system-modification work was done.
