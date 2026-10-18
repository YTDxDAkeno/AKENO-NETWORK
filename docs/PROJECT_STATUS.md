# Project status

_Last updated: 2026-10-18 (AKENO Native 1.0.0-beta.1; see [HANDOFF_PS5_NATIVE.md](HANDOFF_PS5_NATIVE.md))._ Earlier: 2026-10-14 (session 5, part 2). This file is the source of truth for continuing work
across sessions. Do not mark anything complete that has not actually been run.

Branches: **`claude/vibrant-mendel-v55zqf`** (session 5; based on `claude/elegant-wright-4qi8gh`
@ `9dcc192`, fast-forward, history preserved). `claude/elegant-wright-4qi8gh`, Branch A
(`claude/adoring-johnson-6re0yl`), Branch B (`claude/epic-bell-s1psav`) and PR #1
(`chatgpt/ps5-http-backend`) are unchanged. `main` is untouched. Earlier decisions:
[BRANCH_RECONCILIATION.md](BRANCH_RECONCILIATION.md).

**Production:** the user reports `af571eb` as deployed. That commit is **not on GitHub** (the VPS
history was reconciled locally), and this container cannot reach the VPS (the egress proxy
answers 403 for `85.190.112.10.sslip.io`; no SSH). Nothing was deployed. Before an upgrade the
VPS-only commits must be pushed or merged (procedure in the session-2 section below).

## Summary

| Area | Status |
| --- | --- |
| AKENO NETWORK web platform | Implemented and tested |
| **PS5 console app Alpha 0.2** | **Ran on a real PS5, firmware 12.20** (user report with screenshots): launch, browser page, HTTPS, pairing, realtime, friends/messages, notifications while gaming, game detection with full titles, session duration, switching games, recently played visible to a second account, interactive message notifications opening the right conversation |
| **Alpha 0.3** (pictures, banners, themes, badges, game artwork, redesign, media backups) | **Release candidate**: implemented; all automated checks pass locally (session 5 below); not deployed |
| Alpha 0.3 console-page images (profile pictures, artwork, local console icons) | Implemented, tested on Linux; **not run on a PS5** |
| **Alpha 0.4** (voice parties, desktop + mobile browsers) | **Implemented; tested against a real LiveKit server in Chromium** (session 5, part 2); real two-device test, phones, Firefox/Safari still open; not deployed |
| Alpha 0.4 PS5 audio | Research done; experimental **audio lab** ELF (prototypes A and C) builds and passes the import gate; **never run on a PS5**; microphone capture blocked (no SDK interface) |
| **AKENO NETWORK Native (PS5) 0.1** | **Ran on a real PS5, firmware 12.20** (foreground, 1920×1080, DualSense, audible audio, account) — [results](PS5_NATIVE_HARDWARE_RESULTS_FW12.20.md) |
| AKENO Native 0.2 preview | Ran on the owner's PS5, firmware 12.20 (real friend profiles and game activity; owner report, build ID not recorded); not fully tested |
| **AKENO Native 1.0.0-beta.1** (messages + controller keyboard, party, inbox, settings, verified artwork; daemon IPC protocol 2) | **Beta**: implemented, PC tests and real-server end-to-end tests pass, PS5 builds gated and reproducible; **not run on a PS5** — branch `claude/affectionate-cannon-s38t7k`, [handoff](HANDOFF_PS5_NATIVE.md), [test plan](HARDWARE_TEST_PLAN_PS5_NATIVE_1.0.0-beta.1.md) |
| **AKENO Native 1.1.0-beta.1 — two-way party voice** (voice gateway, device voice pass, microphone via libSceAudioIn) | **Beta**: implemented, PC end-to-end tested with a real LiveKit server (both directions); gateway not deployed; **not run on a PS5**, microphone untested on 12.20 — branch `claude/native-voice`, [PS5_VOICE.md](PS5_VOICE.md) |
| In-Game Companion (OnionHEN Toolbox plugin) | **Experimental**: PC-tested, PS5 build gated; needs OnionHEN (owner decision); never run on a PS5 — [IN_GAME_ACCESS.md](IN_GAME_ACCESS.md) |
| Native Game Base / PSN, Control Center card, overlay | **Unsupported** (no safe documented mechanism); in-game access only through a third-party host (above) |
| Production VPS | Not verified, not deployed (network policy, see above) |

