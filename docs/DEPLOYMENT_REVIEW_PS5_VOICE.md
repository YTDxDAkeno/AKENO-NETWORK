# Deployment review — PS5 party voice (server side)

**Status: prepared for review. Nothing has been deployed.** Console voice (AKENO Native
1.1.0-beta.1) needs **two server-side changes** that are not running anywhere yet:

1. the **AKENO server** with the device voice pass (`POST /api/v1/parties/:id/voice/device-token`);
2. the new **voice gateway** service (`apps/voice-gateway`) behind Caddy at `/voice-gateway`.

Until both run on the server the console uses, **Join voice cannot work** on the PS5. The console
then shows "Console voice is not set up on this AKENO server" (or the Join row stays hidden when
party voice itself is off). Nothing else in AKENO Native depends on them.

## What changes on the server

| Area | Change | Database | Risk |
| --- | --- | --- | --- |
| `apps/server/src/modules/voice` | device route and `issueDeviceToken`; the remembered credential is a session **or** a device; joins and reconciliation check the device | none (no migration) | low: browser voice paths are the same code with the same checks (tests unchanged and passing) |
| `apps/server/src/modules/devices` | `hasScope`; unpairing / removing `parties:write` ends that console's voice | none | low |
| `apps/server/src/modules/auth/service.ts` | a password change also ends console voice | none | low |
| `apps/server/src/modules/devices/scopes.ts` | the route is allowed for devices with `parties:write` | none | low: one more device route; default deny unchanged |
| `apps/server/src/config/env.ts` | `VOICE_GATEWAY_URL` (optional, `wss://` only, needs LiveKit) | — | none when empty |
| `packages/shared` | error code `VOICE_GATEWAY_UNAVAILABLE` | — | none |
| `compose.yaml` | `voice-gateway` service in the `voice` profile; `VOICE_GATEWAY_URL` passed to the app | — | only with `--profile voice` |
| `infrastructure/caddy/Caddyfile` | `handle /voice-gateway` → `voice-gateway:7890` | — | only that path |
| `infrastructure/docker/voice-gateway.Dockerfile` | new image (Debian slim: LiveKit's Node SDK ships glibc builds) | — | new image, not built in this environment (no Docker daemon); CI builds and tests the code |

Exact diff: `git diff 34c2485..claude/native-voice -- apps/server packages/shared compose.yaml infrastructure .env.example`
(12 files, about 300 lines, of which 113 are tests).

## Precondition: which code is in production

`docs/PROJECT_STATUS.md` records production as `af571eb`, a commit **not on GitHub**. The voice
work sits on the native line (`claude/laughing-ptolemy-op1d4e` → PR #7 → PR #8 →
`claude/native-voice`). Before any deployment:

1. push or otherwise publish the VPS-only commits (procedure in PROJECT_STATUS.md, "Reconciling the
   VPS-only commits");
2. compare that production line with this branch's server files (table above) and merge the voice
   changes **into the production line** in a reviewed PR; resolve conflicts there, never on the
   VPS;
3. run the full CI on that merge (`ci.yml`: server, web, E2E with LiveKit; `ps5-native.yml`).

**Do not deploy this branch as is** to production without that comparison: it was developed against
the native line, not against `af571eb`.

## Staging first (disposable infrastructure)

1. A test VPS or local machine with the merged build, a copy-free fresh database, its own domain
   and certificates, `--profile voice`.
2. `.env`: LiveKit as in docs/VOICE.md, plus
   `VOICE_GATEWAY_URL=wss://<staging domain>/voice-gateway`.
3. `docker compose --profile voice up -d --build`; check `docker compose ps` (all healthy) and from
   inside the network `wget -qO- http://voice-gateway:7890/healthz` → `{"ok":true,"sessions":0}`.
4. Pair a test console (or the PC build with `AKENO_NATIVE_SIM_MIC=tone:300`) to staging and run
   the hardware plan's V1–V4 there before production.

## Production rollout (after staging and the owner's go)

1. Backup: `infrastructure/scripts/backup.sh` (database and media; voice stores nothing).
2. Deploy the merged server build (no migration).
3. Build and start the gateway: `docker compose --profile voice up -d --build voice-gateway`, then
   reload Caddy.
4. Set `VOICE_GATEWAY_URL` and restart only the app container.
5. Verify: browser voice still works (two browsers); the console's Join voice works; the gateway
   log shows `console voice session live` / `ended`; `GET /healthz`.
6. Firewall: **no new port** (consoles use 443 through Caddy). LiveKit's ports stay as they are.

## Rollback (server)

| Step | Effect |
| --- | --- |
| `VOICE_GATEWAY_URL=` (empty) and restart the app | consoles get 503 "Console voice is not set up"; browsers unaffected |
| `docker compose stop voice-gateway` (and remove the Caddy route if wanted) | connected consoles are disconnected; nothing else changes |
| Redeploy the previous server build | the device route disappears (`404`); no data to migrate back |

The gateway holds no state; stopping it loses nothing.

## Security review checklist

- [ ] The gateway accepts only passes signed with the LiveKit secret, for `akeno-party-*` rooms,
      with `akeno.client=console-gateway` (tests: refused browser pass, foreign signature, garbage).
- [ ] Requests with an `Origin` header are refused (browsers cannot use the gateway).
- [ ] One session per account; limits on message size, uplink rate, buffering; idle timeouts.
- [ ] The pass is device-bound on the server; unpairing / scope removal / password change end voice.
- [ ] The console only connects to a `wss://` gateway on its own AKENO server's host.
- [ ] Logs: the gateway logs user ID, room, counts; never tokens or audio. LiveKit logs joins as before.
- [ ] Nothing is recorded: no egress, no files; audio exists only in memory while it passes.

## Capacity

Per console in voice: ~1 % of one CPU core for Opus (measured 0.2 ms per 20 ms frame on the CI
class of machine), ~64 kbit/s (32 down + 32 up) on 443, plus one LiveKit participant.
`GATEWAY_MAX_SESSIONS` (default 50) caps it.
