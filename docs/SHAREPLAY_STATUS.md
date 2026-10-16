# SharePlay status (Alpha 0.5, experimental)

_Work in progress. This file is the source of truth for resuming SharePlay work._

- Branch: `claude/youthful-cori-gp8duz` (mirrored to `feature/akeno-party-shareplay`)
- Base: `claude/vibrant-mendel-v55zqf` @ `ae948512475a6752fb4cebd70234893e037044e1` (Alpha 0.4, CI green)

## Milestone 1 — shared contracts and server authorization (this commit)

Implemented: shared contracts and binary controller protocol (`packages/shared/src/shareplay/`),
server module (`apps/server/src/modules/shareplay/`): sessions, role-scoped LiveKit tokens in a
room separate from voice, webhook handling, reconciliation, Player 2 request/approve/deny/revoke
with epochs, authenticated controller WebSocket relay to the host's monitor and paired console,
automatic revocation (leave, kick, close, block, suspension, sign-out, host/guest disconnect,
expiry). Feature flags `SHAREPLAY_ENABLED`, `SHAREPLAY_REMOTE_INPUT_ENABLED` (both off).

Tests: shared 20 (12 new), server 188 (21 new in `apps/server/test/shareplay.test.ts`), web 34.

Not yet: web UI, browser E2E with real LiveKit, PS5 receiver, research docs.

## Milestone 2 — browser streaming, viewer and Player 2 in the party page

Implemented: `apps/web/src/features/shareplay/` — stream controller (screen share via
getDisplayMedia, HDMI capture card via getUserMedia; H.264 + VP8 backup, simulcast low layer,
`maintain-framerate`, Opus stereo game audio without voice processing), viewer (video, game
audio, volume, mute, fullscreen, resolution/fps/bitrate/loss/jitter/buffer/RTT stats,
reconnection, ended state, cleanup), host controls (capture indicator, viewer count, quality,
stop), SharePlay bar across pages, Player 2 request/allow/deny/revoke UI, Gamepad API sender
with neutral-input policy, host input monitor with watchdog.

Tests: web unit 39 (5 new); browser E2E with a real LiveKit 1.12.0 server
(`apps/web/tests/e2e/shareplay.spec.ts`, test-generated canvas video + tone — NOT PS5 capture):
passed alone and together with the full suite (8/8).

## Milestone 3 — PS5 research and the console diagnostic receiver

Research: `docs/SHAREPLAY_PS5_RESEARCH.md` (native capture UNSUPPORTED; virtual controller
UNSUPPORTED in AKENO — the SDK lists `scePadVirtualDevice*` names without signatures and known
community implementations need kernel credential changes and SceShellCore injection).

Implemented: `clients/ps5/app/shareplay_rx.{c,h}` (validates, orders and watchdogs relayed
Player 2 state; adapters for virtual pad and native capture always report unsupported),
`/api/shareplay` local route, SharePlay tab (`app/web/shareplay.js`). No new system imports.

Tests: C unit 564 checks/build (ASan/UBSan, both TLS builds, Valgrind clean); integration step
"receives SharePlay Player 2 input as a diagnostic receiver" with the real server; PS5 ELF built
locally with the pinned SDK: import gate OK, 96 imports (unchanged). Never run on a PS5.