## Session 5, part 2 — Alpha 0.4 voice parties

### Built

| Area | Where |
| --- | --- |
| SFU choice: LiveKit 1.12.0 (Apache-2.0) after comparing mediasoup and Janus | docs/VOICE.md |
| Voice authorization: room per party (`auto_create` off), short-lived microphone-only tokens bound to room + user, membership/block/account/session checks at token, join (signed webhook) and every 30 s, immediate removal on leave/kick/close/block/suspension/account deletion/session sign-out, failure isolation (503 only for voice) | `apps/server/src/modules/voice/`, hooks in parties, friends, administration, auth |
| Config: `LIVEKIT_URL`, `LIVEKIT_API_URL`, `LIVEKIT_API_KEY`, `LIVEKIT_API_SECRET`, `VOICE_TOKEN_TTL_SECONDS` (all-or-none, validated); CSP `connect-src` adds the LiveKit origin | `apps/server/src/config/env.ts`, `app/build-app.ts` |
| Web voice client: join/leave, mute, deafen, push-to-talk (key + hold button), voice activity gate (AudioWorklet), per-person volume and mute-for-me (unsubscribe), speaking/mute/quality indicators, reconnection with fresh tokens, autoplay unlock, microphone interruption recovery, one connection per account, voice bar across pages, join from a party invitation | `apps/web/src/features/voice/`, `features/parties/PartiesPage.tsx`, `features/invitations/InvitationsList.tsx`, `app/layouts/AppLayout.tsx` |
| Settings → Voice & Audio: devices, volumes, modes, sensitivity on the level meter, PTT key capture, microphone test with hear-yourself, EC/NS/AGC, test sound, disconnect | `apps/web/src/features/settings/VoiceSettings.tsx` |
| Compose `voice` profile (LiveKit non-root, read-only, 512 MB), Caddy `/livekit` route, `.env.example` section, CI starts LiveKit for the voice E2E and validates the voice profile | `compose.yaml`, `infrastructure/caddy/Caddyfile`, `.github/workflows/ci.yml` |
| No database migration needed (voice state is in memory; preferences in the browser) | — |

### Key decisions and findings

1. **LiveKit** over mediasoup (we would write signalling/auth/TURN ourselves) and Janus (GPL,
   mixing is CPU heavy). Egress/recording is not deployed.
2. **Remote audio through `<audio>` elements, not Web Audio** (`webAudioMix: false`): the
   browser E2E showed the Web Audio path bypasses what Chrome's echo canceller hears; volume is
   therefore capped at 100 %, and "mute for me"/deafen unsubscribe (works on iOS too).
3. **LiveKit accepts tokens ~60 s past expiry** (measured). Expiry is not the revocation
   mechanism; every join is re-checked through the signed webhook.
4. Voice tokens are bound to the requesting **browser session**: signing that session out ends
   the voice connection; other sessions of the same user are unaffected.
5. The E2E (locally and on CI) found real bugs, all fixed: the CSP blocked the inlined
   AudioWorklet (`data:`; now emitted as a file); the party watcher dropped a voice connection
   made right after accepting an invitation (stale "current party" data); the microphone test
   created its audio context outside the click (meter stuck at 0 on CI, would affect Safari);
   static files (including the worklet) counted toward the per-user rate limit, so a
   rate-limited worklet fetch could stall joining (static files are now exempt; joining falls
   back to the plain microphone if processing cannot load); one voice change refetched the
   same party queries two or three times (now coalesced).
6. Test-only: Chromium's default fake microphone (a 20 ms beep every 500 ms) is too sparse for
   LiveKit's speaker detection; the E2E feeds a generated speech-like WAV instead.

### Tests executed (2026-10-14, Linux container, Node 22.22, PostgreSQL 16, Chromium, LiveKit 1.12.0)

| Check | Result |
| --- | --- |
| `pnpm lint`, `pnpm typecheck` | passed |
| `pnpm test` | shared 8/8, **server 167/167** (voice 15), **web 34/34** (voice 7) |
| Browser E2E with real LiveKit (`E2E_LIVEKIT_URL` set) | **7/7** incl. `voice.spec.ts` (two/three browsers, real audio energy in WebRTC stats) |
| Browser E2E without LiveKit | 6/6 passed, voice spec skipped |
| Compose `voice` profile with the production image | two browsers exchanged audio over UDP 7882 |
| LiveKit restart while talking | both clients reconnected with fresh tokens, audio resumed after ~4 s |
| Load: 6 / 8 participants all talking | LiveKit 8 % / 12 % of a core, 66 / 74 MB; 32 kbit/s up, 150 / 209 kbit/s down per participant |

