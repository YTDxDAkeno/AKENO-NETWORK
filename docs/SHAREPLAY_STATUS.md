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
