# SharePlay — integration notes (for merging with the PS5 profile / quick access / AudioLab branch)

## Branches and commits

| | |
| --- | --- |
| Feature branch | `feature/akeno-party-shareplay` (identical mirror: `claude/youthful-cori-gp8duz`, the session branch it was developed on) |
| Base | `claude/vibrant-mendel-v55zqf` @ **`ae948512475a6752fb4cebd70234893e037044e1`** (Alpha 0.4 release candidate, CI green). Chosen after inspecting every remote branch: it contains `claude/elegant-wright-4qi8gh` (Alpha 0.2) and `claude/adoring-johnson-6re0yl`; `main` only has the initial commit; `chatgpt/ps5-http-backend` and `claude/epic-bell-s1psav` are older side branches. |
| History | Linear on top of the base, no merges, no rebases of other branches, nothing force-pushed. |
| Draft PR | into `claude/vibrant-mendel-v55zqf` — **not to be merged before both branches are ready**. |

Important commits (oldest first):

| Commit | What |
| --- | --- |
| `65bd581` | Server + shared contracts/protocol; tests |
| `8eabbe6` | Web: broadcast, viewer, Player 2 UI; E2E |
| `c870a87` | PS5 research, console diagnostic receiver, C tests |
| `8615b53`, `1ff6006` | Architecture + hardware test docs, measurement script, E2E hardening, lint fix |
| (this commit and later) | Integration notes, status |

## New files (no conflict possible)

```
packages/shared/src/shareplay/            contracts.ts, controller-protocol.ts (+ .test.ts), quality.ts, schemas.ts, index.ts
apps/server/src/modules/shareplay/        service.ts, input-relay.ts, routes.ts, capabilities.ts
apps/server/test/                         shareplay.test.ts, fake-livekit.ts
apps/web/src/features/shareplay/          stream-controller.ts, SharePlayProvider.tsx, SharePlayPanel.tsx, SharePlayBar.tsx,
                                          PlayerTwoPanel.tsx, player-two.ts, queries.ts
apps/web/tests/unit/shareplay.test.ts, apps/web/tests/e2e/shareplay.spec.ts
clients/ps5/app/                          shareplay_rx.c, shareplay_rx.h, web/shareplay.js
clients/ps5/tests/unit/test_shareplay.c
infrastructure/scripts/shareplay-measure.mjs
docs/SHAREPLAY_*.md, docs/perf/shareplay-container-2026-10-16.json
```

## Modified shared files (every change, with merge advice)

All edits are additive (new lines next to existing ones); nothing existing was renamed,
reformatted or removed.