| Upgrade check from Alpha 0.2 (`9dcc192`) incl. rollback, with the Alpha 0.4 server | passed |
| CI on GitHub | **All green on `c1e2091`** (run 38557227535 + PS5 run 38557227548): lint, typecheck, unit/integration, browser E2E with a real LiveKit server, C client + audio lab, Docker image, Compose incl. voice profile, PS5 builds and import gates. Earlier runs failed in the voice E2E and led to real fixes (below) |

Not executed: a real two-device audio test, real phones, Firefox/Safari, TURN, the production
VPS (not reachable from this container: its CPU/RAM/bandwidth/firewall were not inspected —
checklist in VOICE.md).

### PS5 audio research (prototypes A–E)

Details and the hardware test plan: [PS5_AUDIO_RESEARCH.md](PS5_AUDIO_RESEARCH.md).

| Report | Result |
| --- | --- |
| Compiles | Release ELF and separate audio lab ELF (`make EXPERIMENTAL_AUDIO=1`), pinned SDK `f5d40f0`, `-Werror`; both pass `check-imports.sh`; identical dynamic imports (96); the release ELF's code is byte-identical to `6e37eb2` (only the page script differs) |
| Supported by known interfaces | Output only: SDK `libSceAudioOut` stub + declarations from ps5-payload-dev/SDL's PS5 audio driver. No `libSceAudioIn` in the SDK, no PS5 capture implementation found |
| Tested on Linux | Audio lab with simulated audio: 5 integration scenarios incl. Chromium (also in CI) |
| Verified on PS5 hardware | Nothing |
| Unsupported | Microphone capture (B), device enumeration, PS5 in a party (D), audio during games (E), rest mode, 13.60 |

A finding of the import gate: the first audio lab version imported five libc functions never
proven on a console (`opendir`, `readdir`, `closedir`, `qsort`, `sin`), which could have stopped
the payload from starting; rewritten without them (stat over a fixed list, oscillator by
recurrence).

### Next steps (Alpha 0.4)

- **User approvals needed:** production upgrade (Alpha 0.3 + 0.4, no new migration for 0.4);
  enabling voice (keys, `LIVEKIT_NODE_IP`, firewall 7881/tcp + 7882/udp — your change); any PS5
  audio lab test (plan in PS5_AUDIO_RESEARCH.md).
- **Real-device tests still required before calling voice verified:** PC + Android phone and
  PC + iPhone on different networks in one party (join, hear both ways, mute, speaking ring,
  leave releases the microphone, phone screen lock behaviour), Firefox and Safari desktop, a
  restrictive network (UDP blocked → TCP 7881).
- **PS5 hardware tests (each needs approval):** audio lab prototypes A and C on 12.20 from the
  home screen; later, only if C works: tone during a game (E), 13.60.
- Optional later: TURN (LiveKit built-in, second IP or layer-4 forwarding), screen sharing
  (design notes in VOICE.md), PS5 voice via a server-side bridge if capture becomes possible.

## Session 5 — Alpha 0.3

### Built

