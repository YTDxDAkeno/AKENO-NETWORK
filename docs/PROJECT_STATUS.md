# Project status

_Last updated: 2026-10-14 (third session, work-in-progress checkpoint)._ This file is the source
of truth for continuing work across sessions. Do not mark anything complete that has not actually
been run.

Branch: `claude/epic-bell-s1psav`. See `git log` for the latest commit; the last commit with a
fully green GitHub Actions run is noted below.

> **Resume here (third session).** The PS5 console build now exists and compiles to a real ELF
> with the ps5-payload SDK. This was saved as a checkpoint ("save everything, continue later").
> Nothing has run on a console. The next steps are listed under
> [Session 3 — in progress](#session-3--ps5-console-build-in-progress).

## Summary

| Area | Status |
| --- | --- |
| AKENO NETWORK web platform (V1) | Implemented and tested (unchanged scope, one realtime bug fixed) |
| Device pairing + scoped device tokens (server, web UI) | **Implemented and tested** |
| Device client core `clients/ps5` (C11, Linux) | **Implemented and tested on Linux** against the real server |
| PS5 console payload (`clients/ps5/payload`) | **Builds to a real ELF** with the ps5-payload SDK; REST over libSceHttp2, native toasts, controller input. **Not run on hardware.** |
| HTTP backend split (POSIX / PS5) | **Done and tested on Linux** (23/23 integration, 273 unit checks) |
| WebSocket (WSS) on the console | **Not built** — no verified TLS byte-stream binding yet; the console uses bounded polling instead |
| Native Game Base / PSN integration | **Unsupported** — but a non-invasive native touchpoint was found: system toast notifications (`sceKernelSendNotificationRequest`, `sceNotificationSend`). See PS5_GAMEBASE_RESEARCH.md |
| Separate `AKENO-NETWORK-PS5` repository | **Not created yet** (planned after the VPS reconcile; see Session 3 steps) |
| Any test on PS5 hardware (12.20 / 13.60) | **None performed** |

## Session 3 — PS5 console build (in progress)

**Done and verified in a cloud container (Linux, clang/lld 18, ps5-payload SDK built from source):**

- **Source-level SDK review.** Reviewed `ps5-payload-dev/sdk` and `EchoStretch/kstuff-lite`
  sources (cloned into a scratch dir; not vendored). Findings:
  - The SDK builds userland ELF payloads for an already-jailbroken console via `prospero-clang`
    (host clang-18 + lld targeting `x86_64-sie-ps5`). It installs and builds here; `hello_world`,
    `http2_get`, `notify` samples all compile to real ELFs.
  - Networking/TLS is the system stack: `libSceNet` (BSD sockets), `libSceSsl` (TLS with the
    console's own root CA store; verification on by default), `libSceHttp2`/`libSceHttp`. No
    bundled OpenSSL, no custom trust anchors. The SDK ships symbol stubs, not headers; the
    samples declare prototypes inline, which is where ours come from (`src/ps5/sce.h`).
  - **Native notification interface found:** `sceKernelSendNotificationRequest` (plain toast) and
    `sceNotificationSend` (interactive JSON toast with DeepLink actions) — userland, with working
    SDK samples. This is a genuine, non-invasive way to surface AKENO events in the system UI.
    It is **not** the friends list / Game Base; those remain unsupported (PS5_GAMEBASE_RESEARCH).
  - kstuff-lite was reviewed only to understand the loader/firmware boundary; the client uses no
    kstuff functions and no kernel interfaces.

- **HTTP backend seam.** `src/httpclient.h` + `src/httpclient_posix.c` (host) and
  `src/ps5/httpclient_ps5.c` (console, libSceHttp2). `client.c` is now backend-agnostic, so the
  pairing/credential/JSON/capability logic is shared and exercised by the Linux tests.

- **PS5 payload** (`clients/ps5/payload/akeno_ps5.c` + `src/ps5/*`): pairing (code shown as a
  native toast + klog), bounded notification polling with native toasts for new events,
  controller input (Cross=refresh, Triangle=cycle presence, Options=sign out, Circle=quit),
  clean shutdown. Builds with `clients/ps5/payload/Makefile`:
  `PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make` → `akeno-network-ps5.elf` (PIE x86-64, SCE imports
  left for the loader). `make checksum` writes the SHA-256.

- **New shared, tested module** `src/events.c` (notification watermark + English formatter),
  wired into the core library and covered by `tests/unit/test_events.c` (30 checks).

**Next steps (not done):**

1. **Reconcile the VPS commits and create the repo** — still the first priority (see Phase 0
   below). The PS5 payload will move to `AKENO-NETWORK-PS5` only after that; keep the backend
   (pairing, device tokens) in this repo.
2. **Console TLS byte-stream for WSS.** Either verify `libSceSsl` raw-stream signatures from a
   working source (not invented) so `ws.c` can run over it, or use a system WebSocket binding if
   one is confirmed. Until then the console polls.
3. **On-screen controller UI.** v0.1 renders through system toasts only; a drawn UI needs a
   renderer/overlay (evaluate the SDK `browser` sample or an overlay lib).
4. **Per-message toasts.** The poll loop surfaces `/notifications` (social events). Direct/group
   messages are not in that stream; add unread-transition detection for message toasts.
5. **Hardware test** — only with explicit approval, following the SAFETY.md checklist on a
   non-primary console. Record results in FIRMWARE_COMPATIBILITY.md.

**How to rebuild the SDK + payload in a fresh session:**

```bash
# 1. Build/install the SDK (needs clang-18 + lld-18; both present here as llvm-18).
git clone --depth 1 https://github.com/ps5-payload-dev/sdk ps5-sdk-src
PATH=/usr/lib/llvm-18/bin:$PATH make -C ps5-sdk-src DESTDIR=/opt/ps5-payload-sdk install
# 2. Build the AKENO payload.
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
PATH=/usr/lib/llvm-18/bin:$PATH make -C clients/ps5/payload checksum
```

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