| File | Change | Conflict risk / how to resolve |
| --- | --- | --- |
| `packages/shared/src/index.ts` | `export * from './shareplay/index.js'` at the end | Low. Keep both sides' exports. |
| `packages/shared/src/contracts/realtime.ts` | import `SharePlayInputRelay`; two union members `shareplay.changed`, `shareplay.input` | Low–medium if the other branch adds events: keep all union members. |
| `packages/shared/src/contracts/errors.ts` | 11 codes after `VOICE_UNAVAILABLE` (SHAREPLAY_UNAVAILABLE … GRANT_CHANGED) | Low. Keep both lists. |
| `apps/server/src/config/env.ts` | import from `@akeno/shared`; 5 `SHAREPLAY_*` variables; 2 validation rules; `AppConfig.shareplay`; mapping in `loadConfig` | Medium if the other branch adds settings in the same places: keep both blocks. |
| `apps/server/src/app/context.ts` | import + `shareplay: SharePlayService` + construction after voice | Low. |
| `apps/server/src/app/build-app.ts` | register `sharePlayRoutes`; 30 s reconciliation timer; clear it and `ctx.shareplay.shutdown()` in `preClose` | Low. |
| `apps/server/src/modules/voice/service.ts` | **one line**: webhooks for non-voice rooms are handed to `ctx.shareplay.handleWebhookEvent(event)` (LiveKit has a single webhook URL) | Low. Keep it; without it SharePlay never sees joins. |
| `apps/server/src/modules/parties/service.ts` | `ctx.shareplay.onPartyClosed` (2×) and `onMemberLeft` next to the voice calls | Low–medium: keep next to the voice effects. |
| `apps/server/src/modules/friends/service.ts` | `ctx.shareplay.onBlock` after `voice.onBlock` | Low. |
| `apps/server/src/modules/administration/service.ts` | `ctx.shareplay.removeUserEverywhere` after the voice call | Low. |
| `apps/server/src/modules/auth/service.ts` | `ctx.shareplay.onSessionsRevoked(...)` after each of the 4 voice calls | Low. |
| `apps/server/src/modules/devices/scopes.ts` | `GET /parties/:id/shareplay`, `GET /shareplay/status`, `POST /parties/:id/shareplay/receiver` (opt-in to display Player 2 input) → `parties:read` | Low. If the other branch adds console routes, keep both. |
| `apps/web/src/app/router.tsx` | `<SharePlayProvider>` inside `<VoiceProvider>` around `<AppLayout>` | Low. |
| `apps/web/src/app/layouts/AppLayout.tsx` | `SharePlayBar` above `VoiceBar` (sidebar and phone bar container now `flex-col gap-2`), `SharePlayPartyWatcher` | **Medium** — the other branch may restyle the layout. Keep the bar next to the voice bar and the watcher component. |
| `apps/web/src/app/providers/RealtimeProvider.tsx` | `case 'shareplay.changed'` (invalidate + window event), `case 'shareplay.input'` (ignored) | Low. |
| `apps/web/src/features/parties/PartiesPage.tsx` | `<SharePlayPanel party={party} />` after `<PartyVoicePanel>` | Low–medium if the party page is redesigned: keep the panel below the voice panel. |
| `apps/web/src/hooks/useGamepadNavigation.ts` | pauses while `isPlayerTwoActive()` (a controller used as Player 2 must not click around the app) | **Medium** — the other branch works on controller quick access. Keep this guard in whatever polling loop drives UI navigation. |
| `clients/ps5/app/app.c` | in `rt_on_event`: `shareplay.input` goes to `akeno_sp_rx_apply` under `app->lock`, never into the event ring | **Medium** (AudioLab/quick-access may touch `rt_on_event`). Keep the early return before `app_push_event`. |
| `clients/ps5/app/app_internal.h` | `#include "shareplay_rx.h"`, field `akeno_sp_rx shareplay` before the event ring, `extern akeno_ui_shareplay_js` | Medium (struct grows on both branches): keep both fields. |
| `clients/ps5/app/app_routes.c` | `GET /shareplay.js`, `route_shareplay` + `GET /api/shareplay` | Medium: keep both sides' routes. The proxy allow-list was **not** changed (`/parties` already covers it). |
| `clients/ps5/app/ui_assets.c` | `AKENO_EMBED_FILE(akeno_ui_shareplay_js, …)` | Low. |
| `clients/ps5/app/web/index.html` | a `SharePlay` tab button; `<script src="/shareplay.js">` before `app.js` | **Medium–high** (profile UI). Keep the tab and the script tag. |
| `clients/ps5/app/web/app.js` | `viewSharePlay()` (5 lines) and `shareplay: viewSharePlay` in the `views` map | **Medium–high** (profile UI, quick access). Keep the function and the map entry; all other SharePlay page code lives in `shareplay.js`. |
| `clients/ps5/CMakeLists.txt`, `clients/ps5/console/Makefile` | add `app/shareplay_rx.c` (app sources, unit tests) and `test_shareplay.c`; `shareplay.js` as an asset dependency | Medium if AudioLab adds sources: keep both. |
| `clients/ps5/tests/unit/test_main.c` | `run_shareplay_tests()` | Low. |
| `clients/ps5/tests/integration/run.mjs` | a fake LiveKit room API + `LIVEKIT_*`/`SHAREPLAY_*` in the server env; one new console-app step; `waitFor` awaits its predicate (backward compatible) | Medium: keep the env additions and the step. |
| `infrastructure/scripts/e2e-server.mjs` | `SHAREPLAY_ENABLED`/`SHAREPLAY_REMOTE_INPUT_ENABLED` = true only when `E2E_LIVEKIT_URL` is set | Low. |
| `compose.yaml`, `.env.example` | pass-through and documentation of the `SHAREPLAY_*` variables (defaults off) | Low. |