| Area | Where |
| --- | --- |
| Media storage: content-sniffed uploads, bounded decoding, EXIF orientation + metadata removal, WebP variants + JPEG fallback, random IDs, atomic writes, quotas, limits, orphan sweep, authenticated immutable serving, failure isolation | `apps/server/src/modules/media/`, docs/MEDIA_STORAGE.md |
| Profile pictures and banners, appearance (themes, custom accent, banner overlay, featured badges) | `apps/server/src/modules/users/`, `apps/web/src/features/settings/ProfileCustomization.tsx`, `features/profile/ImageCropDialog.tsx`, docs/PROFILES.md |
| Theme token system with enforced contrast | `packages/shared/src/themes.ts`, `apps/web/src/lib/theme.ts`, `styles/index.css` |
| Badges: derived Administrator, admin grant/revoke with audit, no self-granted authority badges, operator CLI `grant-badge`/`revoke-badge` | `apps/server/src/modules/badges/`, `packages/shared/src/badges.ts`, `apps/web/src/components/BadgeIcon.tsx`, `features/admin/BadgesDialog.tsx` |
| Game catalogue: canonical games, verified title-ID mappings, icon/cover/banner with provenance, detected-titles report, exact-name matching for favorites/sessions, catalogue cache | `apps/server/src/modules/games/`, `features/admin/GamesPanel.tsx`, docs/GAME_ARTWORK.md |
| Artwork everywhere: presence, history, game sessions, favorites, parties, home, profile | `apps/web/src/components/GameArt.tsx`, `GameActivity.tsx` |
| Redesign: profile page, party page, home "friends playing now", sessions, calmer tokens | `features/profile/ProfilePage.tsx`, `ProfileHeader.tsx`, `features/parties/PartiesPage.tsx`, … |
| Console page: profile pictures and artwork through a bounded media fetch, local console icons (read-only), data: URLs (CSP unchanged) | `clients/ps5/src/client.c` (`akeno_fetch_media`), `app/app_routes.c`, `app/game_detect.c`, `app/web/app.js` |
| Migration `0004` (additive, data migration of curated names) | `apps/server/src/database/migrations/0004_alpha03_profiles_media_games.sql` |
| Docker media volume, media-aware backup/restore, upgrade check for 0.3 | `compose.yaml`, `infrastructure/docker/Dockerfile`, `infrastructure/scripts/` |

### Key decisions

1. **No external artwork provider.** PSN endpoints are excluded; IGDB's terms (commercial use
   needs a partnership, attribution, unclear image redistribution rights) and the lack of
   verifiable title-ID mapping rule out automatic use; artwork comes from administrators with
   recorded source and licence. Console-local icons are shown on the console page only, never
   uploaded. (GAME_ARTWORK.md)
2. **Title IDs map to games only through verified mappings**; names never attach artwork.
3. **Media on a Docker volume**, not object storage (fits the VPS; one service owns all file
   access). Media IDs are capabilities, only disclosed through privacy-checked responses.
