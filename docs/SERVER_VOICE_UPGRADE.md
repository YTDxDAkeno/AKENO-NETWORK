# Console voice on the production server (Phase 4.3 server integration)

**Status (2026-10-19): the GitHub code and the staging tests are complete. Production has NOT been
changed.** The deployment below waits for the owner's explicit approval. German owner guides:
[server-voice/VPS-DIAGNOSE-DE.md](server-voice/VPS-DIAGNOSE-DE.md) (read-only check),
[server-voice/DEPLOY-DE.md](server-voice/DEPLOY-DE.md) (deployment and rollback),
[server-voice/PS5-ERSTTEST-DE.md](server-voice/PS5-ERSTTEST-DE.md) (first real PS5 test).

Goal: the existing AKENO server accepts voice connections from a paired PS5 (the PS5 voice-alpha of
PR #18: `akeno-party-voice` 0.1.0-alpha, plugin 1.20, native-IPC daemon interface 3) in the same
LiveKit room as browsers and phones (`akeno-party-<party id>`). No second voice system, no change
to the website's voice, no database migration, no new firewall port.

## 1. Production findings

The production VPS (`85.190.112.10.sslip.io`) is **not reachable from the environment this work
ran in** (the egress policy refuses the host; there is no SSH). Nothing on the VPS was read or
changed. What follows is established from the repository and the hardware reports; the rest is
left to the read-only diagnostics ([script](../infrastructure/scripts/vps-voice-diagnostics.sh)).

| Question | Answer | Evidence |
| --- | --- | --- |
| Deployed commit | **Not verified.** Reported as `af571eb` on 2026-10-14 (not on GitHub). Production served Alpha 0.3 features on 2026-10-17, so it was upgraded after that report; the current commit is unknown | PROJECT_STATUS.md; PS5_HARDWARE_RESULTS_FW12.20.md (real avatar, banner, badges, covers) |
| Server level | At least the Alpha 0.3 server (device pairing, game activity with 14 device scopes, media and badges; migration `0004` applied) and **before** the console voice pass | Native 0.1 report: `GET /devices/current` 200 with 14 permissions; Alpha 0.5 report: profile images |
| VPS-only commits / local changes | **Unknown.** The VPS had unpublished commits before (`2963980`, reconciled locally into `af571eb`) | PROJECT_STATUS.md, session 2 |
| Browser voice (LiveKit) in production | **Unknown** (diagnostics section 5 shows it) | — |
| Caddy configuration | Unknown; the repository Caddyfile before this PR has no `/voice-gateway` route | — |
| **Reason for the HTTP 404** | **The deployed server build does not contain the route.** `POST /api/v1/parties/:id/voice/device-token` was added in `787d503` (2026-10-18) and never deployed. The answer `404 NOT_FOUND "The requested endpoint was not found."` with a `requestId` is the app's own not-found handler: Caddy forwarded the request and the app had no such route. A missing permission would answer `403 DEVICE_SCOPE_DENIED`, missing configuration `503 VOICE_GATEWAY_UNAVAILABLE` / `503 VOICE_UNAVAILABLE` | `apps/server/src/app/build-app.ts` (not-found handler), PS5_NATIVE_1.1_HARDWARE_RESULTS_FW12.20.md; **reproduced** by the staging drill with the Alpha 0.4 image (byte-identical answer) |

### Provisional baseline

Every GitHub line from the Alpha 0.4 release candidate (`ae94851`, CI green) up to the head of PR #18
(`ce06ba0`) carries **identical server code** apart from the voice changes:
`git diff ae94851 ce06ba0 -- apps/server apps/web packages/shared compose.yaml infrastructure` shows
only the console voice files listed below (and `git diff ae94851 34c2485` for those paths is
empty). This PR therefore uses `ae94851` as its **provisional** production baseline. It is not
labelled production-ready until the diagnostics confirm what the VPS runs (§4, case analysis).

## 2. The server integration in this PR

Branch `claude/gracious-galileo-c5dxv8`, base `claude/vibrant-mendel-v55zqf` (`ae94851`). The PR does
**not** contain PR #17 or PR #18 (no PS5 client, daemon, ShellUI, plugin or OnionHEN change).

| Commit | Content | Origin |
| --- | --- | --- |
| Server: short-lived device voice passes | device route, `issueDeviceToken`, `hasScope`, revocation on unpairing / scope removal / password change, `VOICE_GATEWAY_URL`, error code | cherry-pick of `787d503` (server only) |
| Voice gateway | `apps/voice-gateway` (LiveKit Node SDK, Opus, mixer, protocol 1) + tests | cherry-pick of `d1dbdd5` (gateway only) |
| Deployment files | compose `voice-gateway` service, Caddy route, gateway Dockerfile, `.env.example` | server files of `4985e77` |
| Revocation tied to the connection in the room | sealed credential (AES-256-GCM) in each pass; fail closed after restarts | server files of `4d8a971` |
| A refusal removes only the checked connection | sid-checked removals; settle before shutdown | server files of `d8f1f43` |
| Deployment tooling | read-only diagnostics, upgrade/rollback script, staging drill | new |
| CI | gateway tests with real LiveKit, gateway image, Caddyfile validation, staging drill | new |

`apps/server`, `apps/voice-gateway`, `packages/shared`, `compose.yaml`, `infrastructure/caddy`,
`infrastructure/docker` and `.env.example` are **byte-identical** to the PR #18 head. The voice-alpha
on the PS5 was therefore built and tested against exactly this server code.

Deployment-relevant diff (`git diff --stat ae94851 HEAD -- apps packages compose.yaml infrastructure/caddy infrastructure/docker .env.example pnpm-lock.yaml`):
27 files, +1 932 / −61 lines, of which 723 are tests and 156 the lockfile entries of the gateway.

### The endpoint

`POST /api/v1/parties/:id/voice/device-token` — paired devices only (`Authorization: Bearer akd_…`).

| Check | Where |
| --- | --- |
| Valid, unexpired device credential of an active account | `requireAuth` + `requireDevice` (a website session gets `400`, no or unknown token `401`) |
| Scope `parties:write` | central default-deny device route table (`403 DEVICE_SCOPE_DENIED`) |
| Current member of exactly this party | `403 NOT_MEMBER` |
| No block with anyone already in the room | `403 BLOCKED` |
| Capacity | the LiveKit room is created with `maxParticipants` = the party's member limit |
| Console voice configured | `503 VOICE_GATEWAY_UNAVAILABLE` (`VOICE_GATEWAY_URL` empty) / `503 VOICE_UNAVAILABLE` (LiveKit off or unreachable) |
| Rate limit | 20 per minute |

Answer `200 { url, token, room, identity, expiresAt }`: `url` = `VOICE_GATEWAY_URL`; `token` = a
LiveKit JWT for that room only, identity = the user ID, microphone publishing only, lifetime
`VOICE_TOKEN_TTL_SECONDS` (120 s; only needed to join; every reconnect fetches a fresh pass), with the
attribute `akeno.client=console-gateway` (the gateway accepts nothing else) and `akeno.grant` (the
issuing device, encrypted and bound to identity and room). The device token never leaves the
console; the pass carries no account credential.

Revocation (the console's voice connection ends at once, close code 4003, and is not retried):
leaving or removal from the party, the party closing, blocking, suspension, account deletion,
unpairing the device, removing `parties:write`, a password change. Every join is re-checked by the
signed LiveKit webhook and every 30 s by reconciliation, also after a server restart.

### The voice gateway

`wss://<AKENO_DOMAIN>/voice-gateway` (Caddy → `voice-gateway:7890`). The console sends Opus
(48 kHz mono, 20 ms) over this TLS WebSocket; the gateway is a LiveKit participant with the
console user's identity, so browsers see and hear the console like any member. It refuses requests
with an `Origin` header (browsers), passes without the console attribute, foreign signatures and
garbage; one session per account (a newer one replaces the older: 4004); limits on message size,
uplink rate and buffering; idle timeouts; at most `VOICE_GATEWAY_MAX_SESSIONS` (50). It stores
nothing and logs user ID, room and counters, never passes or audio. Container: non-root, read-only,
no capabilities, 512 MB limit, health check `GET /healthz` (not exposed through Caddy).

### Compose, Caddy, firewall

- `compose.yaml`: the `voice-gateway` service in the **existing** `voice` profile;
  `VOICE_GATEWAY_URL` passed to the app. Nothing else changes.
- `Caddyfile`: `handle /voice-gateway { reverse_proxy voice-gateway:7890 }`, an exact path that
  collides with neither the app (`/api`, `/ws`, `/media`, the web pages) nor `/livekit/*`.
- **Caddy must be recreated, not reloaded**: the Caddyfile is a single-file bind mount and
  `git` replaces the file, so `caddy reload` keeps reading the old one (measured in staging:
  after a reload `/voice-gateway` still reached the app; after a recreate it reached the gateway).
- **No new firewall port.** Consoles use 443. The gateway reaches LiveKit inside Docker for
  signalling and LiveKit's advertised address (`LIVEKIT_NODE_IP` or the STUN-discovered IP) for
  media; when that address is on the VPS's own interface this stays on the machine (staging setup).
  If the VPS sits behind 1:1 NAT (public IP not on any interface), check the first console join
  (diagnostics §6 shows the addresses).

### Compatibility

- **No database migration**; the database is never restarted. Rolling back to the previous image
  works on the same data (drilled).
- Browser and phone voice: same code path and checks as before (unchanged tests pass).
- **One-time effect:** voice connections that exist *during* the upgrade were made with passes that
  carry no sealed credential; the first reconciliation after the upgrade removes them (fail
  closed, intended). Those users press *Join voice* again. Deploy when nobody is in voice.
- The app restart takes about 10 s (realtime clients and the PS5 daemon reconnect by themselves);
  the Caddy recreate about 2 s.

## 3. Verification

All runs on 2026-10-19 (Linux, Node 22.22, PostgreSQL 16, Docker 29, LiveKit 1.12.0, Chromium of
Playwright 1.56.1). CI links: see the PR.

| Check | Result |
| --- | --- |
| `pnpm lint`, `pnpm typecheck` | passed |
| Unit and integration tests | shared 8/8, **server 178/178** (voice + devices incl. device route, scope, revocation, sealed grants), web 34/34, **gateway 6/6 with a real LiveKit server** (both directions by frequency, mute, deafen, replacement, removal, refused passes) |
| Browser end-to-end with a real LiveKit server | **7/7** (incl. `voice.spec.ts`: two and three browsers, audio energy, mute, removal) — browser voice unchanged |
| PR #18 console end-to-end against this server code (real server, LiveKit, gateway behind TLS, native-IPC daemon, `akeno-party-voice` with a simulated microphone/speaker, the plugin's code, a PC participant) | **17/17** (600 Hz to the console, 300 Hz from it, mute/deafen, reconnect with a fresh pass, revocation 4003, leaving, 10 UI cycles, 12 s "game running", crash/restart) |
| **Staging drill** (`infrastructure/staging/voice-staging.mjs`): the production Compose stack (PostgreSQL, app image, Caddy with TLS, LiveKit, gateway), fresh database, upgrade from the Alpha 0.4 image with the real scripts | **35/35**, see below |

Staging drill, in order: baseline stack healthy → seeded accounts, friends, messages, party,
paired consoles → **baseline answers 404 NOT_FOUND** like production → diagnostics identify the old
server and print no secrets → `backup.sh` (dump readable by `pg_restore`) → rollback point →
`git merge --ff-only` → `caddy reload` proven insufficient → `voice-upgrade.sh preflight`, `apply`
(gateway, `VOICE_GATEWAY_URL`, app and Caddy recreated; **db and LiveKit not restarted**; verify:
route 401 for an invalid token, WebSocket 101) → **all data unchanged**, console still paired →
pass 200 (120 s, room-bound, no credentials inside) → refusals (401, 401, 400, 403
`DEVICE_SCOPE_DENIED`, 403 `NOT_MEMBER`) → gateway refuses browser pass, forged pass, `Origin` →
Bob joins on the website (Chromium) → console joins via Caddy, server and Bob see it → **600 Hz from
Bob decoded on the console; 301 Hz from the console analysed in Bob's browser** → mute, deafen →
Carol's browser: browser-to-browser audio and the console still work → duplicate session (4004) →
network drop and reconnect with a fresh pass → revocation by party removal, by removing
`parties:write`, by unpairing (4003 each) → **the PS5 voice-alpha's own code** (daemon paired over
TLS with Caddy's chain, `akeno-party-voice` decoded 602 Hz and was heard at 300 Hz) → no server
errors, no pass in the gateway log; memory: app 63 MB, LiveKit 64 MB, gateway 60 MB → rollback 1
(503 for consoles, browsers fine), 2 (gateway stopped), 3 (previous commit, image, `.env`,
Caddyfile; data written after the upgrade kept; route 404 again) → `restore.sh` restores exactly
the pre-upgrade data.

**Not verified** (and not claimed): anything on the PS5's real audio (background playback and
microphone on FW 12.20 are the purpose of the first hardware test), the production VPS's networking
(node IP, NAT hairpin, firewall), TURN, real phones on mobile networks.

## 4. Deployment (only after the owner's approval)

### Case analysis from the diagnostics

| Diagnostics show | Path |
| --- | --- |
| HEAD is on GitHub, an ancestor of the reviewed commit, no local changes | **A**: `git merge --ff-only <reviewed commit>` on the VPS |
| HEAD is on GitHub but not an ancestor (e.g. the native line) | **A'**: a merge branch is prepared on GitHub first (server code is identical, so no conflict in server files is expected), CI, then fast-forward on the VPS |
| Commits not on GitHub, or uncommitted changes | **B**: publish them first (git bundle from the VPS → the owner pushes to a new branch), merge them into this PR, CI, then path A. Nothing on the VPS is ever overwritten |
| LiveKit not running / voice keys empty | **C**: browser voice must be enabled first (keys, `LIVEKIT_URL`, `LIVEKIT_NODE_IP`, firewall 7881/tcp + 7882/udp for browsers — a firewall change needs separate approval), docs/VOICE.md |

### Steps (path A)

```sh
cd /opt/akeno
# 1. Backup (database + media), check it, copy it off the server
./infrastructure/scripts/backup.sh
docker compose exec -T db pg_restore --list < backups/akeno-<stamp>.dump | grep -c 'TABLE DATA'
gzip -t backups/akeno-media-<stamp>.tar.gz
# 2. Rollback point
docker tag akeno-network:latest akeno-network:pre-voice
cp -p .env .env.pre-voice
git rev-parse HEAD > .pre-voice-commit
# 3. Code (stops by itself if the VPS has commits the reviewed commit does not contain)
git status --short --untracked-files=no      # must be empty
git fetch origin claude/gracious-galileo-c5dxv8
git merge --ff-only <reviewed commit>
# 4. Upgrade (each changing step asks for YES)
./infrastructure/scripts/voice-upgrade.sh preflight
./infrastructure/scripts/voice-upgrade.sh build      # images only, site keeps running
./infrastructure/scripts/voice-upgrade.sh apply      # gateway, VOICE_GATEWAY_URL, app, Caddy, verify
```

If the VPS cannot fetch from GitHub, step 3 uses a bundle made on the owner's PC
(`git bundle create akeno-voice.bundle <deployed commit>..<reviewed commit>`, copied with `scp`, then
`git fetch ./akeno-voice.bundle` and the same `git merge --ff-only`).

Configuration added to `.env` by `apply`: `VOICE_GATEWAY_URL=wss://<AKENO_DOMAIN>/voice-gateway`
(optional: `VOICE_GATEWAY_MAX_SESSIONS`, default 50). Required already: `LIVEKIT_URL`,
`LIVEKIT_API_KEY`, `LIVEKIT_API_SECRET` (browser voice).

### Health checks (`voice-upgrade.sh verify`, read-only)

`/health/ready` 200; voice pass route with an invalid device token → **401** (404 = old server);
`wss://<domain>/voice-gateway` upgrade → **101**; `/livekit/` reachable; gateway `/healthz`
`{"ok":true,…}`; `docker compose ps` healthy; no error-level app log lines in the last 10 minutes.
Then by hand: sign-in, friends, messages, a party, **browser voice with two browsers**, and on the
website *Settings → Security → Paired devices*: the PS5 has the permission "Create, join and leave
parties and invite friends" (`parties:write`; otherwise the console gets 403 `DEVICE_SCOPE_DENIED`;
change the device's permissions there).

### Rollback (`voice-upgrade.sh rollback 1|2|3`)

| Level | What it does | Effect |
| --- | --- | --- |
| 1 | `VOICE_GATEWAY_URL=` and app recreated | consoles get "Console voice is not set up" (503); browsers unaffected |
| 2 | gateway stopped | connected consoles leave voice; nothing else changes |
| 3 | previous commit (detached checkout; no branch is rewritten), `akeno-network:pre-voice`, `.env.pre-voice`, gateway removed, app and Caddy recreated | the previous server exactly; the route answers 404 again; data written meanwhile is kept (no migration) |

Restart order of a full rollback: gateway stop → app (previous image) → Caddy (previous file).
PostgreSQL and LiveKit keep running throughout. `restore.sh` with the step-1 backup is only for data
damage, which no step here can cause; it was drilled in staging.

Triggers: website down or errors → level 3; browser voice broken → level 3; gateway failing or
memory/CPU problems → level 2; console authorization behaving wrongly → level 1.

## 5. Next: the first real PS5 voice test

After deployment and verification: the existing Phase 4.1 OnionHEN host (unchanged,
`toolbox_target_mode=root`), the existing native-IPC daemon (interface 3), plugin 1.20 and
`akeno-party-voice.elf` from PR #18. Tests A–F and the classification rules:
[server-voice/PS5-ERSTTEST-DE.md](server-voice/PS5-ERSTTEST-DE.md). Background audio and microphone
capture on FW 12.20 remain unverified until the owner hears and is heard.

## 6. Open items

1. The read-only diagnostics on the VPS (owner) — they decide path A/A'/B/C.
2. The owner's approval for the deployment (and, for path C, for the firewall change).
3. This environment's network policy blocks the VPS; with SSH access or a network rule for the
   host, the diagnostics could be run from here (read-only).
