# Project status

_Last updated: 2026-10-13 (third session)._ This file is the source of truth for continuing
work across sessions. Do not mark anything complete that has not actually been run.

Branches: `claude/adoring-johnson-6re0yl` (this session; based on `claude/epic-bell-s1psav` at
`8105593`, plus the reviewed parts of PR #1). `main` is untouched. See `git log` for details.

## Summary

| Area | Status |
| --- | --- |
| AKENO NETWORK web platform (V1) | Implemented and tested (unchanged this session) |
| Device pairing + scoped device tokens (server, web UI) | Implemented and tested (unchanged this session) |
| Device client core `clients/ps5` (C11) | Implemented and tested on Linux with **two** TLS transports (OpenSSL, mbedTLS) |
| **PS5 console app Alpha 0.1** (`clients/ps5/app`, `clients/ps5/console`) | **Implemented; ELF builds in CI; tested end to end on Linux (host build). Not yet run on a PS5.** See [PS5_CLIENT.md](PS5_CLIENT.md) |
| PS5 toolchain diagnostic ELF (from PR #1) | **Ran on a real PS5, firmware 12.20 + kstuff** (user-reported, with screenshot): native notification shown |
| Native Game Base / PSN integration | **Unsupported** — no documented interface; see PS5_GAMEBASE_RESEARCH.md |
| Production VPS revision | **Unverified** (local commits around `2963980` not reconciled; nothing deployed or changed this session) |

## Session 3 — PS5 console app Alpha 0.1

### Starting point and PR #1 review

- Baseline `8105593` (green CI). PR #1 (`chatgpt/ps5-http-backend`, commits `f9df326`, `e324fa6`)
  reviewed line by line:
  - `e324fa6` (diagnostic ELF + pinned-SDK workflow): **adopted unchanged** by cherry-pick
    (`4fd58dc`, original authorship kept). This is the ELF that ran on the console.
  - `f9df326` (HTTP backend split): the HTTP **method allow-list** was adopted (with unit tests).
    The `httpclient` split itself was **not** adopted: `transport.h` already isolates sockets and
    TLS, the HTTP/1.1 framing is platform-neutral and is reused unchanged on the console, and
    WebSockets need the raw stream anyway. The PR's `PS5_PORTING_PROGRESS.md` is superseded by
    [PS5_CLIENT.md](PS5_CLIENT.md).
- PR #1 is still open and was not merged into `main`.
- Work of the earlier, unpushed Claude session (PS5 frontend, controller, notification modules)
  was **not available** to this session; nothing of it is claimed or reconstructed here.

### Decisions (with evidence)

1. **TLS on the console = mbedTLS 3.6.7 over BSD sockets**, not `libSceSsl`/`libSceHttp2`. The SDK
   only ships stubs for those (no headers, no documented trust-store or host-name semantics, no
   WebSocket support); mbedTLS gives verifiable chain + host-name checks with our own trust store
   and runs the identical code on Linux, where the complete test suite exercises it. The SDK's
   libc provides sockets and an IPv4 `getaddrinfo` (via `sceNetResolver*`), read in
   `libc/netdb.c` of the pinned SDK.
2. **UI = page served on 127.0.0.1 and opened in the system browser** via
   `sceSystemServiceLaunchWebBrowser` (SDK `samples/browser`). Drawing directly to the screen from a
   background payload has no documented path in the SDK; the browser also provides the on-screen
   keyboard and its own controller handling. Security: per-launch secret, exact loopback Host,
   same-origin Origin, strict CSP, allow-listed proxy; the device token stays in the payload.
3. **Controller input = the browser's own handling** (plus spatial focus on arrow keys and the
   Gamepad API if exposed). `libScePad` from a background process was not used: the pad belongs
   to the foreground app and no reviewed sample shows safe shared use.
4. **Notifications = `sceKernelSendNotificationRequest`**, the exact call proven on 12.20.
5. **Entropy = `kern.arandom` sysctl** (as the SDK's own libc seeds), fallback `/dev/urandom`;
   TLS init fails if neither works.
6. **Import gate**: CI fails if the PS5 ELF imports an `sce*` function outside the reviewed list or
   if AKENO object files reference the SDK's kernel-memory helpers.
7. **SDK runtime disclosure**: every ps5-payload-sdk payload (including the diagnostic that ran)
   starts with the SDK `crt`, which raises its *own* process's credentials and syscall range through
   the loader's kernel R/W using per-firmware offsets from the SDK (12.20 and 13.60 are listed).
   AKENO code itself has no offsets and no kernel calls. Documented in SAFETY.md and PS5_CLIENT.md;
   this is inherent to running any payload with this SDK.

### Built this session

| Item | Where |
| --- | --- |
| mbedTLS transport (Linux + PS5), shared socket code, pinned inputs with SHA-256 | `src/transport_mbedtls.c`, `src/net_posix.c`, `scripts/fetch-deps.sh`, `config/akeno_mbedtls_config.h` |
| Console app: workers, settings, notifications, events, local page server, routes | `app/*.c` |
| Console page (friends, messages, invitations, party, sessions, diagnostics, settings, pairing, setup) | `app/web/` |
| PS5 build + import gate | `console/Makefile`, `console/check-imports.sh` |
| CI: Linux job runs both transports + the app with Chromium; new `PS5 console build` workflow builds both ELFs, checks imports, uploads ELF + SHA256SUMS + BUILDINFO | `.github/workflows/ci.yml`, `.github/workflows/ps5.yml` |

### Tests executed (2026-10-13, Linux container, GCC 13, clang 18, Node 22, PostgreSQL 16)

| Command | Result |
| --- | --- |
| `pnpm check` (lint, typecheck, shared/server/web tests) | passed |
| C unit tests, OpenSSL and mbedTLS builds (ASan/UBSan) | 383 checks each, 0 failures |
| C unit tests under Valgrind (Release, both builds) | clean |
| E2E, `akeno-cli` (OpenSSL) | 24/24 |
| E2E, `akeno-cli-mbedtls` + console app `akeno-app` (incl. two Chromium runs, keyboard only) | 38/38 |
| PS5 build with SDK `f5d40f0`, `-Werror` | builds; 1.1 MB stripped PIE; import gate passes (negative test verified) |

Not executed: anything on PS5 hardware; the PS5 browser; firmware 13.60; the production server.

### Open questions that only hardware can answer (see the test plan in PS5_CLIENT.md)

1. Does `sceSystemServiceLaunchWebBrowser` open the page from a payload on 12.20/13.60?
2. Can the PS5 browser load `http://127.0.0.1:8642` and run the page (ES2017, XHR)?
3. Which controller inputs reach the page (arrow-key events, Gamepad API, cursor only)?
4. Is `/data/akeno` writable and does `kern.arandom` work from the payload?
5. Does DNS + TLS to the real server work from the console (`sceNetResolver`, mbedTLS)?
6. Does the loader in use stay responsive while AKENO runs?

### Next steps

1. **User:** download the `akeno-network-ps5-alpha-0.1` artifact of the latest green "PS5 console
   build" run, verify the checksum and run the hardware test plan (H1–H10) on 12.20.
2. **User / VPS session:** reconcile the VPS commits (below), back up, then deploy a revision with
   device pairing — or test against a separate, disposable server first.
3. Record hardware results in FIRMWARE_COMPATIBILITY.md; fix what fails (the most likely area is
   browser launch / browser compatibility; fallback ideas: auto-start pairing with the code shown
   only as a notification, an opt-in LAN page with a short PIN).
4. Then: game-session actions in the page, QR code for the pairing URL, 13.60.

## Phase 0 — deployment continuity (session 2)

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

## Implemented and tested in session 2

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

## Tests executed in session 2 (2026-10-13)

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
3. **Console app hardware test** (session 3 built the app; see "Next steps" above). The SDK was
   reviewed at source level; the loader was not chosen by this project (use the one that ran the
   diagnostic). Hardware tests only with your approval ([SAFETY.md](SAFETY.md) checklist).
4. Native Game Base integration: recorded as unsupported. Revisit only if a documented,
   non-invasive interface is identified.
5. Optional: route-level code splitting, `pnpm audit` in CI, TOTP for admins (from session 1).

## Important decisions (session 2)

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
