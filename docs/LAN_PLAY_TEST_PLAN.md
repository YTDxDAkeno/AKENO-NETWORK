# AKENO LAN Play — test plan

What is tested now (control plane), and the phased plan for a future transport. A green CI run proves
the control plane and the builds; it never proves console or game compatibility (`docs/SAFETY.md`).

## Automated tests in this change

**Server integration — `apps/server/test/lanplay.test.ts` (14 tests, part of `pnpm test`).**
Run against real PostgreSQL and the real HTTP stack, like every other server test.

- Availability: off unless `LAN_PLAY_ENABLED=true`; disabled server refuses create with
  `LAN_PLAY_UNAVAILABLE`.
- Honest reporting: `/lanplay/status` shows transport `none`, `available:false`, reason
  "Transport not available", and **no** platform pair marked supported.
- Compatibility DB: every entry is weaker than `hardware-tested` and cites a source.
- Lifecycle & roles: one session per party; host creates, members join, non-members refused;
  capacity enforced (`LAN_PARTICIPANTS_FULL`); only the host selects the game and only known games;
  the party leader can end a session a member hosts; a non-host/non-leader cannot.
- Connect is refused with `LAN_TRANSPORT_UNAVAILABLE` (no transport).
- Revocation: session ends when the host leaves the party; a player is removed when it leaves; a
  block removes the participant.
- Credential lifecycle (with a **stub** transport that carries nothing): a credential is issued,
  verified by `verifyCredential`, **expires** on its TTL, and is **revoked** when the participant
  leaves. Proves the control plane, not a data plane.

Also: typecheck, lint, the shared and web unit suites, and the C client / console build + import gate
are unchanged and still pass (LAN Play adds nothing to the PS5 payload).

## Phased plan for a future transport (not in this build)

**Phase 1 — network feasibility.** Document the exact capture/forward path for a PS5 behind a Windows
bridge; confirm the required adapter capabilities; provide a packet-flow diagram; stop and report if
a packet cannot traverse the path. (Design captured in `LAN_PLAY_FEASIBILITY.md`.)

**Phase 2 — PC↔PC transport.** Two isolated environments: verify real UDP delivery, TCP if supported,
bidirectional traffic, latency/jitter/loss measurement, packet filtering, authorization, session
revocation, reconnection, interruption, invalid/malicious packets, relay fallback. A passing unit
test is **not** LAN gameplay.

**Phase 3 — real LAN game.** A specific title with a documented offline LAN mode: host and discover it
on a normal LAN first, then over the AKENO tunnel. Record edition, platforms, topology, discovery,
join, gameplay, stability, latency. No success claim until two real instances exchange gameplay state.

**Phase 4 — PS5↔PS5.** Real consoles or external participants. One console only → mark **pending**.
Never claim PS5↔PS5 from mocked clients.

**Phase 5 — PS5↔PC.** Only a title whose PS5 and Windows builds genuinely support compatible LAN
multiplayer; an online crossplay feature is not proof. If none is found, report the limitation.

**Required evidence for any hardware phase:** logs, packet counts, connection status, exact software
versions, latency measurements, screenshots where applicable, and an explicit split of what was
simulated vs hardware-tested. Never fabricate a console test result.

## How to run the control-plane tests

```bash
pnpm install
pnpm --filter @akeno/server exec vitest run lanplay   # LAN Play only
pnpm test                                             # shared + server + web, incl. LAN Play
pnpm lint && pnpm typecheck
```

Server tests need a PostgreSQL role that can `CREATE DATABASE` (see `docs/TESTING.md`).
