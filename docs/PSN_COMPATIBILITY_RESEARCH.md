# PSN compatibility and server emulation — findings

**Question:** could AKENO NETWORK make existing PS5 system features talk to a self-hosted server
by emulating the services they normally use?

**Answer: not pursued, and not feasible within this project's rules.**

## Why a compatible replacement is not a configuration change

A system feature that talks to an online service depends on far more than the address it
connects to:

| Requirement | Why redirecting traffic does not satisfy it |
| --- | --- |
| TLS trust | Clients only accept certificates for the real service names from trust anchors they ship with. A self-hosted server cannot present such certificates. |
| Authentication and account identity | Features are bound to the signed-in platform account and its tokens. AKENO accounts are a different identity system. |
| Protocol compatibility | Request and response formats, signatures and state machines are proprietary and undocumented; each feature (and often each game) differs. |
| Signed or verified responses | Some data must verify against keys the server operator does not have. |
| Game-specific session behavior | Multiplayer sessions, matchmaking and invitations are implemented per game. |

Overcoming these would require installing custom trust anchors, disabling certificate checks,
impersonating production services, collecting or replaying platform credentials, or modifying
system software. **All of these are excluded** by the project brief and by
[SAFETY.md](SAFETY.md): they put users' accounts and consoles at risk, may affect third parties,
and would make AKENO depend on reverse-engineered behavior that can change with any update.

## Decisions

- AKENO does not redirect, proxy, intercept or emulate any Sony service, and never asks for PSN
  credentials.
- DNS settings cannot provide PSN compatibility (see
  [VPS_DEPLOYMENT.md](VPS_DEPLOYMENT.md#what-dns-does--and-does-not-do)).
- AKENO's own protocol is documented ([API.md](API.md)) so that **independent** clients — the
  AKENO console client, a web browser, or a game written for AKENO — can integrate on purpose.
- Messages, parties and invitations created in AKENO are always AKENO data. They are never
  presented as, or mixed with, PSN messages.

## Evidence levels

| Item | Level |
| --- | --- |
| Self-hosted replacement of system friends/party/voice services without the excluded techniques | 5 |
| Independent client using AKENO's documented API | 1 (tested on Linux) |
| Third-party projects claiming private online layers (OrbisNet for PS4, BlueSphere) | 3 — documented by their authors, not verified, game- or platform-specific, not used |