Not changed: database schema, migrations, `docs/PROJECT_STATUS.md`, `docs/VOICE.md`, the voice
token grants, `.github/workflows/*`, the PS5 import allow-lists, `platform_ps5.c`/`platform.h`.

## API, schema, permissions, configuration

- **API:** new routes only (see `SHAREPLAY_ARCHITECTURE.md`); existing responses unchanged
  (`PartyDto` is untouched — SharePlay state has its own endpoint).
- **Realtime:** two new server events. `shareplay.changed` is low-rate (older console apps show it
  as a page event and refresh). `shareplay.input` (up to ~120/s) is sent **only** to console
  apps of the host that opted in with `POST /parties/:id/shareplay/receiver` (device token,
  renewed every 30 s by the SharePlay tab, expires after 90 s) — so the Alpha 0.2/0.4 console
  app, which would queue it as page events, never receives it.
- **Database:** no schema change, **no migration**.
- **LiveKit permissions:** voice unchanged. SharePlay tokens are issued for a different room:
  host = `screen_share` + `screen_share_audio`, no subscribe; viewer = subscribe only; nobody
  `canPublishData`. LiveKit config is unchanged (same keys, same webhook URL, `auto_create: false`).
- **Environment variables:** `SHAREPLAY_ENABLED` (false), `SHAREPLAY_MAX_VIEWERS` (4),
  `SHAREPLAY_MAX_QUALITY` (720p60), `SHAREPLAY_REMOTE_INPUT_ENABLED` (false),
  `SHAREPLAY_GRANT_MAX_MINUTES` (120). With the defaults, a merged build behaves like Alpha 0.4
  plus a one-line "SharePlay is not enabled on this server" note on the party page.
- **Feature flags:** the two switches above; the console's SharePlay tab is display-only.

## Merge procedure (suggested)

1. Finish and push the profile / quick-access / AudioLab branch. Do not rebase either branch.
2. Create an integration branch from `claude/vibrant-mendel-v55zqf` (or from the other branch's
   head), then `git merge --no-ff` the other branch, then `git merge --no-ff
   feature/akeno-party-shareplay`.
3. Resolve conflicts with the table above — in every case **keep both sides**; SharePlay's
   additions are self-contained lines.
4. Run: `pnpm install --frozen-lockfile && pnpm lint && pnpm typecheck && pnpm test`;
   `pnpm test:e2e` with a LiveKit server (`docs/TESTING.md`), which also runs
   `shareplay.spec.ts`; `pnpm test:client`; the PS5 workflow (import gate must stay green and
   list the same system imports as before plus whatever the other branch adds).
5. Check by hand: the console page's tabs (profile + SharePlay), controller quick access while a
   SharePlay Player 2 is active on the website (the console's quick access is unaffected; the
   website pauses its own gamepad navigation only while sending as Player 2).

## CI status

Green on `65bd581`, `8eabbe6`, `c870a87` (CI: lint, typecheck, unit/integration, browser E2E
with a real LiveKit 1.12.0 including `shareplay.spec.ts`, C client + console app incl. the
SharePlay step, Docker image, Compose; PS5 console build with the pinned SDK and import gate).
`8615b53` had a lint error in the measurement script, fixed in `1ff6006`. See the PR for the
latest runs.

## Hardware status and blockers

Nothing verified on hardware (see `SHAREPLAY_STATUS.md`, `SHAREPLAY_HARDWARE_TEST.md`).
Blocked: native PS5 capture and a real Player 2 controller (no verified, safe mechanism —
`SHAREPLAY_PS5_RESEARCH.md`). Production: not deployed, not touched.