4. **Client crops, server re-validates**: only the crop is uploaded (handles phone photos that
   the server's decoder does not support, e.g. HEIC converted by the browser), and the server
   re-encodes everything anyway.
5. **Console page images as data: URLs** fetched by the payload with its device token: keeps the
   console page's strict CSP and the per-launch secret header requirement unchanged; no new
   PS5 system imports (only libc file reads and the existing transport).
6. A browser-side signature check before decoding avoids Chrome's CSP-blocked `blob:` fetch for
   non-image files (found by the E2E test) instead of loosening the CSP.

### Tests executed (2026-10-14, Linux container, Node 22.22, PostgreSQL 16, Chromium 141)

| Check | Result |
| --- | --- |
| `pnpm lint`, `pnpm typecheck` | passed |
| `pnpm test` | shared 8/8, **server 151/151** (35 new: media-profile 21, games-badges 14), web 27/27 |
| Browser E2E (production builds) | **6/6** (new: profiles-artwork; mobile-parties updated for the new party UI) |
| C client + console app (`pnpm test:client`) | 496 checks × 2 builds under ASan/UBSan (8 new), CLI and console E2E passed incl. the new image scenario and the Chromium console-page scenarios |
| Upgrade check from Alpha 0.2 (`9dcc192`) with rollback | passed |
| Docker image build (node:22-alpine), sharp on musl | passed |
| Backup drill with the production image (upload → backup → restart → delete volumes → restore) | passed |

Not executed: PS5 build of the Alpha 0.3 console app (needs the pinned SDK; CI builds it on
push), anything on PS5 hardware for Alpha 0.3, the production server.

### Next steps (Alpha 0.3)

- User: approve the production upgrade (RELEASE_NOTES_ALPHA_0.3.md → Upgrading).
- Hardware: check profile pictures and game icons on the console page (12.20).
- Optional: animated avatars (with frame/size limits), an administrator-triggered provider
  lookup if a suitably licensed provider is chosen.

## Session 4 — Alpha 0.2

### What was done (commits on `claude/elegant-wright-4qi8gh`)

| Commit | Content |
| --- | --- |
| `2a2ad2d` | **Green build restored.** TS test typing; host C format issues; `game_detect.c`/`activity.c` in the PS5 build; `sceNotificationSend` no longer linked (run-time `dlopen`, opt-in); `sceKernelGetAppInfo` weak; import gate against Alpha 0.1's proven import set; layout-voting detection; activity worker wired into the app; one-time links; recent-message inbox. GitHub Actions: CI run 38496226143 **green**, PS5 build run 38496226211 **green** |
| `7efc1cc` | Server: migration `0003` (excluded games, playtime visibility, private sessions while invisible), `PATCH /devices/:id` reauthorization; website settings; console worker states; 10 console end-to-end scenarios |
| `67bab44` | Console page: Change Server fix, Games tab, banner, quick-open, drafts, diagnostics, ES2017 lint guard; browser tests |
| `e4e1a28` | Release engineering and docs: PS5 workflow builds an `alpha-0.2-rc` artifact with the **verified Alpha 0.1 fallback** (rebuilt from `592c23a`, SHA-256 must equal `35f393c6…6b65`); `infrastructure/scripts/upgrade-check.mjs`; "server update needed" state; 12.20 marked hardware-tested for Alpha 0.1 in the caps table; docs |

### Key findings (with evidence)

1. **Why the PS5 link failed and why simply linking was wrong.** `sceNotificationSend` lives in
   `libSceNotification`, which the Makefile did not link. But the SDK runtime
   (`crt/rtld_payload.c`) aborts the whole payload if a `DT_NEEDED` library or a strong symbol
   fails to resolve — linking it could have broken the proven start-up. It is now loaded with
   `dlopen`/`dlsym` (SDK `samples/hello_dlfcn` pattern) only when the user enables the experiment.
2. **Reproducible build.** Rebuilding Alpha 0.1 (`592c23a`) here gave exactly the CI SHA-256 of
   the hardware-tested ELF. CI now re-verifies this on every PS5 build.
3. **Imports.** Alpha 0.2's dynamic imports = Alpha 0.1's + weak `sceKernelGetAppInfo` (a new
   strong `strnlen` import was removed). Enforced by `console/reviewed-imports.txt`.
4. **`sceKernelGetAppInfo` layout.** SDK samples disagree (title ID at offset 20 vs 16). Resolved
   at run time by voting on the console's own data; conflicts report nothing. Raw probes appear in
   Diagnostics for the hardware test.
5. **Foreground.** No verified API (`sceSystemServiceGetAppFocusedAppStatus` has a stub but no
   sample or structure). AKENO reports "game process running", never "in focus".
6. **Privacy gap found and fixed:** a session played while invisible used to appear in friends'
   history after it ended. Such sessions are now private.
7. **Second bug found and fixed:** an incoming message re-rendered the chat and erased a
   half-typed reply on the console page.

### Tests executed (2026-10-14, Linux container)

lint (incl. ES2017 guard) and typecheck passed; shared 8/8, server 116/116, web 27/27; website
E2E 5/5 (two consecutive runs); C unit 488 checks × 2 transports under ASan/UBSan, Valgrind clean;
CLI E2E 24/24; console E2E 49/49 (incl. 4 Chromium scenarios); PS5 ELF + import gate pass;
Alpha 0.1 rebuild byte-identical; upgrade check from Alpha 0.1 data (seed and `--from-dump`)
passed, old server works on the upgraded database. Details: [TESTING.md](TESTING.md).
GitHub Actions on `e4e1a28` (release candidate): **CI run 38500880377 green** (lint, typecheck,
unit/integration, website E2E, C unit + end-to-end incl. Chromium, Valgrind, Docker) and **PS5
console build run 38500880488 green** (Alpha 0.1 fallback verified byte-identical). The previous
checkpoint `67bab44` had failed only in the website E2E: `mobile-parties` hit the per-IP
registration limit after the new game-activity test registered two more users; fixed in
`e4e1a28` by reusing users. Artifact `akeno-network-ps5-alpha-0.2-rc` (ID 11865294379, kept 90
days):

| File | SHA-256 |
| --- | --- |
| `akeno-network-ps5.elf` (Alpha 0.2 RC) | `06765c7bce57f0eac8423a7b9922e45be8fbe971177b4438a9643073094fb217` |
| `akeno-network-ps5.debug.elf` | `1e0c6394d19e8f57ccf6980df5ea1096822b13ca1bd7743d1837e4f514b514e5` |
| `akeno-ps5-diagnostic.elf` | `9ddfc23a425261b30d316eb0c7078d760aa3d7fc9e8f53793b3f937057255cfe` |
| `fallback-alpha-0.1/akeno-network-ps5-alpha-0.1.elf` | `35f393c64213410585b40262ce203423ec5c1ad66dda97e5919b9b31948b6b65` |

The Alpha 0.2 ELF was also built locally from the same commit with the same SHA-256
(reproducible).

Not executed: anything on PS5 hardware for Alpha 0.2; firmware 13.60; the production server.

### Next steps (for the user)

1. **Approve and test Alpha 0.2 on 12.20**: download `akeno-network-ps5-alpha-0.2-rc` from PS5
   console build **run 38500880488** (commit `e4e1a28`; ELF SHA-256 `06765c7b…b217`, see the table
   above). Later runs contain the same code but embed their own commit ID, so their ELF has a
   different checksum; approve the one you actually test. Verify `SHA256SUMS`, follow
   [HARDWARE_TEST_PLAN_ALPHA_0.2.md](HARDWARE_TEST_PLAN_ALPHA_0.2.md). The verified Alpha 0.1 ELF
   is in the same download as the fallback.
2. **Server update (needed for game activity)**, from your PC or the VPS, never with
   `docker compose down -v`:
   1. On the VPS: `git status`, `git log -1` — record the deployed commit; reconcile any VPS-only
      commits first (procedure in the session-2 section below).
   2. Back up: `./infrastructure/scripts/backup.sh`; copy the dump off the server.
   3. Check the backup and the upgrade on a disposable database (PC or VPS, not the production
      database): `pnpm install && pnpm build && UPGRADE_DATABASE_ADMIN_URL=… node
      infrastructure/scripts/upgrade-check.mjs --from-dump <backup.dump>` (add `--old <checkout of
      the deployed commit>` to also check rollback).
   4. Deploy this branch (VPS_DEPLOYMENT.md → Upgrade; migrations `0002`, `0003` are additive),
      then check `https://<server>/health/ready` and sign in.
   5. On the website: Settings → Security → Paired devices → **Allow game activity** for the
      console paired with Alpha 0.1.
3. Report hardware results; they go into FIRMWARE_COMPATIBILITY.md. Then 13.60.

### Next steps (for the next session)

- After hardware results: fix what fails; if the layout votes show one offset consistently,
  keep the voting (it costs nothing) but note the confirmed layout per firmware.
- If button notifications work on hardware, consider making them the default (only with the
  user's agreement); otherwise remove the experiment.
- Optional: game names for common titles (admin UI exists), artwork from a licensed source.
- Ask the user to allow the VPS host in this environment's network settings (or deploy from
  their PC) before any deployment work.

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
8. **Fallback without the browser**: if the browser cannot be launched and a server is already
   configured (`settings.json` via FTP), the app starts pairing itself and delivers the code as a
   notification, so pairing and notifications work without the page.
9. **Worker threads get an explicit 1 MiB stack** (the default stack size for PS5 payload threads
   is not documented; TLS handshakes run on these threads).

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
| E2E, `akeno-cli-mbedtls` + console app `akeno-app` (incl. two Chromium runs, keyboard only, browser-launch failure fallback) | 39/39 |
| PS5 build with SDK `f5d40f0`, `-Werror` | builds; 1.1 MB stripped PIE; import gate passes (negative test verified) |

GitHub Actions on commit `592c23a`: **CI run 38485099581 green** (lint, typecheck, tests, web E2E,
C client + console app E2E with Chromium, Valgrind, Docker) and **PS5 console build run
38485099391 green**. Its artifact `akeno-network-ps5-alpha-0.1` (expires 2026-11-12) contains:

| File | SHA-256 |
| --- | --- |
| `akeno-network-ps5.elf` | `35f393c64213410585b40262ce203423ec5c1ad66dda97e5919b9b31948b6b65` |
| `akeno-network-ps5.debug.elf` | `d501ebc2989580453999c1a806796550fd3389050c914d5be908f0a78bb6afa7` |
| `akeno-ps5-diagnostic.elf` | `9ddfc23a425261b30d316eb0c7078d760aa3d7fc9e8f53793b3f937057255cfe` |

Not executed: anything on PS5 hardware; the PS5 browser; firmware 13.60; the production server.

### Open questions that only hardware can answer (see the test plan in PS5_CLIENT.md)

_Answered by the Alpha 0.1 hardware test on 12.20 (recorded in session 4): 1 yes, 2 yes, 5 yes;
4 implied (pairing completed, so the credential was stored); 3 and 6 not reported._

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
